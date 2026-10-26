// Действия юнитов в бою (банк 36): общее для бойца по команде и для пришельца в его ход.
// Выстрел — бывшая середина shoot() экрана боя (scr_battle.c): банк 31 переполнен, а пришельцу
// (ИИ) нужно то же самое. Юниты — units[] экрана боя дальним доступом (units.h UNITS_FAR).
#include <stdint.h>
#include <string.h>
#include "far.h"
#include "res.h"
#include "res_ids.h"
#include "rules.h"
#include "loader.h"
#include "units.h"
#include "bview.h"
#include "bfire.h"
#include "bfx.h"
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
	uint8_t n = q->n > MAX_BUNITS ? MAX_BUNITS : q->n, dir = 0;
	for (uint8_t k = 0; k < n; k++) {
		u_get(k, &v);
		f[k].x = v.x; f[k].y = v.y; f[k].z = v.z;
		f[k].flags = (v.alive ? FU_ALIVE : 0) | (v.size > 1 ? FU_BIG : 0) | (v.kneel ? FU_KNEEL : 0);
		f[k].height = v.kneel ? v.h_kneel : v.h_stand;
		f[k].armor = v.armor_rule;
		if (k == q->shooter) dir = v.dir;
	}
	shot_t s;
	s.page = q->page; s.cells = q->cells; s.sx = q->sx; s.sy = q->sy; s.sz = q->sz;
	s.shooter = q->shooter; s.n = n; s.tx = q->tx; s.ty = q->ty; s.tz = q->tz;
	s.action = q->action; s.acc = q->acc; s.weapon = q->weapon; s.dir = dir;
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
	if (dead) {                                  // выбыл: со сцены, труп на пол (bfx.c)
		ru_write(q->page, s.hit, &v);
		fl_corpse(q->page, s.hit, v.x, v.y, v.z, v.size, v.armor_rule);
		bv_sync(n);
		for (uint8_t k = 0; k < v.size * v.size; k++)
			bv_cell((uint8_t)(v.x + (k & 1)), (uint8_t)(v.y + (k >> 1)), v.z);
	}
	return s.hit;
}
