// Ход пришельцев (банк 36): упрощённый AIModule оригинала (REF/OpenXcom/src/Battlescape/
// AIModule.cpp). Пришельцы ходят по очереди, по такту экрана боя:
//   - видит бойца X-COM (getVisibleUnits — тот же обзор, что у отряда, vision.c) и в руке
//     заряженное оружие — поворачивается и стреляет; способ — selectFireMethod (:1775-1834:
//     ближе 4 клеток — очередь, дальше 12 — прицельный, иначе навскидку, по времени), меткость —
//     getFiringAccuracy (firing · точность оружия, с колена · 1.15, двуручное при занятой второй
//     руке · 0.8), до трёх атак за ход;
//   - иначе патрулирует (setupPatrol): идёт к случайному узлу, связанному с его узлом (RMP),
//     или к любому узлу, сколько хватит времени. На виду у отряда — шагами с анимацией (bwalk.c),
//     вне вида — сразу по клетке; после каждого шага экран пересчитывает обзор отряда (AI_SEE);
//   - перед действием, после шага и выстрела пришелец, которого отряд не видит, снова невидим
//     (handleAI:204, UnitWalkBState.cpp:197-201) — стирается с карты, дальше «Hidden Movement».
// Чего нет: гранат, засад и отступлений, оценки укрытий, реакции отряда, дверей по пути,
// надписи «Hidden Movement».
#include <stdint.h>
#include <stddef.h>
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

// Память банка (tools/build.ps1 ram = #BF00): код — до #BEFF
static uint8_t __at(0xBF00) ai_path[PATH_MAX];
static uint8_t __at(0xBF30) ai_a;        // кто ходит
static uint8_t __at(0xBF31) ai_pn;       // шагов в пути
static uint8_t __at(0xBF32) ai_pi;       //   и пройдено
static uint8_t __at(0xBF33) ai_moved;    // уже шёл в этот ход
static uint8_t __at(0xBF34) ai_atk;      // атак в этот ход
static uint8_t __at(0xBF35) ai_walk;     // идёт шаг с анимацией
static uint8_t __at(0xBF36) ai_wx;       // куда шаг
static uint8_t __at(0xBF37) ai_wy;
static uint8_t __at(0xBF38) ai_aim;      // повернулся к цели — выстрел в следующем такте (карта видна)
static uint8_t __at(0xBF39) ai_door;     // едут створки НЛО, открытые пришельцем (doors_anim)
static uint8_t __at(0xBF3A) ai_dtm;      //   кадр (frames), когда сменили их кадр
// Сценарии: путь патруля задан извне — ED:3F3B число шагов (до 4), ED:3F3C.. направления; берётся
// вместо случайного узла первым же патрулём (tests/bat_aidoor.oxs) и сбрасывается
static uint8_t __at(0xBF3B) ai_dbgn;
#define AI_DBGP ((uint8_t *)0xBF3C)
extern volatile uint16_t frames;         // crt0.s
// #BF40..#BF67 — ai_tss[MAX_BUNITS], turnsSinceSpotted (bai.h)
#define CHEAT_TURN 20                     // AlienDeployment::_cheatTurn по умолчанию

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

// Видимый пришелец перешёл в клетку: камера на него, если он за краем экрана, и на его этаж
// (UnitWalkBState.cpp:169-172)
static void follow(const unit_t *u)
{
	if (!u->seen) return;
	if (!bv_on_screen(u->x, u->y, u->z, u->size > 1)) bv_center(u->x, u->y);
	if (u->z != bv_level()) bv_set_level(u->z, 0);
}

static uint16_t isqrt(uint16_t v)
{
	uint16_t r = 0;
	for (uint16_t b = 0x80; b; b >>= 1) if ((r | b) * (r | b) <= v) r |= b;
	return r;
}

static void fill_vu(const aictx_t *c, vu_t *v);

// Юнит i — пришелец (живость — в vu_t)
static uint8_t hostile(uint8_t i)
{
	return far_byte(UNITS_FAR + (uint16_t)i * sizeof(unit_t) + offsetof(unit_t, faction)) == FACTION_HOSTILE;
}

// Обзор пришельца a (calculateFOV): всех не-пришельцев в его секторе — «видели только что»
static void spot_by(const aictx_t *c, vu_t *v, uint8_t a)
{
	if (!(v[a].flags & VU_ALIVE) || !hostile(a)) return;
	for (uint8_t i = 0; i < c->n; i++)
		if ((v[i].flags & VU_ALIVE) && ai_tss[i] && !hostile(i) && vis_pair(v, a, i, 1)) ai_tss[i] = 0;
}

static void spot_all(const aictx_t *c)
{
	vu_t v[MAX_BUNITS];
	fill_vu(c, v);
	for (uint8_t a = c->nsold; a < c->n; a++) spot_by(c, v, a);
}

// Пришелец сменил клетку (UnitWalkBState.cpp:201 — calculateFOV): кого он теперь видит
static void moved(const aictx_t *c)
{
	vu_t v[MAX_BUNITS];
	fill_vu(c, v);
	spot_by(c, v, ai_a);
}

// Кого каждый боец уже видел в этот ход (BattleUnit::_unitsSpottedThisTurn): по биту на юнита
#define AI_SPOT ((uint8_t *)0xBF68)      // MAX_UNITS x 5 байт, до #BFA3
#define SPOT_W  ((MAX_BUNITS + 7) / 8)

uint8_t ai_spotted(uint8_t s, const uint8_t *list, uint8_t n) __banked
{
	uint8_t r = 0;
	if (s >= MAX_UNITS) return 0;
	uint8_t *m = AI_SPOT + (uint16_t)s * SPOT_W;
	for (uint8_t k = 0; k < n; k++) {
		uint8_t i = list[k], b = (uint8_t)(1 << (i & 7));
		if (!(m[i >> 3] & b)) { m[i >> 3] |= b; r = 1; }
	}
	return r;
}

// Начало хода X-COM (prepareNewTurn + recalculateFOV): у бойцов в списке — кого они видят сейчас
static void spot_reset(const aictx_t *c)
{
	vu_t v[MAX_BUNITS];
	uint8_t list[16];
	memset(AI_SPOT, 0, MAX_UNITS * SPOT_W);
	fill_vu(c, v);
	for (uint8_t s = 0; s < c->nsold && s < MAX_UNITS; s++)
		ai_spotted(s, list, vis_list(v, c->n, c->nsold, s, list, sizeof list));
}

void ai_tss_init(const aictx_t *c) __banked
{
	memset(ai_tss, 255, MAX_BUNITS);
	memset(ai_hitby, 0, MAX_BUNITS * 2);
	ai_dbgn = 0;
	for (uint8_t i = c->nsold; i < c->n; i++) {      // целей патруля ещё нет (dset/ilevel — снаряжению)
		unit_t x;
		u_get(i, &x);
		U_TONODE(&x) = 0xFFFF;
		u_set(i, &x);
	}
	spot_reset(c);
}

void ai_round(const aictx_t *c, uint8_t turn) __banked
{
	uint8_t live = 0;
	unit_t x;
	for (uint8_t i = c->nsold; i < c->n; i++) { u_get(i, &x); if (x.alive && x.faction == FACTION_HOSTILE) live++; }
	uint8_t cheat = turn > CHEAT_TURN || (turn > CHEAT_TURN / 2 && live <= 2);
	for (uint8_t i = 0; i < c->n; i++) {
		if (ai_tss[i] < 255) ai_tss[i]++;
		if (cheat && i < c->nsold) { u_get(i, &x); if (x.alive) ai_tss[i] = 0; }
	}
	spot_all(c);
	spot_reset(c);
	memset(ai_hitby, 0, MAX_BUNITS * 2);             // AIModule::reset — пришельцы забывают, кто в них попал
}

void ai_spot(const aictx_t *c, uint8_t m) __banked
{
	vu_t v[MAX_BUNITS];
	if (!ai_tss[m]) return;
	fill_vu(c, v);
	for (uint8_t a = c->nsold; a < c->n && ai_tss[m]; a++)
		if ((v[a].flags & VU_ALIVE) && hostile(a) && vis_pair(v, a, m, 1)) ai_tss[m] = 0;
}

void ai_start(const aictx_t *c) __banked
{
	spot_all(c);                                     // recalculateFOV в endTurn
	ai_a = 0;
	ai_pn = ai_pi = ai_moved = ai_atk = ai_walk = ai_aim = ai_door = 0;
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
		v[i].flags = (x.alive ? VU_ALIVE : 0) | (x.size > 1 ? VU_BIG : 0) | VU_H(UNIT_H(&x));
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

// Граната (AIModule::grenadeAction, explosiveEfficacy :1522-1614, :1839-1874): с хода 3
// (turnAIUseGrenade), времени хватает (4 — достать, взвод 50 % и бросок 25 % времени), выгода:
// отчаяние (мораль, раны) + сложность/2, по +1 за каждого известного врага в радиусе на том же этаже
// (цель — тоже), по −2 за своего, −4, если сам в радиусе; врагов меньше двух — только в отчаянии.
// Упрощено: без трассы от взрыва до каждого и без узла лучшей выгоды (getNodeOfBestEfficacy).
// Бросок — сразу взведённой (bact.c throw_it), взрыв — в конце хода. 1 — бросил
static uint8_t gren(const aictx_t *c, unit_t *u, const vu_t *v, uint8_t tg, uint8_t intel)
{
	if (c->turn < 3) return 0;
	uint8_t cost = (uint8_t)(4 + (uint16_t)u->tu_max * 75 / 100);
	if (u->tu < cost) return 0;
	uint16_t gi = bi_grenade(c->page, ai_a, 0);
	rtab_t t;
	r_items_t w;
	if (gi == 0xFFFF || !rtab_open(RES_RULE_ITEMS, &t) || gi >= t.n) return 0;
	rtab_get(&t, gi, &w);
	int16_t rad = w.blast_radius;
	if (rad < 0) rad = w.damage_type == 2 ? w.power / 30 + 1 : (w.damage_type == 3 || w.damage_type == 6 || w.damage_type == 9) ? w.power / 20 : 0;
	if (rad > 11) rad = 11;
	const vu_t *p = &v[tg];
	int8_t desp = (int8_t)((100 - u->mor) / 10);
	if (u->hp_max - u->hp > u->hp_max / 3 * 2) desp += 3;
	int8_t eff = (int8_t)(desp + ST->difficulty / 2), en = 1;
	eff++;                                           // цель
	for (uint8_t i = 0; i < c->n; i++) {
		if (i == tg || !(v[i].flags & VU_ALIVE) || v[i].z != p->z) continue;
		int16_t dx = (int16_t)v[i].x - p->x, dy = (int16_t)v[i].y - p->y;
		if ((uint16_t)(dx * dx + dy * dy) > (uint16_t)(rad * rad)) continue;
		if (i == ai_a) { eff -= 4; continue; }       // сам под взрывом
		if (hostile(i)) eff -= 2;
		else if (ai_tss[i] <= intel) { en++; eff++; }
	}
	if ((desp < 6 && en < 2) || (eff <= 0 && en < 10)) return 0;
	uint8_t hand;
	if (bi_grenade(c->page, ai_a, &hand) == 0xFFFF) return 0;
	bi_hands(c->page, c->n);
	bafire_t q;
	q.s.page = c->page; q.s.cells = c->cells; q.s.sx = c->sx; q.s.sy = c->sy; q.s.sz = c->sz;
	q.s.shooter = ai_a; q.s.n = c->n; q.s.tx = p->x; q.s.ty = p->y; q.s.tz = p->z;
	q.s.action = BA_THROW; q.s.acc = u->throwing;
	q.hand = hand; q.tu = cost;
	if (ba_fire(&q)) return 0;                       // далеко или некуда (validateThrow)
	u_get(ai_a, u);
	return 1;
}

// Оружие ближнего боя в руке (getMeleeWeapon): BT_MELEE с ценой удара
static uint8_t club(const aictx_t *c, uint8_t a, bitem_t *b, uint8_t *right, r_items_t *w)
{
	rtab_t t;
	if (!rtab_open(RES_RULE_ITEMS, &t)) return 0;
	for (uint8_t h = 1; h != 0xFF; h--) {
		if (!bi_hand(c->page, a, h, b) || b->item >= t.n) continue;
		rtab_get(&t, b->item, w);
		if (w->battle_type == BT_MELEE && w->tu_melee) { *right = h; return 1; }
	}
	return 0;
}

// Ближний бой (meleeAction, :1620-1680; MeleeAttackBState): цель рядом — поворот, в следующем такте
// удар; иначе путь к ней на время за вычетом удара (selectPointNearTarget — у нас путь в клетку цели,
// шаг в занятую клетку и так останавливает рядом). 1 — действовал
static uint8_t melee_ai(const aictx_t *c, unit_t *u, const vu_t *t, uint8_t right, const r_items_t *w)
{
	uint8_t cost = tu_cost(u, w, w->tu_melee);
	if (u->tu < cost) return 0;
	int16_t dx = (int16_t)t->x - u->x, dy = (int16_t)t->y - u->y;
	if (t->z == u->z && dx >= -1 && dx <= 1 && dy >= -1 && dy <= 1) {
		if (!ai_aim) {
			u->dir = dir_to(dx, dy);
			u_set(ai_a, u);
			show(c, ai_a, u);
			if (u->seen) bv_cell(u->x, u->y, u->z);
			ai_aim = 1;
			return 1;
		}
		ai_aim = 0;
		bafire_t q;
		q.s.page = c->page; q.s.cells = c->cells; q.s.sx = c->sx; q.s.sy = c->sy; q.s.sz = c->sz;
		q.s.shooter = ai_a; q.s.n = c->n; q.s.tx = t->x; q.s.ty = t->y; q.s.tz = t->z;
		q.s.action = BA_HIT; q.s.acc = 0;
		q.hand = right; q.tu = cost;
		ba_fire(&q);
		u_get(ai_a, u);
		ai_atk++;
		return 1;
	}
	if (ai_pi < ai_pn || u->tu <= cost) return 0;    // уже идёт к цели
	pf_req_t r;
	r.cells = c->cells; r.sx = c->sx; r.sy = c->sy; r.z = u->z; r.sz = c->sz;
	r.fx = u->x; r.fy = u->y; r.tx = t->x; r.ty = t->y;
	r.budget = (uint8_t)(u->tu - cost); r.step = STEP_TU; r.page = c->page;
	uint8_t path[PATH_MAX];
	ai_pn = pf_find(&r, path, PATH_MAX);
	if (ai_pn) ai_pn--;                              // последний шаг — в клетку цели: встать рядом
	memcpy(ai_path, path, ai_pn);
	ai_pi = 0;
	ai_moved = 1;
	return 0;
}

// Атака видимого бойца: 1 — выстрелил
static uint8_t attack(const aictx_t *c, unit_t *u)
{
	if (ai_atk >= ATK_MAX) return 0;
	bitem_t b;
	uint8_t right, hit = 0;
	r_items_t w;
	if (!gun(c, ai_a, &b, &right, &w)) {
		if (!club(c, ai_a, &b, &right, &w)) return 0;
		hit = 1;
	}
	// цель (selectNearestTarget, validTarget): ближайший не-пришелец, о котором знают (счёт
	// turnsSinceSpotted не больше intelligence) и которого видно без сектора (TileEngine::visible)
	vu_t v[MAX_BUNITS];
	fill_vu(c, v);
	rtab_t tr;
	uint8_t intel = rtab_open(RES_RULE_UNITS, &tr) && u->type < tr.n
		? (uint8_t)rtab_word(&tr, u->type, offsetof(r_units_t, intelligence)) : 0, tg = 0xFF;
	uint16_t best = 0xFFFF;
	for (uint8_t i = 0; i < c->n; i++) {
		if (!(v[i].flags & VU_ALIVE) || ai_tss[i] > intel || hostile(i)) continue;
		int16_t dx = (int16_t)v[i].x - u->x, dy = (int16_t)v[i].y - u->y;
		uint16_t d2 = (uint16_t)(dx * dx + dy * dy);
		if (d2 < best && vis_pair(v, ai_a, i, 0)) { best = d2; tg = i; }
	}
	if (tg == 0xFF) return 0;
	if (hit) return melee_ai(c, u, &v[tg], right, &w);
	if (!ai_aim && gren(c, u, v, tg, intel)) { ai_atk++; return 1; }   // граната — раньше оружия
	if (best > (uint16_t)w.max_range * w.max_range) return 0;
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
			// видит ли i юнита m: в секторе, а если в i попали (gotHit) — и без сектора
			// видит ли i юнита m: в секторе, а если в i попали (gotHit) — и без сектора
			uint8_t hit = i < c->nsold ? ai_hitby[i] != 0 : m < 16 && (ai_hitby[i] >> m) & 1;
			if (!vis_pair(v, i, m, !hit)) continue;
			int16_t sc = r.tu_max ? (int16_t)((uint16_t)r.reactions * r.tu / r.tu_max) : 0;
			if (sc > bs) { bs = sc; br = i; bright = right; bcost = cost; bb = b; bw = w; }
		}
		if (br == 0xFF || ms > bs) break;
		u_get(br, &r);
		r.dir = dir_to((int16_t)mv.x - r.x, (int16_t)mv.y - r.y);
		u_set(br, &r);
		show(c, br, &r);
		if (r.seen || r.faction == FACTION_PLAYER) bv_cell(r.x, r.y, r.z);
		if (r.faction == FACTION_PLAYER) ba_exp(br, EXP_REACT);   // tryReaction: addReactionExp
		fire(c, br, &r, &v[m], BA_SNAPSHOT, bcost, bright, &bb, &bw);
		res = AI_SEE | AI_HIT;
	}
	return res;
}

// Куда патрулировать (setupPatrol, AIModule.cpp:415-579): цель — узел _toNode (U_TONODE), пока
// не дошёл — она помнится между ходами; дошёл — узел становится «откуда» (u->node), новая цель —
// связанный с ним узел, иначе любой; до 5 попыток найти узел, к которому есть путь. В OpenXcom
// путь полный, и юнит идёт по нему, пока хватает TU; у нас поиск — в пределах времени юнита и
// только в малом окне вокруг начала и цели (pf_near_once), а не дошли — к достижимой клетке,
// ближайшей к узлу (pf_partial_once): полный путь стоил ~48 кадров, второй заход по окну 64x64 —
// до 350. Уже стоит в ближайшей — другой узел. Путь — в ai_path
static void plan_patrol(const aictx_t *c, unit_t *u)
{
	far_t np;
	uint16_t nn = mapgen_nodes(&np), to = U_TONODE(u);
	bnode_t b;
	if (ai_dbgn && ai_dbgn <= 4) {                   // путь от сценария
		memcpy(ai_path, AI_DBGP, ai_dbgn);
		ai_pn = ai_dbgn; ai_pi = 0; ai_dbgn = 0;
		return;
	}
	if (!nn) return;
	if (to < nn) {
		far_read(np + (uint32_t)to * sizeof b, &b, sizeof b);
		if (b.x == u->x && b.y == u->y && b.z == u->z) { u->node = to; to = 0xFFFF; }   // дошёл
	} else to = 0xFFFF;
	uint8_t path[PATH_MAX];                     // буфер — на стеке: банку 30 память банка 36 не видна
	ai_pn = 0;
	for (uint8_t tries = 0; tries < 5 && !ai_pn; tries++) {
		if (to == 0xFFFF) {
			if (u->node < nn) {
				far_read(np + (uint32_t)u->node * sizeof b, &b, sizeof b);
				uint16_t links[5];
				uint8_t nk = 0;
				for (uint8_t k = 0; k < 5; k++) if (b.link[k] < nn) links[nk++] = b.link[k];
				if (nk) to = links[rng_range(0, nk - 1)];
			}
			if (to == 0xFFFF) to = rng_range(0, nn - 1);
		}
		far_read(np + (uint32_t)to * sizeof b, &b, sizeof b);
		if (b.x != 0xFF && b.z == u->z) {
			pf_req_t r;
			r.cells = c->cells; r.sx = c->sx; r.sy = c->sy; r.z = u->z; r.sz = c->sz;
			r.fx = u->x; r.fy = u->y; r.tx = b.x; r.ty = b.y;
			r.budget = u->tu; r.step = STEP_TU; r.page = c->page;
			pf_near_once();
			pf_partial_once();
			ai_pn = pf_find(&r, path, PATH_MAX);
		}
		if (!ai_pn) to = 0xFFFF;
	}
	memcpy(ai_path, path, ai_pn);
	ai_pi = 0;
	U_TONODE(u) = to;
	u_set(ai_a, u);
}

// Клетки двери сменились: перерисовать и пересчитать маску обзора (тройки x, y, z до #FF)
static void door_cells(const aictx_t *c, const uint8_t *cl, uint8_t n)
{
	for (uint8_t i = 0; i < n && cl[i * 3] != 0xFF; i++) {
		bv_cell(cl[i * 3], cl[i * 3 + 1], cl[i * 3 + 2]);
		vis_cell(c->cells, cl[i * 3], cl[i * 3 + 1], cl[i * 3 + 2], c->page);
	}
}

// Створки НЛО едут — кадр раз в 100 мс (Tile::animate); 1 — ещё едут
static uint8_t door_anim(const aictx_t *c, const dmap_t *m)
{
	uint8_t parts[5], cl[15];
	ai_door = doors_anim(m, parts, cl);
	ai_dtm = (uint8_t)frames;
	for (uint8_t i = 0; parts[i]; i++) bv_tile(parts[i]);
	door_cells(c, cl, 5);
	return ai_door;
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
	{                                                // дверь на пути — сперва открыть (UnitWalkBState:
		uint8_t list[12], spent = 0;                 //   unitOpensDoor), шаг — в следующем такте
		memset(list, 0xFF, sizeof list);
		uint8_t r = door_open(&m, u->x, u->y, u->z, d, 0, u->tu, &spent, list, 4);
		if (r == 1) { ai_pn = 0; return 0; }         // не хватило времени — путь прерван
		if (r == 0 || r == 3) {                      // открыл (или створка НЛО ещё едет — ждать)
			u->tu -= spent;
			u->dir = d;
			u_set(ai_a, u);
			show(c, ai_a, u);
			if (r == 0 && !door_anim(c, &m))         // обычная дверь: клетки (пары x, y) — сразу
				for (uint8_t i = 0; i < 4 && list[i * 2] != 0xFF; i++) {
					bv_cell(list[i * 2], list[i * 2 + 1], u->z);
					vis_cell(c->cells, list[i * 2], list[i * 2 + 1], u->z, c->page);
				}
			return 1;
		}
	}
	uint8_t cost = cell_cost(&m, (uint8_t)nx, (uint8_t)ny, u->z, d, STEP_TU);	if (!cost || u->tu < cost) { ai_pn = 0; return 0; }
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
	if (u->seen) { bv_cell(ox, oy, oz); bv_cell(u->x, u->y, u->z); follow(u); }
	moved(c);
	return 1;
}

// Клетки юнита — перерисовать
static void cells(const unit_t *u)
{
	for (uint8_t q = 0; q < u->size * u->size; q++)
		bv_cell((uint8_t)(u->x + (q & 1)), (uint8_t)(u->y + (q >> 1)), u->z);
}

// Пришелец, которого отряд больше не видит, снова невидим: перед каждым действием
// (BattlescapeGame::handleAI:204) и после каждого шага (UnitWalkBState.cpp:197-201) — setVisible(false)
// и calculateFOV. 1 — спрятан (клетки перерисовывает вызывающий)
static uint8_t hide_check(const aictx_t *c, unit_t *u)
{
	if (!u->seen || !u->alive) return 0;
	vu_t v[MAX_BUNITS];
	fill_vu(c, v);
	if (vis_by_xcom(v, c->nsold, ai_a)) return 0;
	u->seen = 0;
	u_set(ai_a, u);
	show(c, ai_a, u);
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
	memset(ai_hitby, 0, c->nsold * 2);               // resetUnitHitStates — перед действием ИИ
	uint8_t r = ai_step(c);
	return r & AI_DONE ? r : r | hidden(c);
}

static uint8_t ai_step(const aictx_t *c)
{
	unit_t u;
	if (ai_door) {                                   // створки НЛО едут — пришелец ждёт
		uint8_t f = (uint8_t)frames, t = ai_dtm;     // сравнения — с копиями (SDCC 4.5)
		f -= t;
		if (f < 5) return 0;
		dmap_t m;
		m.cells = c->cells; m.sx = c->sx; m.sy = c->sy; m.sz = c->sz; m.page = c->page;
		return door_anim(c, &m) ? 0 : AI_SEE;        // обзор отряда — раз, когда доехали
	}
	if (ai_walk) {                                   // шаг с анимацией
		uint8_t r = bv_walk_tick();
		if (r == 1) {                                // середина: числится в новой клетке
			u_get(ai_a, &u);
			u.x = ai_wx; u.y = ai_wy;
			u_set(ai_a, &u);
			show(c, ai_a, &u);
			follow(&u);
			moved(c);
			return AI_SEE;
		}
		if (r != 2) return 0;
		ai_walk = 0;
		uint8_t rf = ai_react(c, ai_a);              // шаг сделан — отряд может ответить огнём
		if (rf) ai_pn = 0;                           //   (UnitWalkBState: путь обрывается)
		u_get(ai_a, &u);
		if (hide_check(c, &u)) bv_walk_done();       // ушёл из вида — клетки шага уже без него
		if (u.alive && ai_pi < ai_pn && step(c, &u)) return AI_SEE;   // следующий шаг — без кадра стойки
		bv_walk_done();
		return AI_SEE | rf;
	}
	while (ai_a < c->n) {
		u_get(ai_a, &u);
		uint8_t f = u.faction;                       // сравнения — с копиями (SDCC 4.5)
		if (!u.alive || f == FACTION_PLAYER || ai_a < c->nsold) { next_alien(); continue; }
		if (!ai_moved && !ai_atk && !ai_aim && hide_check(c, &u)) cells(&u);   // начал действовать
		// мирные (ход FACTION_NEUTRAL — после пришельцев: они за ними в списке) только патрулируют
		if (f == FACTION_HOSTILE && attack(c, &u)) {                         // стрелял — в следующем такте думает снова
			if (ai_aim) return AI_SEE | AI_HIT;
			uint8_t rf = ai_react(c, ai_a);          // после выстрела — ответ отряда
			u_get(ai_a, &u);
			if (hide_check(c, &u)) cells(&u);        // мог убить того, кто его видел
			return AI_SEE | AI_HIT | rf;
		}
		if (ai_pi >= ai_pn && !ai_moved) { ai_moved = 1; plan_patrol(c, &u); }
		if (ai_pi < ai_pn && step(c, &u)) {
			uint8_t rf = 0;
			if (!ai_walk) {                          // шаг без анимации — сразу ответ и проверка вида
				rf = ai_react(c, ai_a);
				if (rf) ai_pn = 0;
				u_get(ai_a, &u);
				if (hide_check(c, &u)) cells(&u);
			}
			return AI_SEE | rf;
		}
		next_alien();
		return 0;                                    // по пришельцу на такт
	}
	{                                                // конец хода: двери НЛО закрываются (endTurn)
		uint8_t list[24];
		dmap_t m;
		m.cells = c->cells; m.sx = c->sx; m.sy = c->sy; m.sz = c->sz; m.page = c->page;
		memset(list, 0xFF, sizeof list);
		door_cells(c, list, doors_close_ufo(&m, list, 8));
	}
	return AI_DONE;
}
