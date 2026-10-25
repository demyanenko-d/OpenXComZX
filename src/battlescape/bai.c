// Ход пришельцев (банк 36): упрощённый AIModule оригинала (REF/OpenXcom/src/Battlescape/
// AIModule.cpp). Пришельцы ходят по очереди, по такту экрана боя:
//   - видит бойца X-COM (getVisibleUnits — тот же обзор, что у отряда, vision.c) и в руке
//     заряженное оружие — поворачивается и стреляет; способ — selectFireMethod (:1775-1834:
//     ближе 4 клеток — очередь, дальше 12 — прицельный, иначе навскидку, по времени), меткость —
//     getFiringAccuracy (firing · точность оружия, с колена · 1.15, двуручное при занятой второй
//     руке · 0.8), до трёх атак за ход;
//   - иначе патрулирует (setupPatrol): идёт к случайному узлу, связанному с его узлом (RMP),
//     или к любому узлу, сколько хватит времени. На виду у отряда — шагами с анимацией (bwalk.c),
//     вне вида — сразу по клетке; после каждого шага экран пересчитывает обзор отряда (AI_SEE).
// Чего нет: гранат, засад и отступлений, оценки укрытий, реакции отряда, дверей по пути,
// надписи «Hidden Movement».
#include <stdint.h>
#include <string.h>
#include "far.h"
#include "res.h"
#include "res_ids.h"
#include "rules.h"
#include "game.h"
#include "loader.h"
#include "units.h"
#include "bitems.h"
#include "bmenu.h"
#include "bview.h"
#include "vision.h"
#include "pathfind.h"
#include "doors.h"
#include "mapgen.h"
#include "bact.h"
#include "bai.h"

#define STEP_TU   4
#define PATH_MAX  48
#define ATK_MAX   3

// Память банка (tools/build.ps1 ram = #B000)
static uint8_t __at(0xB000) ai_path[PATH_MAX];
static uint8_t __at(0xB030) ai_a;        // кто ходит
static uint8_t __at(0xB031) ai_pn;       // шагов в пути
static uint8_t __at(0xB032) ai_pi;       //   и пройдено
static uint8_t __at(0xB033) ai_moved;    // уже шёл в этот ход
static uint8_t __at(0xB034) ai_atk;      // атак в этот ход
static uint8_t __at(0xB035) ai_walk;     // идёт шаг с анимацией
static uint8_t __at(0xB036) ai_wx;       // куда шаг
static uint8_t __at(0xB037) ai_wy;

static const int8_t dir_dx[8] = { 0, 1, 1, 1, 0, -1, -1, -1 };
static const int8_t dir_dy[8] = { -1, -1, 0, 1, 1, 1, 0, -1 };

static void u_get(uint8_t i, unit_t *u) { far_read(UNITS_FAR + (uint16_t)i * sizeof *u, u, sizeof *u); }
static void u_set(uint8_t i, const unit_t *u) { far_write(UNITS_FAR + (uint16_t)i * sizeof *u, u, sizeof *u); }

// Запись юнита для вида (как ru_put экрана боя) и перерисовка его клетки
static void show(const aictx_t *c, uint8_t i, const unit_t *u)
{
	runit_t r;
	r.x = u->x; r.y = u->y; r.z = u->z; r.dir = u->dir;
	r.flags = (u->alive && u->seen && (u->sheet || u->faction == FACTION_PLAYER) ? RU_ALIVE : 0)
		| (u->kneel ? RU_KNEEL : 0) | (u->size > 1 ? RU_BIG : 0);
	r.sheet = u->sheet;
	r.pad[0] = r.pad[1] = 0;
	far_write(FAR(c->page + 1, TL_RUNITS) + (uint16_t)i * sizeof r, &r, sizeof r);
	bv_sync(c->n);
}

static uint16_t isqrt(uint16_t v)
{
	uint16_t r = 0;
	for (uint16_t b = 0x80; b; b >>= 1) if ((r | b) * (r | b) <= v) r |= b;
	return r;
}

void ai_start(void) __banked
{
	ai_a = 0;
	ai_pn = ai_pi = ai_moved = ai_atk = ai_walk = 0;
}

// Следующий пришелец
static void next_alien(void)
{
	ai_a++;
	ai_pn = ai_pi = ai_moved = ai_atk = 0;
}

// Атака видимого бойца: 1 — выстрелил
static uint8_t attack(const aictx_t *c, unit_t *u)
{
	if (ai_atk >= ATK_MAX) return 0;
	bitem_t b, o;
	uint8_t right = 1;
	if (!bi_hand(c->page, ai_a, 1, &b)) { right = 0; if (!bi_hand(c->page, ai_a, 0, &b)) return 0; }
	rtab_t t;
	r_items_t w;
	if (!rtab_open(RES_RULE_ITEMS, &t) || b.item >= t.n) return 0;
	rtab_get(&t, b.item, &w);
	if (w.battle_type != 1) return 0;             // BT_FIREARM
	if (w.compatible_ammo.n && (b.ammo == 0xFFFF || !b.rounds)) return 0;
	// кого видит (тот же обзор, что у отряда: сектор и линия взгляда)
	vu_t v[MAX_BUNITS];
	unit_t x;
	for (uint8_t i = 0; i < c->n; i++) {
		u_get(i, &x);
		v[i].x = x.x; v[i].y = x.y; v[i].z = x.z; v[i].dir = x.dir;
		v[i].flags = (x.alive ? VU_ALIVE : 0) | (x.size > 1 ? VU_BIG : 0);
	}
	uint8_t list[8], nl = vis_list(v, c->nsold, 0, ai_a, list, sizeof list), tg = 0xFF;
	uint16_t best = 0xFFFF;
	for (uint8_t k = 0; k < nl; k++) {               // ближайший
		int16_t dx = (int16_t)v[list[k]].x - u->x, dy = (int16_t)v[list[k]].y - u->y;
		uint16_t d2 = (uint16_t)(dx * dx + dy * dy);
		if (d2 < best) { best = d2; tg = list[k]; }
	}
	if (tg == 0xFF || best > (uint16_t)w.max_range * w.max_range) return 0;
	uint16_t dist = isqrt(best);
	if (dist * dist < best) dist++;                   // TileEngine::distance — вверх
	// selectFireMethod: цена — процент от времени юнита (flatRate — как есть)
	uint8_t tu_a = w.tu_auto, tu_s = w.tu_snap, tu_m = w.tu_aimed;
	if (!(w.flags & ITEMS_F_FLAT_RATE)) {
		tu_a = (uint8_t)((uint16_t)u->tu_max * tu_a / 100);
		tu_s = (uint8_t)((uint16_t)u->tu_max * tu_s / 100);
		tu_m = (uint8_t)((uint16_t)u->tu_max * tu_m / 100);
	}
	uint8_t tu = u->tu, act = 0, cost = 0;
	if (dist < 4) {
		if (w.tu_auto && tu >= tu_a) { act = BA_AUTOSHOT; cost = tu_a; }
		else if (w.tu_snap && tu >= tu_s) { act = BA_SNAPSHOT; cost = tu_s; }
		else if (w.tu_aimed && tu >= tu_m) { act = BA_AIMEDSHOT; cost = tu_m; }
	} else {
		if (dist > 12 && w.tu_aimed && tu >= tu_m) { act = BA_AIMEDSHOT; cost = tu_m; }
		else if (dist > 12 && dist < 20 && w.tu_snap && tu >= tu_s) { act = BA_SNAPSHOT; cost = tu_s; }
		else if (w.tu_snap && tu >= tu_s) { act = BA_SNAPSHOT; cost = tu_s; }
		else if (w.tu_aimed && tu >= tu_m) { act = BA_AIMEDSHOT; cost = tu_m; }
		else if (w.tu_auto && tu >= tu_a) { act = BA_AUTOSHOT; cost = tu_a; }
	}
	if (!act) return 0;
	uint16_t acc = act == BA_AIMEDSHOT ? w.accuracy_aimed : act == BA_AUTOSHOT ? w.accuracy_auto : w.accuracy_snap;
	acc = (uint16_t)u->firing * acc / 100;
	if (u->kneel) acc = acc * 115 / 100;
	if ((w.flags & ITEMS_F_TWO_HANDED) && bi_hand(c->page, ai_a, !right, &o)) acc = acc * 80 / 100;
	u->dir = dir_to((int16_t)v[tg].x - u->x, (int16_t)v[tg].y - u->y);   // lookAt
	u->tu -= cost;
	u_set(ai_a, u);
	show(c, ai_a, u);
	if (u->seen) bv_cell(u->x, u->y, u->z);
	bashot_t q;
	q.page = c->page; q.cells = c->cells; q.sx = c->sx; q.sy = c->sy; q.sz = c->sz;
	q.shooter = ai_a; q.n = c->n; q.tx = v[tg].x; q.ty = v[tg].y; q.tz = v[tg].z;
	q.action = act; q.acc = acc > 255 ? 255 : (uint8_t)acc; q.weapon = b.item; q.ammo = b.ammo;
	uint8_t shots = act == BA_AUTOSHOT ? w.auto_shots : 1;
	for (uint8_t k = 0; k < shots; k++) {
		if (w.compatible_ammo.n) {
			bitem_t r;
			if (!bi_hand(c->page, ai_a, right, &r) || !r.rounds) break;
			bi_spend(c->page, ai_a, right);
		}
		ba_shot(&q);
	}
	ai_atk++;
	return 1;
}

// Куда патрулировать (setupPatrol): связанный с его узлом узел, иначе любой; путь — в ai_path
static void plan_patrol(const aictx_t *c, unit_t *u)
{
	far_t np;
	uint16_t nn = mapgen_nodes(&np), to = 0xFFFF;
	bnode_t b;
	if (!nn) return;
	if (u->node < nn) {
		far_read(np + (uint32_t)u->node * sizeof b, &b, sizeof b);
		uint16_t links[5];
		uint8_t nk = 0;
		for (uint8_t k = 0; k < 5; k++) if (b.link[k] < nn) links[nk++] = b.link[k];
		if (nk) to = links[rng_range(0, nk - 1)];
	}
	if (to == 0xFFFF) to = rng_range(0, nn - 1);
	far_read(np + (uint32_t)to * sizeof b, &b, sizeof b);
	if (b.x == 0xFF || b.z != u->z) return;
	pf_req_t r;
	r.cells = c->cells; r.sx = c->sx; r.sy = c->sy; r.z = u->z; r.sz = c->sz;
	r.fx = u->x; r.fy = u->y; r.tx = b.x; r.ty = b.y;
	r.budget = u->tu; r.step = STEP_TU; r.page = c->page;
	ai_pn = pf_find(&r, ai_path, PATH_MAX);
	ai_pi = 0;
	if (ai_pn) { u->node = to; u_set(ai_a, u); }
}

// Шаг по пути: на виду — с анимацией, иначе сразу. 0 — шагать некуда (путь кончился, время, занято)
static uint8_t step(const aictx_t *c, unit_t *u)
{
	if (ai_pi >= ai_pn) return 0;
	uint8_t d = ai_path[ai_pi];
	int16_t nx = (int16_t)u->x + dir_dx[d], ny = (int16_t)u->y + dir_dy[d];
	if (nx < 0 || ny < 0 || nx >= c->sx || ny >= c->sy) { ai_pn = 0; return 0; }
	dmap_t m;
	m.cells = c->cells; m.sx = c->sx; m.sy = c->sy; m.sz = c->sz; m.page = c->page;
	uint8_t cost = cell_cost(&m, (uint8_t)nx, (uint8_t)ny, u->z, d, STEP_TU);
	if (!cost || u->tu < cost) { ai_pn = 0; return 0; }
	for (uint8_t i = 0; i < c->n; i++) {             // клетка занята — стоп
		unit_t o;
		if (i == ai_a) continue;
		u_get(i, &o);
		if (o.alive && o.z == u->z && o.x == (uint8_t)nx && o.y == (uint8_t)ny) { ai_pn = 0; return 0; }
	}
	uint8_t nz = cell_step_z(&m, u->x, u->y, u->z, (uint8_t)nx, (uint8_t)ny);
	u->tu -= cost;
	u->dir = d;
	ai_pi++;
	if (u->seen && nz == u->z) {                     // на виду: фазами (bwalk.c), клетку сменит середина шага
		u_set(ai_a, u);
		show(c, ai_a, u);
		ai_wx = (uint8_t)nx; ai_wy = (uint8_t)ny;
		bv_walk(ai_a, u->x, u->y, (uint8_t)nx, (uint8_t)ny, u->z, d);
		ai_walk = 1;
		return 1;
	}
	uint8_t ox = u->x, oy = u->y, oz = u->z;
	u->x = (uint8_t)nx; u->y = (uint8_t)ny; u->z = nz;
	u_set(ai_a, u);
	show(c, ai_a, u);
	if (u->seen) { bv_cell(ox, oy, oz); bv_cell(u->x, u->y, u->z); }
	return 1;
}

uint8_t ai_tick(const aictx_t *c) __banked
{
	unit_t u;
	if (ai_walk) {                                   // шаг с анимацией
		uint8_t r = bv_walk_tick();
		if (r == 1) {                                // середина: числится в новой клетке
			u_get(ai_a, &u);
			u.x = ai_wx; u.y = ai_wy;
			u_set(ai_a, &u);
			show(c, ai_a, &u);
			return AI_SEE;
		}
		if (r != 2) return 0;
		ai_walk = 0;
		u_get(ai_a, &u);
		if (ai_pi < ai_pn && step(c, &u)) return AI_SEE;   // следующий шаг — без кадра стойки
		bv_walk_done();
		return AI_SEE;
	}
	while (ai_a < c->n) {
		u_get(ai_a, &u);
		if (!u.alive || u.faction != FACTION_HOSTILE || ai_a < c->nsold) { next_alien(); continue; }
		if (attack(c, &u)) return AI_SEE;            // стрелял — в следующем такте думает снова
		if (ai_pi < ai_pn) { if (step(c, &u)) return AI_SEE; }
		else if (!ai_moved) {
			ai_moved = 1;
			plan_patrol(c, &u);
			if (ai_pn && step(c, &u)) return AI_SEE;
		}
		next_alien();
		return 0;                                    // по пришельцу на такт
	}
	return AI_DONE;
}
