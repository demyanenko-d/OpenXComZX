// Поиск пути по клеткам боя (16_battlescape_plan.md §5.4, порт Pathfinding оригинала —
// пока без «больших стен», приседания и полёта). Вынесен в свой банк: экран боя (банк 28)
// уже занимал всю страницу.
//
// Что учитывается: пол под ногами, объект в клетке, стены между клетками, цена прохода
// (MCD.TU_Walk, 255 — не пройти) и переходы между этажами — вверх по ступеням
// (T_Level <= -16) и вниз, если под ногами нет пола. Двери проходимы: боец откроет их
// шагом, как в оригинале (у закрытой двери цена не 255, и isBlocked её не считает преградой).
//
// Волна — Беллман–Форд с очередью: цена шага разная, и найденное первым не всегда дешевле,
// поэтому клетка переоткрывается, если к ней нашли путь дешевле.
//
// Скорость (замеры 2026-09-21, карта 40x40): сперва волна читала карту дальним чтением на
// каждое соседство, и один щелчок думал 165 кадров (3.3 с). Что сделано:
//   - клетки этажа сворачиваются в слепок по байту на клетку (pathfind.h, PC_*), и карта
//     при этом читается рядами — одно far_read на ряд вместо чтения на клетку;
//   - волна идёт в окне вокруг начала и цели (PF_MARGIN); не дошли — второй заход без
//     границ. Почти все щелчки рядом с бойцом, а полная волна на Z80 стоит больше секунды.
// Клетки других этажей (ступени, падение) читаются по месту — это редкость.
//
// Карты шире 64 (суда TFTD — 70x70): таблицы волны — 64x64, волна идёт в окне 64x64 вокруг
// начала и цели (pf_ox, pf_oy — его угол на карте). Бюджет времени — байт, шаг стоит от 4:
// дальше 63 клеток путь всё равно не дотянется.
#include <stdint.h>
#include <string.h>
#include "pages.h"
#include "far.h"
#include "pathfind.h"

#define PF_W      64
#define PF_MARGIN 6            // запас окна поиска вокруг начала и цели (в клетках)
#define PF_COST(x, y)  (pf[(uint16_t)(y) * PF_W + (x)])
#define PF_FROM(x, y)  (pf[4096 + (uint16_t)(y) * PF_W + (x)])
#define PF_CELL(x, y)  (pf[PF_CACHE + (uint16_t)(y) * PF_W + (x)])

const int8_t pf_dx[8] = { 0, 1, 1, 1, 0, -1, -1, -1 };
const int8_t pf_dy[8] = { -1, -1, 0, 1, 1, 1, 0, -1 };

// Стены между клетками (Pathfinding::isBlocked, направления 0..7):
// для каждого направления — свои проверки (dx, dy, слот), слот 1 — западная стена клетки,
// 2 — северная. Смещения от клетки, из которой шагаем.
static const int8_t blk[] = {
	0, 0, 2,                                              // 0 север
	0, 0, 2,   1, -1, 1,   1, 0, 1,   1, 0, 2,            // 1 северо-восток
	1, 0, 1,                                              // 2 восток
	1, 0, 1,   0, 1, 2,    1, 1, 2,   1, 1, 1,            // 3 юго-восток
	0, 1, 2,                                              // 4 юг
	0, 0, 1,   0, 1, 1,    0, 1, 2,   -1, 1, 2,           // 5 юго-запад
	0, 0, 1,                                              // 6 запад
	0, 0, 1,   0, 0, 2,    -1, 0, 2,  0, -1, 1,           // 7 северо-запад
};
static const uint8_t blk_ofs[8] = { 0, 1, 5, 6, 10, 11, 15, 16 };   // в тройках
static const uint8_t blk_n[8] = { 1, 4, 1, 4, 1, 4, 1, 4 };

// Память банка (tools/build.ps1 ram = #BF00): Win1 полон
static far_t __at(0xBF00) pf_cells;
static uint8_t __at(0xBF04) pf_sx;           // окно волны (не больше PF_W)
static uint8_t __at(0xBF05) pf_sy;
static uint8_t __at(0xBF06) pf_z;
static uint8_t __at(0xBF07) pf_step;
static uint8_t __at(0xBF08) pf_mx;           // размер карты
static uint8_t __at(0xBF09) pf_my;
static uint8_t __at(0xBF0A) pf_ox;           // угол окна на ней
static uint8_t __at(0xBF0B) pf_oy;
static uint8_t __at(0xBF0C) pf_near;         // #A5 — следующий поиск только в малом окне (pf_near_once)
static uint8_t __at(0xBF0D) pf_part;         // #A5 — не дошли: путь к ближайшей к цели клетке

void pf_near_once(void) __banked
{
	pf_near = 0xA5;
}

void pf_partial_once(void) __banked
{
	pf_part = 0xA5;
}

// Клетка карты (координаты окна): номер считается в 16 битах и умножается на 4 сдвигом —
// 32-битное умножение здесь обходилось дороже самого чтения.
static void cell_at(uint8_t x, uint8_t y, uint8_t z, uint8_t *c)
{
	uint16_t idx = (uint16_t)(((uint16_t)z * pf_my + (uint8_t)(y + pf_oy)) * pf_mx + (uint8_t)(x + pf_ox));
	far_read(pf_cells + ((uint32_t)idx << 2), c, 4);
}

// Большие стены (MCD.Big_Wall у объекта; Pathfinding::isBlocked с O_WESTWALL / O_NORTHWALL /
// O_BIGWALL): стенка с запада или севера (4, 5, 9) — стена этой клетки, с востока или юга (6, 7,
// 8) — стена соседа (её ставит слепок, bw_next). Блок и диагональные стены (1–3) не пускают
// наискосок мимо себя; у нас — стенами со всех четырёх сторон клетки: в оригинале вдоль
// диагональной стены наискосок пройти можно (исключение BIGWALLNESW / NWSE), у нас — в обход
static uint8_t bw_own(const uint8_t *pf, const uint8_t *c)
{
	uint8_t w = c[3] ? pf[PF_BIGW + c[3]] : 0;
	if (!w || w > 9) return 0;
	if (w <= 3 || w == 9) return PC_WEST | PC_NORTH;
	return w == 4 ? PC_WEST : w == 5 ? PC_NORTH : 0;
}

// Стены, которые объект клетки ставит соседям: бит 0 — восточному (его западная), 1 — южному
static uint8_t bw_next(const uint8_t *pf, const uint8_t *c)
{
	uint8_t w = c[3] ? pf[PF_BIGW + c[3]] : 0;
	if (!w || w > 9) return 0;
	if (w <= 3 || w == 8) return 3;
	return w == 6 ? 1 : w == 7 ? 2 : 0;
}

// Четыре части клетки -> байт слепка: цена входа в старшей тетраде, флаги — в младшей
static uint8_t cell_pack(const uint8_t *pf, const uint8_t *c)
{
	uint8_t b = 0;
	if (c[1] && pf[PF_TU + c[1]] == 255) b |= PC_WEST;
	if (c[2] && pf[PF_TU + c[2]] == 255) b |= PC_NORTH;
	b |= bw_own(pf, c);
	int8_t lv = c[0] ? (int8_t)pf[PF_TLEVEL + c[0]] : 0;
	if (c[3]) {
		int8_t o = (int8_t)pf[PF_TLEVEL + c[3]];
		if (o < lv) lv = o;
	}
	if (lv <= -16) b |= PC_STAIRS;
	if (!c[0] || (pf[PF_FLAGS + c[0]] & 1)) b |= PC_NOFLOOR;
	uint8_t cost = 0;
	if (!(b & PC_NOFLOOR)) {                     // цена входа: пол плюс объект
		uint8_t t = pf[PF_TU + c[0]];
		if (t != 255) {
			cost = t;
			if (c[3]) {
				uint8_t o = pf[PF_TU + c[3]];
				if (o == 255) cost = 0;
				else cost += o;
			}
			if (cost && cost < pf_step) cost = pf_step;
			if (!cost && !c[3]) cost = pf_step;
			if (cost > 15) cost = 15;
		}
	}
	return (uint8_t)(b | (cost << 4));
}

// Слепок клетки: свой этаж — из слепка, чужой — читаем карту (ступени и падение)
static uint8_t cell_info(const uint8_t *pf, uint8_t x, uint8_t y, uint8_t z)
{
	if (z == pf_z) return PF_CELL(x, y);
	uint8_t c[4];
	cell_at(x, y, z, c);
	return cell_pack(pf, c);
}

// Стена на пути между клетками: 1 — не пройти
static uint8_t wall_blocked(const uint8_t *pf, uint8_t cx, uint8_t cy, uint8_t cz, uint8_t d)
{
	const int8_t *p = blk + (uint16_t)blk_ofs[d] * 3;
	for (uint8_t i = blk_n[d]; i; i--, p += 3) {
		int16_t x = (int16_t)cx + p[0], y = (int16_t)cy + p[1];
		if (x < 0 || y < 0 || x >= pf_sx || y >= pf_sy) return 1;
		uint8_t b = cell_info(pf, (uint8_t)x, (uint8_t)y, cz);
		if (b & (p[2] == 1 ? PC_WEST : PC_NORTH)) return 1;
	}
	return 0;
}

uint8_t pf_find(const pf_req_t *q, uint8_t *path, uint8_t maxlen) __banked
{
	uint8_t near = pf_near == 0xA5, part = pf_part == 0xA5;   // просьбы — на один поиск
	uint8_t pbd = 0xFF, pbx = 0, pby = 0;
	pf_near = pf_part = 0;
	if (q->page == PG_NONE || q->tx >= q->sx || q->ty >= q->sy) return 0;
	// Окно волны на карте: вся карта, если влезает, иначе 64 клетки вокруг середины пути
	pf_mx = q->sx; pf_my = q->sy;
	pf_ox = pf_oy = 0;
	if (q->sx > PF_W) {
		uint8_t m = (uint8_t)(((uint16_t)q->fx + q->tx) >> 1);
		pf_ox = m > 31 ? (uint8_t)(m - 31) : 0;
		if (pf_ox > q->sx - PF_W) pf_ox = (uint8_t)(q->sx - PF_W);
	}
	if (q->sy > PF_W) {
		uint8_t m = (uint8_t)(((uint16_t)q->fy + q->ty) >> 1);
		pf_oy = m > 31 ? (uint8_t)(m - 31) : 0;
		if (pf_oy > q->sy - PF_W) pf_oy = (uint8_t)(q->sy - PF_W);
	}
	uint8_t fx = (uint8_t)(q->fx - pf_ox), fy = (uint8_t)(q->fy - pf_oy);   // в окне
	uint8_t tx = (uint8_t)(q->tx - pf_ox), ty = (uint8_t)(q->ty - pf_oy);
	if (fx >= PF_W || fy >= PF_W || tx >= PF_W || ty >= PF_W) return 0;   // дальше 63 — не дойти
	pf_cells = q->cells; pf_z = q->z;
	pf_sx = q->sx > PF_W ? PF_W : q->sx; pf_sy = q->sy > PF_W ? PF_W : q->sy;
	pf_step = q->step;
	uint8_t old = pg_map3(q->page);
	uint8_t *pf = (uint8_t *)0xC000;
	uint16_t *queue = (uint16_t *)(pf + PF_QUEUE);
	// Окно поиска: вокруг начала и цели с запасом. Не дошли — второй заход по всей карте.
	uint8_t bx0 = fx < tx ? fx : tx, bx1 = fx > tx ? fx : tx;
	uint8_t by0 = fy < ty ? fy : ty, by1 = fy > ty ? fy : ty;
	bx0 = bx0 > PF_MARGIN ? (uint8_t)(bx0 - PF_MARGIN) : 0;
	by0 = by0 > PF_MARGIN ? (uint8_t)(by0 - PF_MARGIN) : 0;
	bx1 = (uint8_t)(bx1 + PF_MARGIN);
	by1 = (uint8_t)(by1 + PF_MARGIN);
	if (bx1 >= pf_sx) bx1 = (uint8_t)(pf_sx - 1);
	if (by1 >= pf_sy) by1 = (uint8_t)(pf_sy - 1);
	for (uint8_t pass = 0; pass < 2; pass++) {
		if (pass) {                  // второй заход — всё окно
			bx0 = 0; by0 = 0;
			bx1 = (uint8_t)(pf_sx - 1); by1 = (uint8_t)(pf_sy - 1);
		}
		{                            // слепок клеток окна: карта читается рядами
			uint8_t rowbuf[PF_W * 4], smark[PF_W];
			uint16_t rb = (uint16_t)(bx1 - bx0 + 1) * 4;
			memset(smark, 0, sizeof smark);
			far_t ra = pf_cells + ((uint32_t)(((uint16_t)q->z * pf_my + (uint8_t)(by0 + pf_oy)) * pf_mx
				+ (uint8_t)(bx0 + pf_ox)) << 2);
			uint16_t step = (uint16_t)pf_mx * 4;
			for (uint8_t y = by0; y <= by1; y++, ra += step) {
				far_read(ra, rowbuf, rb);
				const uint8_t *c = rowbuf;
				uint8_t em = 0;                  // западная стена от соседа слева (большая стена)
				for (uint8_t x = bx0; x <= bx1; x++, c += 4) {
					uint8_t v = cell_pack(pf, c), nx = bw_next(pf, c);
					if (em) v |= PC_WEST;
					if (smark[x & (PF_W - 1)]) v |= PC_NORTH;
					em = nx & 1;
					smark[x & (PF_W - 1)] = nx & 2;
					PF_CELL(x, y) = v;
				}
			}
		}
		memset(pf, 0xFF, 4096);
		pbx = fx; pby = fy;                          // ближе, чем стоит, — иначе пути нет
		pbd = fx > tx ? fx - tx : tx - fx;
		if ((uint8_t)(fy > ty ? fy - ty : ty - fy) > pbd) pbd = fy > ty ? fy - ty : ty - fy;
		uint16_t head = 0, tail = 0;
		PF_COST(fx, fy) = 0;
		PF_FROM(fx, fy) = (uint8_t)(q->z << 3);   // в старших битах — этаж клетки
		queue[tail++] = (uint16_t)fy * PF_W + fx;
		while (head != tail) {
			uint16_t cur = queue[head++];
			if (head >= PF_QMAX) head = 0;
			uint8_t cx = (uint8_t)(cur & (PF_W - 1)), cy = (uint8_t)(cur / PF_W);
			uint8_t cc = PF_COST(cx, cy);
			uint8_t cz = (uint8_t)(PF_FROM(cx, cy) >> 3);
			uint8_t stairs = cell_info(pf, cx, cy, cz) & PC_STAIRS;
			for (uint8_t d = 0; d < 8; d++) {
				int16_t nx = (int16_t)cx + pf_dx[d], ny = (int16_t)cy + pf_dy[d];
				if (nx < bx0 || ny < by0 || nx > bx1 || ny > by1) continue;
				// Этаж после шага (Pathfinding::getTUCost): вверх по ступеням,
				// вниз пока под ногами нет пола
				uint8_t nz = cz;
				uint8_t nb = cell_info(pf, (uint8_t)nx, (uint8_t)ny, nz);
				if (stairs && nz + 1 < q->sz) {
					uint8_t up = cell_info(pf, (uint8_t)nx, (uint8_t)ny, (uint8_t)(nz + 1));
					if (!(up & PC_NOFLOOR)) { nz++; nb = up; }
				}
				while (nz && (nb & PC_NOFLOOR))
					nb = cell_info(pf, (uint8_t)nx, (uint8_t)ny, --nz);
				uint8_t cost = (uint8_t)(nb >> 4);
				if (!cost) continue;
				if (d & 1) cost += cost >> 1;   // наискось дороже в полтора раза
				uint16_t nc = (uint16_t)cc + cost;
				if (nc > q->budget || nc >= PF_COST((uint8_t)nx, (uint8_t)ny)) continue;
				if (wall_blocked(pf, cx, cy, cz, d)) continue;
				PF_COST((uint8_t)nx, (uint8_t)ny) = (uint8_t)nc;
				PF_FROM((uint8_t)nx, (uint8_t)ny) = (uint8_t)(d | (nz << 3));
				if (part) {                  // достижимая клетка, ближайшая к цели (pf_partial_once)
					uint8_t ax = (uint8_t)nx, ay = (uint8_t)ny;
					uint8_t ex = ax > tx ? ax - tx : tx - ax, ey = ay > ty ? ay - ty : ty - ay;
					if (ey > ex) ex = ey;
					if (ex < pbd) { pbd = ex; pbx = ax; pby = ay; }
				}
				queue[tail++] = (uint16_t)ny * PF_W + (uint16_t)nx;
				if (tail >= PF_QMAX) tail = 0;
				if (tail == head) { head = tail; break; }   // очередь кончилась
			}
		}
		if (PF_COST(tx, ty) != 0xFF) break;           // дошли
		if (!bx0 && !by0 && bx1 == pf_sx - 1 && by1 == pf_sy - 1) break;   // и так всё окно
		if (near) break;                              // просили без второго захода (бегство, патруль)
	}
	if (part && PF_COST(tx, ty) == 0xFF) { tx = pbx; ty = pby; }   // не дошли: к ближайшей к цели
	// Путь — от цели назад: длина, затем первые maxlen шагов (длиннее — обрезается: ИИ идёт,
	// пока хватает времени, как по полному пути Pathfinding::calculate)
	uint8_t n = 0;
	if (PF_COST(tx, ty) != 0xFF) {
		uint8_t x = tx, y = ty, len = 0;
		while ((x != fx || y != fy) && len < 255) {
			uint8_t d = PF_FROM(x, y) & 7;
			x = (uint8_t)((int16_t)x - pf_dx[d]);
			y = (uint8_t)((int16_t)y - pf_dy[d]);
			len++;
		}
		if (x == fx && y == fy) {
			n = len < maxlen ? len : maxlen;
			x = tx; y = ty;
			for (uint8_t i = len; i; i--) {
				uint8_t d = PF_FROM(x, y) & 7;
				if (i <= n) path[i - 1] = d;
				x = (uint8_t)((int16_t)x - pf_dx[d]);
				y = (uint8_t)((int16_t)y - pf_dy[d]);
			}
		}
	}
	pg_map3(old);
	return n;
}
