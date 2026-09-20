// Генератор наземной миссии: порт BattlescapeGenerator::generateMap (16 §3.2).
//
// Карта собирается из блоков террейна по mapScript, как в оригинале: поле считается в
// модулях 10x10 клеток, команды скрипта занимают модули блоками, потом выбранные блоки
// укладываются в клетки. Данные готовит конвертер (OxzConv/Core/BattleData.cs):
//   TERRAINS   — террейны: скрипт, наборы MCD, блоки (ресурс, размеры, группы);
//   MAPSCRIPTS — скрипты: команды со всеми полями;
//   MCDSET_*   — части набора (8 байт на часть), в SPG;
//   MAPBLK_*   — блок карты (как MAP оригинала), на SD;
//   TILESET_*  — кадры набора, на SD.
//
// Что уже есть: addBlock, addLine, fillArea, checkBlock, removeBlock, resize и сами укладка
// и трансляция номеров частей. Корабль, НЛО и тоннели (addCraft/addUFO/digTunnel) — следующий
// шаг, пока такие команды пропускаются (14 §todo).
#include <stdint.h>
#include <string.h>
#include "pages.h"
#include "far.h"
#include "res.h"
#include "res_ids.h"
#include "game.h"
#include "mapgen.h"
#include "dbg.h"

#define MOD_W       10                 // сторона модуля в клетках
#define MAX_MOD     8                  // модулей по стороне (до 80x80 клеток)
#define MAX_PARTS   255                // частей в наборах террейна (номер в клетке — байт)
#define MAX_BLOCKS  64                 // блоков в террейне, которые различает генератор

// Команды скрипта — те же номера, что в конвертере
#define C_ADD_BLOCK 0
#define C_ADD_LINE  1
#define C_ADD_CRAFT 2
#define C_ADD_UFO   3
#define C_DIG_TUNNEL 4
#define C_FILL_AREA 5
#define C_CHECK_BLOCK 6
#define C_REMOVE_BLOCK 7
#define C_RESIZE    8

// Группы блоков (MapBlockType): 0 обычный, 1 посадочная площадка, 2 перекрёсток,
// 3 дорога с севера на юг, 4 дорога с запада на восток, 5 НЛО, 6 корабль
#define G_DEFAULT   0
#define G_LANDING   1
#define G_CROSS     2
#define G_NS        3
#define G_EW        4

typedef struct {
	uint16_t res;                      // ресурс MAPBLK
	uint8_t w, l, sz, groups;          // размеры в клетках и маска групп
} blk_t;

// Состояние генерации — в памяти банка (Win1 занят), см. ram= в build.ps1
typedef struct {
	uint8_t mx, my;                    // размер поля в модулях
	uint8_t sz;                        // этажей
	uint8_t grid[MAX_MOD * MAX_MOD];   // номер блока + 1 в модуле (0 — пусто)
	uint8_t head[MAX_MOD * MAX_MOD];   // 1 — верхний левый угол блока (его и укладываем)
	uint8_t nblk;
	blk_t blk[MAX_BLOCKS];
	uint8_t uses[MAX_BLOCKS];          // сколько раз блок уже поставлен (maxUses)
	uint8_t nsets;
	uint16_t set[8];                   // ресурсы MCDSET террейна
	uint8_t setn[8];                   // частей в каждом наборе — для трансляции номеров
	uint16_t tset[8];                  // ресурсы TILESET
	uint8_t tpage[8];                  // страницы, куда загружены тайлсеты
	uint8_t tpages[8];                 // сколько страниц занял тайлсет
	far_t cells;                       // карта: sx*sy*sz клеток по 4 байта
	uint8_t page;                      // первая страница карты
	uint8_t pages;
	uint16_t sx, sy;                   // размер карты в клетках
	uint8_t label_ok[16];              // успех команд с метками (conditionals)
	uint8_t craft_x, craft_y, craft_w, craft_l;   // где встал корабль отряда (в клетках)
} gen_t;

static gen_t __at(0xB000) G;
// Какие корабль и НЛО ставить: номера террейнов из таблицы TERRAINS (#FFFF — не ставить).
// Пока их задаёт вызывающий (отладка), потом — миссия: корабль отряда и тип НЛО.
static uint16_t gen_craft = 0xFFFF, gen_ufo = 0xFFFF;

// ------------------------------------------------------------------ чтение правил

// Запись террейна в TERRAINS: i16 script, u8 nSets, u8 nBlocks, наборы, блоки
static uint8_t load_terrain(uint16_t idx, int16_t *script)
{
	res_t r;
	if (!res_find(RES_TERRAINS, &r)) return 0;
	uint16_t n = far_word(r.phys);
	if (idx >= n) return 0;
	far_t p = r.phys + far_word(r.phys + 2 + (uint32_t)idx * 2);
	*script = (int16_t)far_word(p);
	G.nsets = far_byte(p + 2);
	G.nblk = far_byte(p + 3);
	if (G.nsets > 8) G.nsets = 8;
	if (G.nblk > MAX_BLOCKS) G.nblk = MAX_BLOCKS;
	p += 5;                                 // за nBlocks идёт вид террейна (kind)
	for (uint8_t i = 0; i < G.nsets; i++) { G.set[i] = far_word(p); G.tset[i] = far_word(p + 2); p += 4; }
	for (uint8_t i = 0; i < G.nblk; i++) {
		uint8_t b[6];
		far_read(p, b, 6);
		G.blk[i].res = (uint16_t)b[0] | ((uint16_t)b[1] << 8);
		G.blk[i].w = b[2]; G.blk[i].l = b[3]; G.blk[i].sz = b[4]; G.blk[i].groups = b[5];
		G.uses[i] = 0;
		p += 6;
	}
	// размеры наборов нужны, чтобы переводить номер части блока в номер части миссии
	for (uint8_t i = 0; i < G.nsets; i++) {
		res_t rs;
		G.setn[i] = res_find(G.set[i], &rs) ? (uint8_t)rs.a : 0;
	}
	return 1;
}

// ------------------------------------------------------------------ выбор блоков

static uint8_t in_group(uint8_t i, uint8_t g) { return (G.blk[i].groups & (1 << g)) != 0; }

// Блок по группам/списку команды: как MapScript::getNextBlock — из блоков нужной группы
// случайный, с учётом maxUses и того, что блок должен влезть в свободное место.
static int16_t pick_block(uint8_t group, const uint8_t *blocks, uint8_t nblocks,
			  const uint8_t *maxuses, uint8_t size)
{
	uint8_t cand[MAX_BLOCKS], n = 0;
	if (nblocks) {
		for (uint8_t i = 0; i < nblocks && n < MAX_BLOCKS; i++) {
			uint8_t b = blocks[i];
			if (b >= G.nblk) continue;
			if (maxuses && maxuses[i] && G.uses[b] >= maxuses[i]) continue;
			if (size && G.blk[b].w != size * MOD_W) continue;
			cand[n++] = b;
		}
	} else {
		for (uint8_t i = 0; i < G.nblk && n < MAX_BLOCKS; i++) {
			if (!in_group(i, group)) continue;
			if (size && G.blk[i].w != size * MOD_W) continue;
			cand[n++] = i;
		}
	}
	if (!n) return -1;
	return cand[rng_next() % n];
}

// Свободно ли место под блок size x size модулей в (mx, my)
static uint8_t area_free(uint8_t x, uint8_t y, uint8_t size)
{
	if (x + size > G.mx || y + size > G.my) return 0;
	for (uint8_t j = 0; j < size; j++)
		for (uint8_t i = 0; i < size; i++)
			if (G.grid[(y + j) * MAX_MOD + x + i]) return 0;
	return 1;
}

// Занять место блоком (в оригинале — addBlock, :2717)
static void put_block(uint8_t x, uint8_t y, uint8_t b)
{
	uint8_t sw = G.blk[b].w / MOD_W, sl = G.blk[b].l / MOD_W;
	if (!sw) sw = 1;
	if (!sl) sl = 1;
	for (uint8_t j = 0; j < sl; j++)
		for (uint8_t i = 0; i < sw; i++) {
			G.grid[(y + j) * MAX_MOD + x + i] = b + 1;
			G.head[(y + j) * MAX_MOD + x + i] = 0;
		}
	G.head[y * MAX_MOD + x] = 1;
	G.uses[b]++;
}

// Место под блок: в пределах прямоугольников команды (в модулях) или по всей карте
static uint8_t find_place(const int16_t *rects, uint8_t nrects, uint8_t size, uint8_t *ox, uint8_t *oy)
{
	uint8_t tries = 64;
	while (tries--) {
		uint8_t x, y;
		if (nrects) {
			uint8_t k = rng_next() % nrects;
			int16_t rx = rects[k * 4], ry = rects[k * 4 + 1], rw = rects[k * 4 + 2], rl = rects[k * 4 + 3];
			if (rw <= 0) rw = 1;
			if (rl <= 0) rl = 1;
			x = (uint8_t)(rx + rng_next() % rw);
			y = (uint8_t)(ry + rng_next() % rl);
		} else {
			x = rng_next() % G.mx;
			y = rng_next() % G.my;
		}
		if (area_free(x, y, size)) { *ox = x; *oy = y; return 1; }
	}
	// случайно не нашли — пройти подряд
	for (uint8_t y = 0; y < G.my; y++)
		for (uint8_t x = 0; x < G.mx; x++)
			if (area_free(x, y, size)) { *ox = x; *oy = y; return 1; }
	return 0;
}

// ------------------------------------------------------------------ укладка клеток

// Номер части блока (как в MAP оригинала — сквозной по наборам террейна) уже годится:
// наборы миссии идут в том же порядке, поэтому номер остаётся собой. Трансляция нужна
// только когда добавляются наборы корабля и НЛО — это следующий шаг.
// Сдвиг номеров частей в строке клеток (0 — пусто, его не трогаем)
static void shift_row(uint8_t *row, uint16_t len, uint8_t offset)
{
	for (uint16_t i = 0; i < len; i++)
		if (row[i]) row[i] += offset;
}

// offset — сдвиг номеров частей: у блока чужого террейна (корабль, НЛО) его наборы MCD
// добавлены к наборам миссии после основных, как mapDataSetOffset в loadMAP.
static void lay_res(uint8_t mx, uint8_t my, uint16_t res, uint8_t offset)
{
	res_t r;
	if (!res_find(res, &r)) return;
	uint8_t h[4];
	far_read(r.phys, h, 4);
	uint8_t bx = h[0], by = h[1], bz = h[2];
	uint8_t row[MOD_W * 4 * 4];            // до 4 модулей в ширину
	uint16_t rowlen = (uint16_t)bx * 4;
	if (rowlen > sizeof row) return;
	for (uint8_t z = 0; z < bz && z < G.sz; z++)
		for (uint8_t y = 0; y < by; y++) {
			uint16_t wy = my * MOD_W + y;
			if (wy >= G.sy) break;
			far_read(r.phys + 4 + ((uint32_t)(z * by + y) * bx) * 4, row, rowlen);
			if (offset) shift_row(row, rowlen, offset);
			far_t dst = G.cells + ((uint32_t)(z * G.sy + wy) * G.sx + mx * MOD_W) * 4;
			uint16_t len = rowlen;
			if (mx * MOD_W + bx > G.sx) len = (uint16_t)(G.sx - mx * MOD_W) * 4;
			far_write(dst, row, len);
		}
}

static void lay_block(uint8_t mx, uint8_t my, uint8_t b)
{
	lay_res(mx, my, G.blk[b].res, 0);
}

// ------------------------------------------------------------------ корабль, НЛО, тоннели

// Блок чужого террейна (корабль X-COM или НЛО): его наборы MCD добавляются к наборам миссии,
// блок занимает свои модули и больше туда ничего не ставится — так же оригинал держит
// посадочную площадку (BattlescapeGenerator::addCraft/addUFO, :2609-2640).
static uint8_t place_extra(uint16_t terr, const int16_t *rects, uint8_t nrects, uint8_t is_craft)
{
	res_t r;
	if (!res_find(RES_TERRAINS, &r)) return 0;
	uint16_t n = far_word(r.phys);
	if (terr >= n) return 0;
	far_t p = r.phys + far_word(r.phys + 2 + (uint32_t)terr * 2);
	uint8_t nsets = far_byte(p + 2), nblk = far_byte(p + 3);
	if (!nsets || !nblk || G.nsets + nsets > 8) return 0;
	p += 5;

	// сдвиг номеров частей — столько их уже занято террейном миссии
	uint8_t base = 0;
	for (uint8_t i = 0; i < G.nsets; i++) base += G.setn[i];
	uint8_t added = 0, parts = 0;
	for (uint8_t i = 0; i < nsets; i++) {
		uint16_t ms = far_word(p + (uint32_t)i * 4), ts = far_word(p + (uint32_t)i * 4 + 2);
		res_t rs;
		uint8_t cnt = res_find(ms, &rs) ? (uint8_t)rs.a : 0;
		if ((uint16_t)base + parts + cnt > 255) return 0;   // номер части в клетке — байт
		G.set[G.nsets + added] = ms;
		G.tset[G.nsets + added] = ts;
		G.setn[G.nsets + added] = cnt;
		parts += cnt;
		added++;
	}
	p += (uint32_t)nsets * 4;

	uint8_t b[6];
	far_read(p + (uint32_t)(rng_next() % nblk) * 6, b, 6);
	uint16_t res = (uint16_t)b[0] | ((uint16_t)b[1] << 8);
	uint8_t sw = b[2] / MOD_W, sl = b[3] / MOD_W;
	if (!sw) sw = 1;
	if (!sl) sl = 1;
	uint8_t x, y;
	if (!find_place(rects, nrects, sw > sl ? sw : sl, &x, &y)) return 0;
	for (uint8_t j = 0; j < sl; j++)
		for (uint8_t i = 0; i < sw; i++) {
			G.grid[(y + j) * MAX_MOD + x + i] = 0xFF;        // место занято, кладём сами
			G.head[(y + j) * MAX_MOD + x + i] = 0;
		}
	lay_res(x, y, res, base);
	G.nsets += added;
	if (is_craft) {                                   // запомнить, куда высаживать отряд
		G.craft_x = x * MOD_W; G.craft_y = y * MOD_W;
		G.craft_w = b[2]; G.craft_l = b[3];
	}
	return 1;
}

// Проходы между модулями на уровне level: оригинал заменяет стены по краям модулей на
// «дверные» части (MCDReplacements), у нас таких замен в данных почти нет — тогда стена
// просто убирается и проход появляется (drillModules, :2769-2889).
static void dig_tunnel(uint8_t level, uint8_t dir)
{
	uint8_t zero = 0;
	if (level >= G.sz) return;
	uint8_t vertical = dir == 1 || dir == 3, horizontal = dir == 2 || dir == 3;
	for (uint8_t my = 0; my < G.my; my++)
		for (uint8_t mx = 0; mx < G.mx; mx++) {
			uint16_t cx = (uint16_t)mx * MOD_W + MOD_W / 2;
			uint16_t cy = (uint16_t)my * MOD_W + MOD_W / 2;
			if (vertical && my + 1 < G.my)
				for (uint8_t k = 0; k < 2; k++) {
					uint16_t wy = (uint16_t)(my + 1) * MOD_W;
					far_t c = G.cells + ((uint32_t)(level * G.sy + wy) * G.sx + cx + k) * 4;
					far_write(c + 2, &zero, 1);              // северная стена
				}
			if (horizontal && mx + 1 < G.mx)
				for (uint8_t k = 0; k < 2; k++) {
					uint16_t wx = (uint16_t)(mx + 1) * MOD_W;
					far_t c = G.cells + ((uint32_t)(level * G.sy + cy + k) * G.sx + wx) * 4;
					far_write(c + 1, &zero, 1);              // западная стена
				}
		}
}

// ------------------------------------------------------------------ скрипт

typedef struct {
	int16_t type, size, direction, chances, executions, label, ufo, tunnel;
	uint8_t nrects, ngroups, nblocks, nfreqs, nmaxuses, ncond, nrepl;
	int16_t rects[4 * 6];
	uint8_t groups[8], blocks[24], freqs[24], maxuses[24];
	int16_t cond[8];
} cmd_t;

static uint8_t read_cmd(far_t p, cmd_t *c, uint16_t *len)
{
	uint16_t w[16];
	far_read(p, w, sizeof w);
	*len = w[0];
	if (*len < 16) return 0;
	c->type = w[1]; c->size = w[2]; c->direction = w[3]; c->chances = w[4];
	c->executions = w[5]; c->label = w[6]; c->ufo = (int16_t)w[7]; c->tunnel = (int16_t)w[8];
	c->nrects = (uint8_t)w[9]; c->ngroups = (uint8_t)w[10]; c->nblocks = (uint8_t)w[11];
	c->nfreqs = (uint8_t)w[12]; c->nmaxuses = (uint8_t)w[13]; c->ncond = (uint8_t)w[14];
	c->nrepl = (uint8_t)w[15];
	far_t q = p + 32;
	uint8_t n = c->nrects > 6 ? 6 : c->nrects;
	for (uint8_t i = 0; i < n * 4; i++) c->rects[i] = (int16_t)far_word(q + (uint32_t)i * 2);
	q += (uint32_t)c->nrects * 8;
	c->nrects = n;
	n = c->ngroups > 8 ? 8 : c->ngroups;
	for (uint8_t i = 0; i < n; i++) c->groups[i] = (uint8_t)far_word(q + (uint32_t)i * 2);
	q += (uint32_t)c->ngroups * 2; c->ngroups = n;
	n = c->nblocks > 24 ? 24 : c->nblocks;
	for (uint8_t i = 0; i < n; i++) c->blocks[i] = (uint8_t)far_word(q + (uint32_t)i * 2);
	q += (uint32_t)c->nblocks * 2; c->nblocks = n;
	n = c->nfreqs > 24 ? 24 : c->nfreqs;
	for (uint8_t i = 0; i < n; i++) c->freqs[i] = (uint8_t)far_word(q + (uint32_t)i * 2);
	q += (uint32_t)c->nfreqs * 2; c->nfreqs = n;
	n = c->nmaxuses > 24 ? 24 : c->nmaxuses;
	for (uint8_t i = 0; i < n; i++) c->maxuses[i] = (uint8_t)far_word(q + (uint32_t)i * 2);
	q += (uint32_t)c->nmaxuses * 2; c->nmaxuses = n;
	n = c->ncond > 8 ? 8 : c->ncond;
	for (uint8_t i = 0; i < n; i++) c->cond[i] = (int16_t)far_word(q + (uint32_t)i * 2);
	c->ncond = n;
	return 1;
}

// conditionals: положительный номер — команда с такой меткой должна была удаться,
// отрицательный — провалиться (MapScript::getConditionals)
static uint8_t cond_ok(const cmd_t *c)
{
	for (uint8_t i = 0; i < c->ncond; i++) {
		int16_t v = c->cond[i];
		uint8_t want = v > 0, lab = (uint8_t)(v > 0 ? v : -v);
		if (lab >= 16) continue;
		if (G.label_ok[lab] != want) return 0;
	}
	return 1;
}

// Дорога: линия модулей с севера на юг (или с запада на восток) из блоков групп 3/4,
// пересечения — группа 2 (BattlescapeGenerator::addLine)
static uint8_t add_line(const cmd_t *c)
{
	uint8_t vertical = c->direction == 1 || c->direction == 3;
	uint8_t horizontal = c->direction == 2 || c->direction == 3;
	uint8_t ok = 0;
	if (vertical) {
		uint8_t x = rng_next() % G.mx;
		for (uint8_t y = 0; y < G.my; y++) {
			uint8_t cross = G.grid[y * MAX_MOD + x] != 0;
			int16_t b = pick_block(cross ? G_CROSS : G_NS, 0, 0, 0, 0);
			if (b < 0) continue;
			G.grid[y * MAX_MOD + x] = (uint8_t)b + 1;
			G.head[y * MAX_MOD + x] = 1;
			G.uses[b]++;
		}
		ok = 1;
	}
	if (horizontal) {
		uint8_t y = rng_next() % G.my;
		for (uint8_t x = 0; x < G.mx; x++) {
			uint8_t cross = G.grid[y * MAX_MOD + x] != 0;
			int16_t b = pick_block(cross ? G_CROSS : G_EW, 0, 0, 0, 0);
			if (b < 0) continue;
			G.grid[y * MAX_MOD + x] = (uint8_t)b + 1;
			G.head[y * MAX_MOD + x] = 1;
			G.uses[b]++;
		}
		ok = 1;
	}
	return ok;
}

// ------------------------------------------------------------------ точка входа

// Первый террейн нужного вида (1 — корабль X-COM, 2 — НЛО): пока миссии нет, отладка
// берёт первый попавшийся.
uint16_t mapgen_find_kind(uint8_t kind) __banked
{
	res_t r;
	const uint8_t want = kind;   // сравнение байта с параметром SDCC 4.5 портит (CLAUDE.md)
	if (!res_find(RES_TERRAINS, &r)) return 0xFFFF;
	uint16_t n = far_word(r.phys);
	for (uint16_t i = 0; i < n; i++) {
		far_t p = r.phys + far_word(r.phys + 2 + (uint32_t)i * 2);
		// сравнение делаем словами: байтовое SDCC 4.5 собирает так, что checkasm видит
		// свою известную ловушку (CLAUDE.md)
		uint16_t k = far_byte(p + 4);
		if (k == (uint16_t)want) return i;
	}
	return 0xFFFF;
}

void mapgen_set_extra(uint16_t craft, uint16_t ufo) __banked
{
	gen_craft = craft;
	gen_ufo = ufo;
}

uint8_t mapgen_run(uint16_t terrain, uint8_t mods, uint8_t levels) __banked
{
	int16_t script = -1;
	memset(&G, 0, sizeof G);
	G.mx = mods > MAX_MOD ? MAX_MOD : mods;
	G.my = G.mx;
	G.sz = levels;
	G.sx = (uint16_t)G.mx * MOD_W;
	G.sy = (uint16_t)G.my * MOD_W;
	if (!load_terrain(terrain, &script)) { dbg_puts("mapgen: no terrain\n"); return 0; }

	// карта: 4 байта на клетку
	uint32_t bytes = (uint32_t)G.sx * G.sy * G.sz * 4;
	G.pages = (uint8_t)((bytes + 16383) / 16384);
	G.page = pg_alloc(G.pages, 1);
	if (G.page == PG_NONE) { dbg_puts("mapgen: no pages\n"); return 0; }
	G.cells = FAR(G.page, 0);
	for (uint8_t i = 0; i < G.pages; i++) far_fill(FAR(G.page + i, 0), 0, 16384);

	// команды скрипта
	res_t r;
	if (script >= 0 && res_find(RES_MAPSCRIPTS, &r)) {
		uint16_t ns = far_word(r.phys);
		if ((uint16_t)script < ns) {
			far_t sp = r.phys + far_word(r.phys + 2 + (uint32_t)script * 2);
			uint16_t ncmd = far_word(sp);
			far_t p = sp + 2;
			for (uint16_t i = 0; i < ncmd; i++) {
				cmd_t c;
				uint16_t len;
				if (!read_cmd(p, &c, &len)) break;
				p += (uint32_t)len * 2;
				if (!cond_ok(&c)) { if (c.label && c.label < 16) G.label_ok[c.label] = 0; continue; }
				if (c.chances < 100 && (int16_t)(rng_next() % 100) >= c.chances) {
					if (c.label && c.label < 16) G.label_ok[c.label] = 0;
					continue;
				}
				uint8_t ok = 0;
				for (int16_t e = 0; e < c.executions; e++) {
					uint8_t x, y;
					int16_t b;
					switch (c.type) {
					case C_ADD_BLOCK:
						b = pick_block(c.ngroups ? c.groups[0] : G_DEFAULT, c.nblocks ? c.blocks : 0,
							       c.nblocks, c.nmaxuses ? c.maxuses : 0, (uint8_t)c.size);
						if (b >= 0 && find_place(c.rects, c.nrects, c.size ? (uint8_t)c.size : 1, &x, &y)) {
							put_block(x, y, (uint8_t)b);
							ok = 1;
						}
						break;
					case C_ADD_LINE:
						ok |= add_line(&c);
						break;
					case C_FILL_AREA:
						for (;;) {
							b = pick_block(c.ngroups ? c.groups[0] : G_DEFAULT, c.nblocks ? c.blocks : 0,
								       c.nblocks, c.nmaxuses ? c.maxuses : 0, 0);
							if (b < 0 || !find_place(c.rects, c.nrects, 1, &x, &y)) break;
							put_block(x, y, (uint8_t)b);
							ok = 1;
						}
						break;
					case C_CHECK_BLOCK:
						ok = 1;
						break;
					case C_REMOVE_BLOCK:
						for (uint8_t j = 0; j < G.my; j++)
							for (uint8_t k = 0; k < G.mx; k++)
								if (G.grid[j * MAX_MOD + k]) { G.grid[j * MAX_MOD + k] = 0; G.head[j * MAX_MOD + k] = 0; ok = 1; j = G.my; break; }
						break;
					case C_RESIZE:
						if (c.size > 0 && c.size <= MAX_MOD) {
							G.mx = G.my = (uint8_t)c.size;
							G.sx = (uint16_t)G.mx * MOD_W;
							G.sy = (uint16_t)G.my * MOD_W;
							ok = 1;
						}
						break;
					case C_ADD_CRAFT:
						// корабль X-COM: его террейн задаётся миссией, здесь — отладочный
						if (gen_craft != 0xFFFF) ok |= place_extra(gen_craft, c.rects, c.nrects, 1);
						break;
					case C_ADD_UFO:
						// НЛО: террейн назван в самой команде (UFOName), иначе берём заданный
						if (c.ufo >= 0) ok |= place_extra((uint16_t)c.ufo, c.rects, c.nrects, 0);
						else if (gen_ufo != 0xFFFF) ok |= place_extra(gen_ufo, c.rects, c.nrects, 0);
						break;
					case C_DIG_TUNNEL:
						if (c.tunnel >= 0) { dig_tunnel((uint8_t)c.tunnel, (uint8_t)c.direction); ok = 1; }
						break;
					default:
						break;
					}
				}
				if (c.label && c.label < 16) G.label_ok[c.label] = ok;
			}
		}
	}

	// незанятые модули добираем обычными блоками — карта должна быть заполнена целиком
	for (uint8_t y = 0; y < G.my; y++)
		for (uint8_t x = 0; x < G.mx; x++) {
			if (G.grid[y * MAX_MOD + x]) continue;
			int16_t b = pick_block(G_DEFAULT, 0, 0, 0, 0);
			if (b < 0) b = 0;
			put_block(x, y, (uint8_t)b);
		}

	// укладка выбранных блоков
	for (uint8_t y = 0; y < G.my; y++)
		for (uint8_t x = 0; x < G.mx; x++) {
			if (!G.head[y * MAX_MOD + x]) continue;
			lay_block(x, y, (uint8_t)(G.grid[y * MAX_MOD + x] - 1));
		}
	return 1;
}

// Сколько клеток непусто (пол есть) — грубая проверка, что укладка сработала
// Где встал корабль отряда (в клетках): 0 в ширине — корабля на карте нет
void mapgen_craft(uint8_t *x, uint8_t *y, uint8_t *w, uint8_t *l) __banked
{
	*x = G.craft_x; *y = G.craft_y; *w = G.craft_w; *l = G.craft_l;
}

uint16_t mapgen_filled(void) __banked
{
	uint16_t n = 0;
	for (uint8_t z = 0; z < G.sz; z++)
		for (uint16_t y = 0; y < G.sy; y++) {
			uint8_t row[80 * 4];
			uint16_t len = G.sx * 4;
			if (len > sizeof row) len = sizeof row;
			far_read(G.cells + ((uint32_t)(z * G.sy + y) * G.sx) * 4, row, len);
			for (uint16_t x = 0; x < len; x += 4)
				if (row[x] | row[x + 1] | row[x + 2] | row[x + 3]) n++;
		}
	return n;
}

uint16_t mapgen_sx(void) __banked { return G.sx; }
uint16_t mapgen_sy(void) __banked { return G.sy; }
uint8_t mapgen_sz(void) __banked { return G.sz; }
far_t mapgen_cells(void) __banked { return G.cells; }
// Наборы миссии: MCDSET (части) и TILESET (кадры) по порядку — из них экран боя собирает
// таблицу частей и загружает тайлсеты в страницы.
uint8_t mapgen_sets(uint16_t *set, uint16_t *tset, uint8_t *count) __banked
{
	for (uint8_t i = 0; i < G.nsets; i++) { set[i] = G.set[i]; tset[i] = G.tset[i]; }
	*count = G.nsets;
	return G.nsets;
}
