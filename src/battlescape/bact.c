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
	r.flags = (u->alive && u->seen && (u->sheet || u->faction == FACTION_PLAYER) ? RU_ALIVE : 0)
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

uint8_t ba_shot(const bashot_t *q) __banked
{
	rtab_t t;
	r_items_t w;
	unit_t v;
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
	u_get(s.hit, &v);                            // TileEngine::hit: урон, мораль
	dmg_t d;
	uint8_t hs = v.size * 8;
	d.power = power; d.type = type;
	d.rx = s.vx - ((int16_t)v.x * 16 + hs); d.ry = s.vy - ((int16_t)v.y * 16 + hs);
	d.rz = s.vz - (int16_t)v.z * 24;
	d.dir = v.dir; d.height = f[s.hit].height; d.armor_rule = v.armor_rule;
	memcpy(d.armor, v.armor, 5);
	d.hp = v.hp; d.stun = v.stun;
	uint8_t adj = fire_damage(&d);
	memcpy(v.armor, d.armor, 5);
	v.hp = d.hp; v.stun = d.stun;
	uint16_t loss = (uint16_t)adj * ((110 - v.bravery) / 10) / 10;
	v.mor = loss >= v.mor ? 0 : (uint8_t)(v.mor - loss);
	uint8_t dead = !v.hp || v.stun >= v.hp;
	if (dead) v.alive = 0;
	u_set(s.hit, &v);
	if (dead) ba_kill(q->page, s.hit, n);
	return s.hit;
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

// ProjectileFlyBState::createNewProjectile, BA_THROW: дуга (bfire.c), предмет из руки летит
// (bfx.c) и ложится на пол клетки падения; пришелец бросает гранату сразу взведённой
static uint16_t throw_it(bafire_t *q, unit_t *u, uint16_t bi, const bitem_t *b, const r_items_t *w)
{
	bashot_t *a = &q->s;
	rtab_t t;
	throw_t r;
	fu_t f[MAX_BUNITS];
	shot_t s;
	r.strength = u->strength; r.kneel = u->kneel; r.weight = w->weight; r.wtotal = w->weight;
	r.acc = a->acc;
	if (b->ammo != 0xFFFF && b->ammo != b->item && rtab_open(RES_RULE_ITEMS, &t) && b->ammo < t.n)
		r.wtotal += (uint8_t)rtab_word(&t, b->ammo, offsetof(r_items_t, weight));
	uint8_t n = fill(a, f, &s), e = gr_throw(&s, f, &r);
	if (e) return e == 1 ? STR_OUT_OF_RANGE : STR_UNABLE_TO_THROW_HERE;
	u->tu -= q->tu;
	u_set(a->shooter, u);
	if (u->faction != FACTION_PLAYER && w->battle_type == BT_GRENADE) {
		bitem_t g = *b;
		g.rounds = 1;                            // таймер 0
		far_write(FAR(a->page + 1, TL_BITEMS) + (bi - 1) * sizeof g, &g, sizeof g);
	}
	far_fill(FAR(a->page + 1, TL_BITEMS) + (bi - 1) * sizeof(bitem_t), BI_GROUND, 1);   // из руки
	bi_hands(a->page, n);
	bv_sync(n);
	bv_cell(u->x, u->y, u->z);
	fxthrow_t x;
	x.page = a->page; x.ox = s.ox; x.oy = s.oy; x.oz = s.oz; x.dx = r.dx; x.dy = r.dy; x.dz = r.dz;
	x.r16 = r.r16; x.z16 = r.z16; x.n = r.n; x.item = b->item;
	fx_throw(&x);
	fl_drop(a->page, bi - 1, r.lx, r.ly, r.lz);
	bv_sync(n);
	bv_cell(r.lx, r.ly, r.lz);
	return 0;
}

uint16_t ba_fire(bafire_t *q) __banked
{
	bashot_t *a = &q->s;
	unit_t u;
	bitem_t b;
	rtab_t t;
	r_items_t w;
	uint8_t i = a->shooter;
	uint16_t bi = bi_hand(a->page, i, q->hand, &b);
	if (!bi || !rtab_open(RES_RULE_ITEMS, &t) || b.item >= t.n) return 0xFFFF;
	rtab_get(&t, b.item, &w);
	u_get(i, &u);
	if (u.tu < q->tu) return STR_NOT_ENOUGH_TIME_UNITS;
	int16_t dx = (int16_t)a->tx - u.x, dy = (int16_t)a->ty - u.y;
	a->weapon = b.item; a->ammo = b.ammo;
	if (a->action != BA_THROW) {
		if (w.compatible_ammo.n && b.ammo == 0xFFFF) return STR_NO_AMMUNITION_LOADED;
		if (w.compatible_ammo.n && !b.rounds) return STR_NO_ROUNDS_LEFT;
		if ((uint16_t)(dx * dx + dy * dy) > (uint16_t)w.max_range * w.max_range) return STR_OUT_OF_RANGE;
	}
	u.dir = dir_to(dx, dy);                  // lookAt
	u_set(i, &u);
	if (a->action == BA_THROW) return throw_it(q, &u, bi, &b, &w);
	u.tu -= q->tu;
	u_set(i, &u);
	uint8_t shots = a->action == BA_AUTOSHOT ? w.auto_shots : 1;
	for (uint8_t k = 0; k < shots; k++) {
		if (w.compatible_ammo.n) {               // BattleItem::spendBullet; кончились — очередь обрывается
			bitem_t c;
			if (!bi_hand(a->page, i, q->hand, &c) || !c.rounds) break;
			bi_spend(a->page, i, q->hand);
		}
		ba_shot(a);
	}
	return 0;
}
