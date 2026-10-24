// Двери и цена прохода по клеткам боя (порт TileEngine::unitOpensDoor и Tile::openDoor).
// Живёт в банке поиска пути: в банке экрана боя (28) места уже нет.
//
// Свойства частей миссии экран боя раскладывает в странице поиска пути (pathfind.h):
// цена прохода (MCD.TU_Walk, 255 — стена), чем заменяется открытая дверь и в какой слот
// клетки эту замену класть. Распашная дверь меняется на часть Alt_MCD, дверь НЛО — на
// заведённую при загрузке часть «в движении», которой экран боя прокручивает кадры 1..7 створки
// (Tile::openDoor и Tile::animate), а на кадре 7 — на часть с открытым кадром.
#include <stdint.h>
#include "far.h"
#include "pathfind.h"
#include "loader.h"
#include "doors.h"

#define AN_MAX 4                         // створок в движении разом (дверь и соседние)
uint8_t door_an_n;                            // сколько створок едет (обнуляет загрузка миссии, loader.c)
static uint8_t an_f;                     // и их кадр
static uint8_t an_c[AN_MAX * 4];         // x, y, z, слот

// Какие стены смотреть при открывании двери (боец 1x1): тройки (dx, dy, слот), слот 1 —
// западная стена клетки, 2 — северная. У направлений наискось правая кнопка добавляет ещё
// две проверки — поэтому их по четыре.
static const int8_t door_chk[] = {
	0, 0, 2,                                              // 0 север
	0, 0, 2,   1, -1, 1,    1, 0, 1,   1, 0, 2,           // 1 северо-восток (+правая кнопка)
	1, 0, 1,                                              // 2 восток
	1, 1, 1,   1, 1, 2,     1, 0, 1,   0, 1, 2,           // 3 юго-восток
	0, 1, 2,                                              // 4 юг
	0, 0, 1,   -1, 1, 2,    0, 1, 1,   0, 1, 2,           // 5 юго-запад
	0, 0, 1,                                              // 6 запад
	0, 0, 1,   0, 0, 2,     0, -1, 1,  -1, 0, 2,          // 7 северо-запад
};
static const uint8_t door_ofs[8] = { 0, 1, 5, 6, 10, 11, 15, 16 };   // в тройках
static const uint8_t door_n[8] = { 1, 2, 1, 2, 1, 2, 1, 2 };         // без правой кнопки

// Номер клетки считается в 16 битах: на 32-битное умножение уходило слишком много времени
static far_t cell_addr(const dmap_t *m, uint8_t x, uint8_t y, uint8_t z)
{
	uint16_t idx = (uint16_t)(((uint16_t)z * m->sy + y) * m->sx + x);
	return m->cells + ((uint32_t)idx << 2);
}

static uint8_t part_get(const dmap_t *m, uint16_t off)
{
	return far_byte(FAR(m->page, off));
}

uint8_t dir_to(int16_t dx, int16_t dy) __banked
{
	int16_t ax = dx < 0 ? -dx : dx, ay = dy < 0 ? -dy : dy;
	if (!dx && !dy) return 0;
	if (ax * 5 < ay * 2) return dy < 0 ? 0 : 4;
	if (ay * 5 < ax * 2) return dx > 0 ? 2 : 6;
	if (dx > 0) return dy < 0 ? 1 : 3;
	return dy < 0 ? 7 : 5;
}

uint8_t cell_cost(const dmap_t *m, uint8_t x, uint8_t y, uint8_t z, uint8_t d, uint8_t step) __banked
{
	uint8_t c[4];
	far_read(cell_addr(m, x, y, z), c, 4);
	uint8_t cost = part_get(m, PF_TU + c[0]);
	if (cost == 255) cost = 0;
	if (c[3]) {
		uint8_t o = part_get(m, PF_TU + c[3]);
		if (o != 255) cost += o;
	}
	if (!cost) cost = step;
	if (d & 1) cost += cost >> 1;
	return cost;
}

// Есть ли в клетке пол: сама часть пола и флаг «нет пола» (MCD.No_Floor)
static uint8_t has_floor(const dmap_t *m, uint8_t x, uint8_t y, uint8_t z)
{
	uint8_t c[4];
	far_read(cell_addr(m, x, y, z), c, 4);
	if (!c[0]) return 0;
	return (part_get(m, PF_FLAGS + c[0]) & 1) == 0;
}

// Насколько опущен пол клетки (Tile::getTerrainLevel): меньшее из T_Level пола и объекта
static int8_t terrain_level(const dmap_t *m, uint8_t x, uint8_t y, uint8_t z)
{
	uint8_t c[4];
	far_read(cell_addr(m, x, y, z), c, 4);
	int8_t lv = c[0] ? (int8_t)part_get(m, PF_TLEVEL + c[0]) : 0;
	if (c[3]) {
		int8_t o = (int8_t)part_get(m, PF_TLEVEL + c[3]);
		if (o < lv) lv = o;
	}
	return lv;
}

uint8_t cell_step_z(const dmap_t *m, uint8_t fx, uint8_t fy, uint8_t z, uint8_t tx, uint8_t ty) __banked
{
	uint8_t nz = z;
	// стоим высоко на ступенях — шаг поднимает на этаж, если наверху есть на что встать
	if (z + 1 < m->sz && terrain_level(m, fx, fy, z) <= -16 && has_floor(m, tx, ty, (uint8_t)(z + 1)))
		nz = (uint8_t)(z + 1);
	while (nz && !has_floor(m, tx, ty, nz)) nz--;   // под ногами пусто — падаем ниже
	return nz;
}

// Заменить часть slot клетки (t) на открытую створку и записать клетку в список перерисовки;
// дверь НЛО ещё и в список открытых — в конце хода она закроется (TileEngine::closeUfoDoors)
static void door_swap(const dmap_t *m, uint8_t x, uint8_t y, uint8_t z, uint8_t slot, uint8_t t,
		      uint8_t alt, uint8_t as, uint8_t *list, uint8_t maxcells, uint8_t *n)
{
	uint8_t c[4];
	far_t a = cell_addr(m, x, y, z);
	far_read(a, c, 4);
	c[slot] = 0;
	c[as & 3] = alt;
	far_write(a, c, 4);
	if (*n < maxcells) { list[*n * 2] = x; list[*n * 2 + 1] = y; (*n)++; }
	if ((part_get(m, PF_ALTSLOT + alt) & 0x40) && door_an_n < AN_MAX) {   // створка поедет
		uint8_t *c4 = an_c + door_an_n * 4;
		c4[0] = x; c4[1] = y; c4[2] = z; c4[3] = as & 3;
		door_an_n++;
		an_f = 0;
	}
	uint8_t k = part_get(m, PF_UFOOPEN);
	if ((as & 0x80) && k < PF_UFOMAX) {
		uint8_t r[5] = { x, y, z, slot, t };
		far_write(FAR(m->page, PF_UFOOPEN + 1 + k * 5), r, 5);
		k++;
		far_write(FAR(m->page, PF_UFOOPEN), &k, 1);
	}
}

uint8_t doors_close_ufo(const dmap_t *m, uint8_t *list, uint8_t maxcells) __banked
{
	uint8_t k = part_get(m, PF_UFOOPEN), n = 0;
	for (uint8_t i = 0; i < k; i++) {
		uint8_t r[5], c[4];
		far_read(FAR(m->page, PF_UFOOPEN + 1 + i * 5), r, 5);
		far_t a = cell_addr(m, r[0], r[1], r[2]);
		far_read(a, c, 4);
		uint8_t alt = part_get(m, PF_ALT + r[4]), as = part_get(m, PF_ALTSLOT + r[4]) & 3;
		// в клетке створка в движении или уже открытая; иначе её сменили (разрушение и т. п.)
		if (c[as] != alt && c[as] != part_get(m, PF_ALT + alt)) continue;
		c[as] = 0;
		c[r[3]] = r[4];
		far_write(a, c, 4);
		if (n < maxcells) { list[n * 3] = r[0]; list[n * 3 + 1] = r[1]; list[n * 3 + 2] = r[2]; n++; }
	}
	k = 0;
	far_write(FAR(m->page, PF_UFOOPEN), &k, 1);
	door_an_n = 0;                            // закрылись — ехать нечему
	return n;
}

uint8_t doors_anim(const dmap_t *m, uint8_t *parts, uint8_t *cells) __banked
{
	uint8_t np = 0, nc = 0;
	parts[0] = 0; cells[0] = 0xFF;
	if (!door_an_n) return 0;
	an_f++;
	for (uint8_t i = 0; i < door_an_n; i++) {
		const uint8_t *c4 = an_c + i * 4;
		uint8_t c[4], s = c4[3];
		far_t a = cell_addr(m, c4[0], c4[1], c4[2]);
		far_read(a, c, 4);
		uint8_t t = c[s];
		if (!(part_get(m, PF_ALTSLOT + t) & 0x40)) continue;   // створку уже сменили
		if (an_f >= 7) {                     // доехала — открытая створка, проход свободен
			c[s] = part_get(m, PF_ALT + t);
			far_write(a, c, 4);
		} else {                             // кадр створки — в таблицу вида её части
			uint8_t e[TE_SIZE], j;
			uint8_t d = far_byte(FAR(m->page + 1, TL_UDIDX) + t);
			far_read(FAR(m->page + 1, TL_UDFR) + (uint16_t)(d * 7 + an_f - 1) * TE_SIZE, e, TE_SIZE);
			far_write(FAR(m->page + 1, TL_TILES) + (uint16_t)(t - 1) * TE_SIZE, e, TE_SIZE);
			for (j = 0; j < np && parts[j] != t; j++) ;
			if (j == np) { parts[np++] = t; parts[np] = 0; }
		}
		cells[nc * 3] = c4[0]; cells[nc * 3 + 1] = c4[1]; cells[nc * 3 + 2] = c4[2];
		nc++;
		cells[nc * 3] = 0xFF;
	}
	if (an_f >= 7) door_an_n = 0;
	return door_an_n;
}

// Двери НЛО открываются парой: соседние по стене створки НЛО (той же стены клетки) открываются
// вместе (TileEngine::checkAdjacentDoors — только две соседние клетки, створки могут быть разными
// частями: у левой и правой половины свои кадры).
static void door_adjacent(const dmap_t *m, uint8_t x, uint8_t y, uint8_t z, uint8_t slot,
			  uint8_t *list, uint8_t maxcells, uint8_t *n)
{
	for (int8_t s = -1; s <= 1; s += 2) {
		int16_t cx = x, cy = y;
		if (slot == 1) cy += s; else cx += s;
		if (cx < 0 || cy < 0 || cx >= m->sx || cy >= m->sy) continue;
		uint8_t c[4];
		far_read(cell_addr(m, (uint8_t)cx, (uint8_t)cy, z), c, 4);
		uint8_t t = c[slot], as = part_get(m, PF_ALTSLOT + t);
		if (!t || (as & 0xC0) != 0x80) continue;      // не закрытая створка НЛО
		door_swap(m, (uint8_t)cx, (uint8_t)cy, z, slot, t, part_get(m, PF_ALT + t), as, list, maxcells, n);
	}
}

uint8_t door_open(const dmap_t *m, uint8_t x, uint8_t y, uint8_t z, uint8_t dir, uint8_t rclick,
		  uint8_t tu, uint8_t *spent, uint8_t *list, uint8_t maxcells) __banked
{
	uint8_t n = door_n[dir];             // наискось правая кнопка смотрит ещё две стены
	if (rclick && n > 1) n = 4;
	const int8_t *p = door_chk + (uint16_t)door_ofs[dir] * 3;
	*spent = 0;
	for (uint8_t i = 0; i < n; i++, p += 3) {
		int16_t cx = (int16_t)x + p[0], cy = (int16_t)y + p[1];
		if (cx < 0 || cy < 0 || cx >= m->sx || cy >= m->sy) continue;
		uint8_t slot = (uint8_t)p[2], c[4];
		far_read(cell_addr(m, (uint8_t)cx, (uint8_t)cy, z), c, 4);
		uint8_t t = c[slot];
		if (!t) continue;
		uint8_t alt = part_get(m, PF_ALT + t);
		if (!alt) continue;                          // часть не дверь
		uint8_t as = part_get(m, PF_ALTSLOT + t), cnt = 0;
		if (as & 0x40) return 3;                     // створка НЛО ещё едет — ждать
		uint8_t cost = part_get(m, PF_TU + t);
		if (cost == 255) cost = 0;
		if (tu < cost) return 1;
		door_swap(m, (uint8_t)cx, (uint8_t)cy, z, slot, t, alt, as, list, maxcells, &cnt);
		if (as & 0x80) door_adjacent(m, (uint8_t)cx, (uint8_t)cy, z, slot, list, maxcells, &cnt);
		*spent = cost;
		return cnt ? 0 : 2;
	}
	return 2;
}

uint8_t door_find(const dmap_t *m, uint8_t z, uint8_t *x, uint8_t *y, uint8_t *slot) __banked
{
	for (uint8_t cy = 0; cy < m->sy; cy++)
		for (uint8_t cx = 0; cx < m->sx; cx++) {
			uint8_t c[4];
			far_read(cell_addr(m, cx, cy, z), c, 4);
			for (uint8_t s = 1; s <= 2; s++)
				if (c[s] && part_get(m, PF_ALT + c[s])) {
					*x = cx; *y = cy; *slot = s;
					return 1;
				}
		}
	return 0;
}
