// Бегство мирного жителя (банк 35): AIModule::setupEscape для FACTION_NEUTRAL (AIModule.cpp:220-223,
// :415-579). Вынесено из банка ИИ (36 полон).
//
// Мирный бежит, если его видит пришелец (getSpottingUnits: в секторе обзора и на линии взгляда).
// Оригинал перебирает случайные клетки в радиусе и оценивает их (дальше от врагов, не на виду); у
// нас — восемь клеток по направлениям на 6 клеток от мирного, лучшая — та, где ближайший живой
// пришелец дальше всего; путь — в пределах его времени и в малом окне, не дошёл — к ближайшей
// достижимой клетке (pathfind.h pf_near_once, pf_partial_once).
#include <stdint.h>
#include "far.h"
#include "units.h"
#include "vision.h"
#include "pathfind.h"
#include "bai.h"
#include "bescape.h"

#define STEP_TU 4

uint8_t ai_escape(const aictx_t *c, uint8_t a, uint8_t *path, uint8_t max) __banked
{
	static const int8_t ddx[8] = { 0, 1, 1, 1, 0, -1, -1, -1 }, ddy[8] = { -1, -1, 0, 1, 1, 1, 0, -1 };
	vu_t v[MAX_BUNITS];
	uint8_t hostile[MAX_BUNITS], spotted = 0;
	unit_t u, x;
	for (uint8_t i = 0; i < c->n; i++) {
		far_read(UNITS_FAR + (uint16_t)i * sizeof x, &x, sizeof x);
		v[i].x = x.x; v[i].y = x.y; v[i].z = x.z; v[i].dir = x.dir;
		v[i].flags = (x.alive ? VU_ALIVE : 0) | (x.size > 1 ? VU_BIG : 0) | VU_H(UNIT_H(&x));
		uint8_t f = x.faction;                   // сравнения — с копиями (SDCC 4.5)
		hostile[i] = x.alive && f == FACTION_HOSTILE;
		if (i == a) u = x;
	}
	for (uint8_t i = c->nsold; i < c->n && !spotted; i++)
		if (hostile[i] && vis_pair(v, i, a, 1)) spotted = 1;
	if (!spotted) return 0;
	int32_t best = -1;
	uint8_t bx = u.x, by = u.y;
	for (uint8_t k = 0; k < 8; k++) {
		int16_t tx = (int16_t)u.x + ddx[k] * 6, ty = (int16_t)u.y + ddy[k] * 6;
		if (tx < 0) tx = 0;
		if (ty < 0) ty = 0;
		if (tx >= c->sx) tx = c->sx - 1;
		if (ty >= c->sy) ty = c->sy - 1;
		uint16_t near = 0xFFFF;                  // ближайший пришелец к клетке (квадрат расстояния)
		for (uint8_t i = c->nsold; i < c->n; i++) {
			if (!hostile[i]) continue;
			int16_t dx = tx - v[i].x, dy = ty - v[i].y;
			uint16_t d = (uint16_t)(dx * dx + dy * dy);
			if (d < near) near = d;
		}
		if ((int32_t)near > best) { best = near; bx = (uint8_t)tx; by = (uint8_t)ty; }
	}
	pf_req_t r;
	r.cells = c->cells; r.sx = c->sx; r.sy = c->sy; r.z = u.z; r.sz = c->sz;
	r.fx = u.x; r.fy = u.y; r.tx = bx; r.ty = by;
	r.budget = u.tu; r.step = STEP_TU; r.page = c->page;
	pf_near_once();
	pf_partial_once();
	return pf_find(&r, path, max);
}
