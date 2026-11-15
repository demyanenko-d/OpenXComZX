// Действия юнитов в бою (банк 36): общее для бойца по команде и для пришельца в его ход.
// Выстрел — бывшая середина shoot() экрана боя (scr_battle.c): банк 31 переполнен, а пришельцу
// (ИИ) нужно то же самое. Юниты — units[] экрана боя дальним доступом (units.h UNITS_FAR).
#include <stdint.h>
#include <string.h>
#include <stddef.h>
#include "far.h"
#include "res.h"
#include "res_ids.h"
#include "rules.h"
#include "loader.h"
#include "units.h"
#include "bview.h"
#include "bfire.h"
#include "bfx.h"
#include "bitems.h"
#include "bmenu.h"
#include "doors.h"
#include "str_ids.h"
#include "bgren.h"
#include "bact.h"
#include "bai.h"
#include "game.h"

static void u_get(uint8_t i, unit_t *u)
{
	far_read(UNITS_FAR + (uint16_t)i * sizeof *u, u, sizeof *u);
}

static void u_set(uint8_t i, const unit_t *u)
{
	far_write(UNITS_FAR + (uint16_t)i * sizeof *u, u, sizeof *u);
}

// Запись юнита для вида (как ru_put экрана боя)
static void ru_write(uint8_t page, uint8_t i, const unit_t *u)
{
	runit_t r;
	r.x = u->x; r.y = u->y; r.z = u->z; r.dir = u->dir;
	r.flags = (u->alive && u->seen && (u->sheet || u->type == 0xFFFF) ? RU_ALIVE : 0)
		| (u->kneel ? RU_KNEEL : 0) | (u->size > 1 ? RU_BIG : 0);
	r.sheet = u->sheet;
	r.pad[0] = r.pad[1] = 0;
	far_write(FAR(page + 1, TL_RUNITS) + (uint16_t)i * sizeof r, &r, sizeof r);
}

// Юниты для траектории (fu_t) и выстрел из запроса; вернёт число юнитов
static uint8_t fill(const bashot_t *q, fu_t *f, shot_t *s)
{
	unit_t v;
	uint8_t n = q->n > MAX_BUNITS ? MAX_BUNITS : q->n, dir = 0;
	for (uint8_t k = 0; k < n; k++) {
		u_get(k, &v);
		f[k].x = v.x; f[k].y = v.y; f[k].z = v.z;
		f[k].flags = (v.alive ? FU_ALIVE : 0) | (v.size > 1 ? FU_BIG : 0) | (v.kneel ? FU_KNEEL : 0);
		f[k].height = v.kneel ? v.h_kneel : v.h_stand;
		f[k].armor = v.armor_rule;
		if (k == q->shooter) dir = v.dir;
	}
	s->page = q->page; s->cells = q->cells; s->sx = q->sx; s->sy = q->sy; s->sz = q->sz;
	s->shooter = q->shooter; s->n = n; s->tx = q->tx; s->ty = q->ty; s->tz = q->tz;
	s->action = q->action; s->acc = q->acc; s->weapon = q->weapon; s->dir = dir;
	return n;
}

// ba_morale — в bmorale.c (банк 35: этот банк полон)

// Попадание по юниту h (TileEngine::hit): урон с бронёй по стороне — от точки удара (vx, vy, vz),
// мораль; выбывший — со сцены. Стрелок замечен пришельцами и помнится раненым (gotHit)
static uint8_t is_player(uint8_t i)
{
	return far_byte(UNITS_FAR + (uint16_t)i * sizeof(unit_t) + offsetof(unit_t, faction)) == FACTION_PLAYER;
}

// xp — опыт бьющего бойца за попадание по пришельцу (EXP_FIRE / EXP_MELEE)
static void hurt(uint8_t page, uint8_t n, uint8_t shooter, uint8_t h, int16_t vx, int16_t vy, int16_t vz,
		 uint16_t power, uint8_t type, uint8_t height, uint8_t xp)
{
	unit_t v;
	u_get(h, &v);
	if (v.faction == FACTION_HOSTILE && is_player(shooter)) ba_exp(shooter, xp);
	dmg_t d;
	uint8_t hs = v.size * 8;
	d.power = power; d.type = type;
	d.rx = vx - ((int16_t)v.x * 16 + hs); d.ry = vy - ((int16_t)v.y * 16 + hs);
	d.rz = vz - (int16_t)v.z * 24;
	d.dir = v.dir; d.height = height; d.armor_rule = v.armor_rule;
	memcpy(d.armor, v.armor, 5);
	d.hp = v.hp; d.stun = v.stun;
	d.wu = v.faction == FACTION_PLAYER && h < MAX_UNITS ? h : 0xFF;   // раны — бойцам X-COM (isWoundable)
	d.page = page;
	uint8_t adj = fire_damage(&d);
	memcpy(v.armor, d.armor, 5);
	v.hp = d.hp; v.stun = d.stun;
	uint16_t loss = (uint16_t)adj * ((110 - v.bravery) / 10) / 10 + d.mor;
	v.mor = loss >= v.mor ? 0 : (uint8_t)(v.mor - loss);
	uint8_t dead = !v.hp || v.stun >= v.hp;
	if (dead) v.alive = 0;
	if (v.faction == FACTION_HOSTILE) {          // попал в пришельца — его заметили (ProjectileFlyBState:794)
		ai_tss[shooter] = 0;
		if (shooter < 16) ai_hitby[h] |= (uint16_t)1 << shooter;   // setWasHitBy
	} else ai_hitby[h] = 1;                      // hitState
	u_set(h, &v);
	if (dead) {
		if (!v.hp) ba_morale(h, shooter, n);     // гибель, не оглушение
		ba_kill(page, h, n);
	}
}

uint8_t ba_shot(const bashot_t *q) __banked
{
	rtab_t t;
	r_items_t w;
	if (!rtab_open(RES_RULE_ITEMS, &t) || q->weapon >= t.n) return 0xFF;
	rtab_get(&t, q->weapon, &w);
	uint16_t power = w.power;
	uint8_t type = w.damage_type;
	if (q->ammo != 0xFFFF && q->ammo < t.n) {   // мощность и тип урона — обоймы
		rtab_get(&t, q->ammo, &w);
		power = w.power; type = w.damage_type;
	}
	fu_t f[MAX_BUNITS];
	shot_t s;
	uint8_t n = fill(q, f, &s);
	fire_shot(&s, f);
	{                                            // полёт пули и вспышка (bfx.c), урон — после
		fxshot_t x;
		x.page = q->page; x.weapon = q->weapon; x.ammo = q->ammo;
		x.ox = s.ox; x.oy = s.oy; x.oz = s.oz; x.ex = s.vx; x.ey = s.vy; x.ez = s.vz;
		fx_shot(&x);
	}
	if (s.res != V_UNIT || s.hit == 0xFF) return 0xFF;
	hurt(q->page, n, q->shooter, s.hit, s.vx, s.vy, s.vz, power, type, f[s.hit].height, EXP_FIRE);
	return s.hit;
}

// Удар (MeleeAttackBState::resolveHit, :224-281): шанс — accuracyMelee (· melee, если skillApplied;
// поправки на раны нет); сила — meleePower
// (оглушение — приклад), у оружия ближнего боя — его мощность и тип урона; strengthApplied — плюс
// сила; точка удара — у цели со смещением к бьющему (броня — с его стороны). Цель — юнит в клетке
// (tx, ty, tz); вернёт его номер (#FF — промах или никого)
static uint8_t melee_target(const bashot_t *a)
{
	for (uint8_t i = 0; i < a->n; i++) {
		unit_t o;
		u_get(i, &o);
		if (i != a->shooter && o.alive && o.z == a->tz && (uint8_t)(a->tx - o.x) < o.size && (uint8_t)(a->ty - o.y) < o.size) return i;
	}
	return 0xFF;
}

static uint8_t melee(const bashot_t *a, const unit_t *u, const r_items_t *w, uint8_t h)
{
	uint16_t acc = w->accuracy_melee;            // getFiringAccuracy(BA_HIT): навык — если skillApplied
	if (w->flags & ITEMS_F_SKILL_APPLIED) acc = acc * u->melee / 100;
	if (rng_range(0, 99) >= acc) return 0xFF;
	uint16_t power = w->melee_power;
	uint8_t type = 6;                            // DT_STUN
	if (w->battle_type == BT_MELEE) { power = w->power; type = w->damage_type; }
	if (w->flags & ITEMS_F_STRENGTH_APPLIED) power += u->strength;
	unit_t v;
	u_get(h, &v);
	int8_t sx = (int8_t)(u->x > a->tx ? 1 : u->x < a->tx ? -1 : 0), sy = (int8_t)(u->y > a->ty ? 1 : u->y < a->ty ? -1 : 0);
	hurt(a->page, a->n, a->shooter, h, (int16_t)a->tx * 16 + 8 + sx, (int16_t)a->ty * 16 + 8 + sy,
	     (int16_t)a->tz * 24 + UNIT_H(&v) / 2, power, type, UNIT_H(&v), EXP_MELEE);
	return h;
}

void ba_kill(uint8_t page, uint8_t i, uint8_t n) __banked
{
	unit_t v;
	u_get(i, &v);
	ru_write(page, i, &v);
	fl_corpse(page, i, v.x, v.y, v.z, v.size, v.armor_rule);
	bv_sync(n);
	for (uint8_t k = 0; k < v.size * v.size; k++)
		bv_cell((uint8_t)(v.x + (k & 1)), (uint8_t)(v.y + (k >> 1)), v.z);
}

// Для bact2.c (банк 37: ba_fire и бросок вынесены — банк 36 был полон)
uint8_t ba_fill(const bashot_t *q, fu_t *f, shot_t *s) __banked
{
	return fill(q, f, s);
}

// Удар: go = 0 — только кто в клетке цели (#FF — никого), иначе — сам удар по нему
uint8_t ba_melee(const bashot_t *a, const unit_t *u, const r_items_t *w, uint8_t go) __banked
{
	uint8_t h = melee_target(a);
	if (go && h != 0xFF) melee(a, u, w, h);
	return h;
}

