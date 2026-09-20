// Поиск пути по клеткам боя (16_battlescape_plan.md §5.4, порт Pathfinding оригинала —
// пока упрощённый). Вынесен в свой банк: экран боя (банк 28) уже занимал всю страницу.
//
// Волна идёт по восьми направлениям текущего этажа, цена шага одна и та же (в оригинале
// она зависит от пола, приседания и подъёма). Проходимость: нужен пол и пустой объект;
// стены между клетками, двери и уровни — следующий шаг.
//
// Рабочие массивы лежат в странице пула, которую даёт вызывающий: стоимость и направление
// прихода по байту на клетку (карта до 64x64), дальше очередь.
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

static uint8_t cell_ok(far_t cells, uint8_t sx, uint8_t sy, uint8_t x, uint8_t y, uint8_t z)
{
	if (x >= sx || y >= sy) return 0;
	uint8_t c[4];
	far_read(cells + ((uint32_t)(z * sy + y) * sx + x) * 4, c, 4);
	return c[0] && !c[3];
}

uint8_t pf_find(const pf_req_t *q, uint8_t *path, uint8_t maxlen) __banked
{
	if (q->page == PG_NONE || q->sx > PF_W || q->sy > PF_W) return 0;
	if (!cell_ok(q->cells, q->sx, q->sy, q->tx, q->ty, q->z)) return 0;
	uint8_t old = pg_map3(q->page);
	uint8_t *pf = (uint8_t *)0xC000;
	uint16_t *queue = (uint16_t *)(pf + 8192);
	memset(pf, 0xFF, 4096);
	uint16_t head = 0, tail = 0;
	PF_COST(q->fx, q->fy) = 0;
	queue[tail++] = (uint16_t)q->fy * PF_W + q->fx;
	uint8_t found = 0;
	while (head != tail && !found) {
		uint16_t cur = queue[head++];
		if (head >= PF_QMAX) head = 0;
		uint8_t cx = (uint8_t)(cur & (PF_W - 1)), cy = (uint8_t)(cur / PF_W);
		uint8_t cc = PF_COST(cx, cy);
		if (cc + q->step > q->budget) continue;
		for (uint8_t d = 0; d < 8; d++) {
			int16_t nx = (int16_t)cx + pf_dx[d], ny = (int16_t)cy + pf_dy[d];
			if (nx < 0 || ny < 0 || nx >= q->sx || ny >= q->sy) continue;
			if (PF_COST((uint8_t)nx, (uint8_t)ny) != 0xFF) continue;
			if (!cell_ok(q->cells, q->sx, q->sy, (uint8_t)nx, (uint8_t)ny, q->z)) continue;
			PF_COST((uint8_t)nx, (uint8_t)ny) = cc + q->step;
			PF_FROM((uint8_t)nx, (uint8_t)ny) = d;
			if ((uint8_t)nx == q->tx && (uint8_t)ny == q->ty) { found = 1; break; }
			queue[tail++] = (uint16_t)ny * PF_W + (uint16_t)nx;
			if (tail >= PF_QMAX) tail = 0;
			if (tail == head) { head = tail; found = 0; break; }   // очередь кончилась
		}
	}
	uint8_t n = 0;
	if (found) {
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
