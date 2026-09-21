// Поиск пути по клеткам боя (16_battlescape_plan.md §5.4, порт Pathfinding оригинала —
// пока без подъёмов, приседания и «больших стен»). Вынесен в свой банк: экран боя (банк 28)
// уже занимал всю страницу.
//
// Что учитывается: пол под ногами, объект в клетке, стены между клетками и цена прохода
// (MCD.TU_Walk, 255 — не пройти). Двери проходимы: боец откроет их шагом, как в оригинале
// (Pathfinding::isBlocked закрытую дверь не считает преградой — у неё цена не 255).
//
// Волна — Беллман–Форд с очередью: цена шага теперь разная, и найденное первым не всегда
// дешевле, поэтому клетка переоткрывается, если к ней нашли путь дешевле.
//
// Рабочие массивы лежат в странице пула, которую даёт вызывающий: стоимость и направление
// прихода по байту на клетку (карта до 64x64), дальше очередь и таблицы частей (pathfind.h).
#include <stdint.h>
#include <string.h>
#include "pages.h"
#include "far.h"
#include "pathfind.h"

#define PF_W     64
#define PF_COST(x, y)  (pf[(uint16_t)(y) * PF_W + (x)])
#define PF_FROM(x, y)  (pf[4096 + (uint16_t)(y) * PF_W + (x)])
#define PF_QMAX  2048

const int8_t pf_dx[8] = { 0, 1, 1, 1, 0, -1, -1, -1 };
const int8_t pf_dy[8] = { -1, -1, 0, 1, 1, 1, 0, -1 };

// Стены между клетками (Pathfinding::isBlocked, направления 0..7, без «больших стен»):
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

static far_t pf_cells;
static uint8_t pf_sx, pf_sy, pf_z;

static void cell_at(uint8_t x, uint8_t y, uint8_t *c)
{
	far_read(pf_cells + ((uint32_t)(pf_z * pf_sy + y) * pf_sx + x) * 4, c, 4);
}

// Цена входа в клетку: пол плюс объект (Pathfinding::getTUCost). 0 — клетка непроходима
// (нет пола или что-то стоит намертво), иначе единицы времени; step — цена по умолчанию,
// когда у частей она не задана.
static uint8_t enter_cost(const uint8_t *pf, const uint8_t *c, uint8_t step)
{
	if (!c[0]) return 0;                          // без пола не пройти (полёта пока нет)
	uint8_t t = pf[PF_TU + c[0]];
	if (t == 255) return 0;
	uint8_t cost = t;
	if (c[3]) {
		uint8_t o = pf[PF_TU + c[3]];
		if (o == 255) return 0;
		cost += o;
	}
	if (!cost) cost = step;
	return cost ? cost : 1;
}

// Стена на пути между клетками: 1 — не пройти
static uint8_t wall_blocked(const uint8_t *pf, uint8_t cx, uint8_t cy, uint8_t d)
{
	const int8_t *p = blk + (uint16_t)blk_ofs[d] * 3;
	for (uint8_t i = blk_n[d]; i; i--, p += 3) {
		int16_t x = (int16_t)cx + p[0], y = (int16_t)cy + p[1];
		if (x < 0 || y < 0 || x >= pf_sx || y >= pf_sy) return 1;
		uint8_t c[4];
		cell_at((uint8_t)x, (uint8_t)y, c);
		uint8_t t = c[(uint8_t)p[2]];
		if (t && pf[PF_TU + t] == 255) return 1;
	}
	return 0;
}

uint8_t pf_find(const pf_req_t *q, uint8_t *path, uint8_t maxlen) __banked
{
	if (q->page == PG_NONE || q->sx > PF_W || q->sy > PF_W) return 0;
	pf_cells = q->cells; pf_sx = q->sx; pf_sy = q->sy; pf_z = q->z;
	if (q->tx >= q->sx || q->ty >= q->sy) return 0;
	uint8_t old = pg_map3(q->page);
	uint8_t *pf = (uint8_t *)0xC000;
	uint16_t *queue = (uint16_t *)(pf + 8192);
	uint8_t c[4];
	cell_at(q->tx, q->ty, c);
	if (!enter_cost(pf, c, q->step)) { pg_map3(old); return 0; }
	memset(pf, 0xFF, 4096);
	uint16_t head = 0, tail = 0;
	PF_COST(q->fx, q->fy) = 0;
	queue[tail++] = (uint16_t)q->fy * PF_W + q->fx;
	while (head != tail) {
		uint16_t cur = queue[head++];
		if (head >= PF_QMAX) head = 0;
		uint8_t cx = (uint8_t)(cur & (PF_W - 1)), cy = (uint8_t)(cur / PF_W);
		uint8_t cc = PF_COST(cx, cy);
		for (uint8_t d = 0; d < 8; d++) {
			int16_t nx = (int16_t)cx + pf_dx[d], ny = (int16_t)cy + pf_dy[d];
			if (nx < 0 || ny < 0 || nx >= q->sx || ny >= q->sy) continue;
			cell_at((uint8_t)nx, (uint8_t)ny, c);
			uint8_t cost = enter_cost(pf, c, q->step);
			if (!cost) continue;
			if (d & 1) cost += cost >> 1;         // наискось дороже в полтора раза
			uint16_t nc = (uint16_t)cc + cost;
			if (nc > q->budget || nc >= PF_COST((uint8_t)nx, (uint8_t)ny)) continue;
			if (wall_blocked(pf, cx, cy, d)) continue;
			PF_COST((uint8_t)nx, (uint8_t)ny) = (uint8_t)nc;
			PF_FROM((uint8_t)nx, (uint8_t)ny) = d;
			queue[tail++] = (uint16_t)ny * PF_W + (uint16_t)nx;
			if (tail >= PF_QMAX) tail = 0;
			if (tail == head) { head = tail; break; }   // очередь кончилась — что нашли, то нашли
		}
	}
	uint8_t n = 0;
	if (PF_COST(q->tx, q->ty) != 0xFF) {
		uint8_t x = q->tx, y = q->ty;
		uint8_t buf[32];
		while ((x != q->fx || y != q->fy) && n < 32 && n < maxlen) {
			uint8_t d = PF_FROM(x, y);
			buf[n++] = d;
			x = (uint8_t)((int16_t)x - pf_dx[d]);
			y = (uint8_t)((int16_t)y - pf_dy[d]);
		}
		if (x == q->fx && y == q->fy)
			for (uint8_t i = 0; i < n; i++) path[i] = buf[n - 1 - i];
		else n = 0;
	}
	pg_map3(old);
	return n;
}
