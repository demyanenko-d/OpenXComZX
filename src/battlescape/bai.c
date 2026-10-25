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
static uint8_t __at(0xB038) ai_aim;      // повернулся к цели — выстрел в следующем такте (карта видна)

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
	ai_pn = ai_pi = ai_moved = ai_atk = ai_walk = ai_aim = 0;
}

// Следующий пришелец
static void next_alien(void)
{
	ai_a++;
	ai_pn = ai_pi = ai_moved = ai_atk = 0;
}

// Оружие в руке юнита a (getMainHandWeapon): огнестрельное с патронами — правой, иначе левой
static uint8_t gun(const aictx_t *c, uint8_t a, bitem_t *b, uint8_t *right, r_items_t *w)
{
	rtab_t t;
	if (!rtab_open(RES_RULE_ITEMS, &t)) return 0;
	for (uint8_t h = 1; h != 0xFF; h--) {
		if (!bi_hand(c->page, a, h, b) || b->item >= t.n) continue;
		rtab_get(&t, b->item, w);
		if (w->battle_type != 1) continue;            // BT_FIREARM
		if (w->compatible_ammo.n && (b->ammo == 0xFFFF || !b->rounds)) continue;
		*right = h;
		return 1;
	}
	return 0;
}

// Цена действия (getActionTUs): процент от времени юнита, flatRate — как есть
static uint8_t tu_cost(const unit_t *u, const r_items_t *w, uint8_t v)
{
	return (w->flags & ITEMS_F_FLAT_RATE) ? v : (uint8_t)((uint16_t)u->tu_max * v / 100);
}

// Обзор: юниты копией на стеке (vision.c)
static void fill_vu(const aictx_t *c, vu_t *v)
{
	unit_t x;
	for (uint8_t i = 0; i < c->n; i++) {
		u_get(i, &x);
		v[i].x = x.x; v[i].y = x.y; v[i].z = x.z; v[i].dir = x.dir;
		v[i].flags = (x.alive ? VU_ALIVE : 0) | (x.size > 1 ? VU_BIG : 0);
	}
}

// Выстрел юнита a действием act по клетке t (getFiringAccuracy: firing · точность оружия, с колена
// · 1.15, двуручное при занятой второй руке · 0.8): время, патроны, пули (bact.c)
static void fire(const aictx_t *c, uint8_t a, unit_t *u, const vu_t *t, uint8_t act, uint8_t cost,
		 uint8_t right, const bitem_t *b, const r_items_t *w)
{
	bitem_t o;
	uint16_t acc = act == BA_AIMEDSHOT ? w->accuracy_aimed : act == BA_AUTOSHOT ? w->accuracy_auto : w->accuracy_snap;
	acc = (uint16_t)u->firing * acc / 100;
	if (u->kneel) acc = acc * 115 / 100;
	if ((w->flags & ITEMS_F_TWO_HANDED) && bi_hand(c->page, a, !right, &o)) acc = acc * 80 / 100;
	u->tu -= cost;
	u_set(a, u);
	bashot_t q;
	q.page = c->page; q.cells = c->cells; q.sx = c->sx; q.sy = c->sy; q.sz = c->sz;
	q.shooter = a; q.n = c->n; q.tx = t->x; q.ty = t->y; q.tz = t->z;
	q.action = act; q.acc = acc > 255 ? 255 : (uint8_t)acc; q.weapon = b->item; q.ammo = b->ammo;
	uint8_t shots = act == BA_AUTOSHOT ? w->auto_shots : 1;
	for (uint8_t k = 0; k < shots; k++) {
		if (w->compatible_ammo.n) {
			bitem_t r;
			if (!bi_hand(c->page, a, right, &r) || !r.rounds) break;
			bi_spend(c->page, a, right);
		}
		ba_shot(&q);
	}
}

// Атака видимого бойца: 1 — выстрелил
static uint8_t attack(const aictx_t *c, unit_t *u)
{
	if (ai_atk >= ATK_MAX) return 0;
	bitem_t b;
	uint8_t right;
	r_items_t w;
	if (!gun(c, ai_a, &b, &right, &w)) return 0;
	vu_t v[MAX_BUNITS];                               // кого видит (тот же обзор, что у отряда)
	fill_vu(c, v);
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
	uint8_t tu_a = tu_cost(u, &w, w.tu_auto), tu_s = tu_cost(u, &w, w.tu_snap), tu_m = tu_cost(u, &w, w.tu_aimed);
	uint8_t tu = u->tu, act = 0, cost = 0;           // selectFireMethod
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
	if (!ai_aim) {                                    // сперва поворот: экран покажет карту
		u->dir = dir_to((int16_t)v[tg].x - u->x, (int16_t)v[tg].y - u->y);   // lookAt
		u_set(ai_a, u);
		show(c, ai_a, u);
		if (u->seen) bv_cell(u->x, u->y, u->z);
		ai_aim = 1;
		return 1;
	}
	ai_aim = 0;
	fire(c, ai_a, u, &v[tg], act, cost, right, &b, &w);
	ai_atk++;
	return 1;
}

// Огонь в ответ (TileEngine::checkReactionFire, TileEngine.cpp:828-1090): на действие юнита m
// стреляют юниты другой стороны ближе 20 клеток, которые его видят (сектор и линия взгляда), с
// оружием навскидку в руке (determineReactionType: snap, патроны, дальность, времени больше цены);
// первым — с наибольшими очками реакции (getReactionScore: reactions · TU / TU max), и только если
// они не меньше очков самого m (getReactor); после выстрела выбор заново
uint8_t ai_react(const aictx_t *c, uint8_t m) __banked
{
	unit_t mv, r;
	vu_t v[MAX_BUNITS];
	uint8_t res = 0;
	for (uint8_t tries = 0; tries < 8; tries++) {
		u_get(m, &mv);
		if (!mv.alive || mv.faction == FACTION_NEUTRAL) break;
		fill_vu(c, v);
		uint8_t player = mv.faction == FACTION_PLAYER, br = 0xFF, bright = 0, bcost = 0;
		uint8_t lo = player ? c->nsold : 0, hi = player ? c->n : c->nsold;
		int16_t ms = mv.tu_max ? (int16_t)((uint16_t)mv.reactions * mv.tu / mv.tu_max) : 0, bs = -1;
		bitem_t bb;
		r_items_t bw;
		for (uint8_t i = lo; i < hi; i++) {
			u_get(i, &r);
			if (!r.alive || r.stun >= r.hp || r.faction == mv.faction || r.faction == FACTION_NEUTRAL) continue;
			int16_t dx = (int16_t)r.x - mv.x, dy = (int16_t)r.y - mv.y, dz = (int16_t)r.z - mv.z;
			uint16_t d2 = (uint16_t)(dx * dx + dy * dy + dz * dz);
			if (d2 > 400) continue;                       // MAX_VIEW_DISTANCE_SQR
			bitem_t b;
			uint8_t right;
			r_items_t w;
			if (!gun(c, i, &b, &right, &w) || !w.tu_snap || d2 >= (uint16_t)w.max_range * w.max_range) continue;
			uint8_t cost = tu_cost(&r, &w, w.tu_snap);
			if (!cost || r.tu <= cost) continue;
			// видит ли i юнита m: пришелец смотрит на бойцов [0, nsold), боец — на пришельцев [nsold, n)
			uint8_t list[16], nl = player ? vis_list(v, c->nsold, 0, i, list, sizeof list)
			                              : vis_list(v, c->n, c->nsold, i, list, sizeof list), seen = 0;
			for (uint8_t k = 0; k < nl; k++) if (list[k] == m) seen = 1;
			if (!seen) continue;
			int16_t sc = r.tu_max ? (int16_t)((uint16_t)r.reactions * r.tu / r.tu_max) : 0;
			if (sc > bs) { bs = sc; br = i; bright = right; bcost = cost; bb = b; bw = w; }
		}
		if (br == 0xFF || ms > bs) break;
		u_get(br, &r);
		r.dir = dir_to((int16_t)mv.x - r.x, (int16_t)mv.y - r.y);
		u_set(br, &r);
		show(c, br, &r);
		if (r.seen || r.faction == FACTION_PLAYER) bv_cell(r.x, r.y, r.z);
		fire(c, br, &r, &v[m], BA_SNAPSHOT, bcost, bright, &bb, &bw);
		res = AI_SEE | AI_HIT;
	}
	return res;
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
	uint8_t path[PATH_MAX];                     // буфер — на стеке: банку 30 память банка 36 не видна
	ai_pn = pf_find(&r, path, PATH_MAX);
	memcpy(ai_path, path, ai_pn);
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

static uint8_t ai_step(const aictx_t *c);

// Ходит невидимый отряду пришелец (и не целится): экрану — «Hidden Movement» (Map::draw)
static uint8_t hidden(const aictx_t *c)
{
	unit_t u;
	if (ai_a >= c->n || ai_aim) return 0;
	u_get(ai_a, &u);
	return u.seen ? 0 : AI_HIDDEN;
}

uint8_t ai_tick(const aictx_t *c) __banked
{
	uint8_t r = ai_step(c);
	return r & AI_DONE ? r : r | hidden(c);
}

static uint8_t ai_step(const aictx_t *c)
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
		uint8_t rf = ai_react(c, ai_a);              // шаг сделан — отряд может ответить огнём
		if (rf) ai_pn = 0;                           //   (UnitWalkBState: путь обрывается)
		u_get(ai_a, &u);
		if (u.alive && ai_pi < ai_pn && step(c, &u)) return AI_SEE;   // следующий шаг — без кадра стойки
		bv_walk_done();
		return AI_SEE | rf;
	}
	while (ai_a < c->n) {
		u_get(ai_a, &u);
		if (!u.alive || u.faction != FACTION_HOSTILE || ai_a < c->nsold) { next_alien(); continue; }
		if (attack(c, &u))                           // стрелял — в следующем такте думает снова
			return AI_SEE | AI_HIT | (ai_aim ? 0 : ai_react(c, ai_a));   // после выстрела — ответ отряда
		if (ai_pi >= ai_pn && !ai_moved) { ai_moved = 1; plan_patrol(c, &u); }
		if (ai_pi < ai_pn && step(c, &u)) {
			uint8_t rf = ai_walk ? 0 : ai_react(c, ai_a);   // шаг без анимации — сразу ответ
			if (rf) ai_pn = 0;
			return AI_SEE | rf;
		}
		next_alien();
		return 0;                                    // по пришельцу на такт
	}
	return AI_DONE;
}
