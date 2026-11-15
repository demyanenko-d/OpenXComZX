// Действие предметом в руке (банк 37): выстрел, бросок, удар — ba_fire (bact.h). Вынесено из
// bact.c (банк 36 заполнился с ИИ): подготовка снаряда, урон и удар остались там (ba_fill,
// ba_shot, ba_melee — память ИИ, кто кого видел и ранил, живёт в банке 36).
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

uint8_t ba_fill(const bashot_t *q, fu_t *f, shot_t *s) __banked;   // bact.c
uint8_t ba_melee(const bashot_t *a, const unit_t *u, const r_items_t *w, uint8_t go) __banked;

static void u_get(uint8_t i, unit_t *u)
{
	far_read(UNITS_FAR + (uint16_t)i * sizeof *u, u, sizeof *u);
}

static void u_set(uint8_t i, const unit_t *u)
{
	far_write(UNITS_FAR + (uint16_t)i * sizeof *u, u, sizeof *u);
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
	uint8_t n = ba_fill(a, f, &s), e = gr_throw(&s, f, &r);
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
	if (a->action == BA_HIT) {                   // удар — в соседнюю клетку (validMeleeRange)
		uint16_t h = ba_melee(a, &u, &w, 0), tz = a->tz, uz = u.z;   // сравнения — словами (SDCC 4.5, checkasm)
		if (dx < -1 || dx > 1 || dy < -1 || dy > 1 || tz != uz || (!dx && !dy) || h == 0xFF) return STR_THERE_IS_NO_ONE_THERE;
		u.dir = dir_to(dx, dy);
		u.tu -= q->tu;
		u_set(i, &u);
		ba_melee(a, &u, &w, 1);
		return 0;
	}
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
