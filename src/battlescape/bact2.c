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
#include "vision.h"
#include "game.h"

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

// Паника в начале хода стороны (BattleUnit::prepareNewTurn :1710-1725, BattlescapeGame::
// handlePanickingUnit :1267-1335, UnitPanicBState): у живого 1x1 шанс 100 − 2 · мораль; треть —
// берсерк, остальные — паника, из них половина бросает оружие из рук на пол. Потом время 0,
// мораль +15. Упрощено: паникующий не убегает, берсерк не стреляет (14_todo). В list — номера
// (бит 7 — берсерк); вернёт их число (до max)
uint8_t ba_panic(uint8_t page, uint8_t side, uint8_t n, uint8_t *list, uint8_t max) __banked
{
	uint8_t cnt = 0;
	for (uint8_t i = 0; i < n && cnt < max; i++) {
		unit_t u;
		u_get(i, &u);
		uint8_t f = u.faction;               // сравнения — с копиями (SDCC 4.5)
		if (!u.alive || f != side || u.size != 1) continue;
		int16_t chance = 100 - 2 * (int16_t)u.mor;
		if ((int16_t)rng_range(1, 100) > chance) continue;
		uint8_t berserk = rng_range(0, 100) <= 33;
		if (!berserk && rng_range(0, 100) <= 50)         // бросает оружие
			for (uint8_t h = 0; h < 2; h++) {
				bitem_t b;
				uint16_t bi = bi_hand(page, i, h, &b);
				if (!bi) continue;
				far_fill(FAR(page + 1, TL_BITEMS) + (bi - 1) * sizeof(bitem_t), BI_GROUND, 1);
				fl_drop(page, bi - 1, u.x, u.y, u.z);
			}
		if (!berserk) u.tu = 0;              // берсерк сперва стреляет (ba_berserk), время — потом
		u.mor = u.mor > 85 ? 100 : (uint8_t)(u.mor + 15);
		u_set(i, &u);
		list[cnt++] = (uint8_t)(i | (berserk ? 0x80 : 0));
	}
	if (cnt) { bi_hands(page, n); bv_sync(n); }
	return cnt;
}

// Оглушение (BattleUnit::prepareNewTurn :1705-1708 — юнитам стороны хода −1, мирным — в ход
// пришельцев; SavedBattleGame::reviveUnconsciousUnits :1524-1558): лежащий без сознания 1x1 с
// оглушением меньше здоровья встаёт, если его клетка свободна (соседние клетки оригинал тоже
// пробует — у нас нет), тело (corpseBattle брони) с пола клетки убирается. Вернёт, сколько встало
uint8_t ba_stun(uint8_t page, uint8_t side, uint8_t n) __banked
{
	uint8_t up = 0;
	for (uint8_t i = 0; i < n; i++) {
		unit_t u, o;
		u_get(i, &u);
		uint8_t f = u.faction;               // сравнения — с копиями (SDCC 4.5)
		if (u.stun && (f == side || (side == FACTION_HOSTILE && f == FACTION_NEUTRAL)) && (u.size == 1 || u.alive)) u.stun--;
		if (!u.alive && u.hp && u.stun < u.hp && u.size == 1) {
			uint8_t busy = 0;
			for (uint8_t k = 0; k < n && !busy; k++) {
				u_get(k, &o);
				busy = k != i && o.alive && o.x == u.x && o.y == u.y && o.z == u.z;
			}
			rtab_t t;
			r_armors_t ar;
			if (!busy && u.armor_rule != 0xFFFF && rtab_open(RES_RULE_ARMORS, &t) && u.armor_rule < t.n) {
				rtab_get(&t, u.armor_rule, &ar);
				uint16_t body = 0xFFFF;
				if (ar.corpse_battle.n) rtab_tail(&t, ar.corpse_battle.off, &body, 2);
				for (uint16_t k = 0; k < BI_MAX; k++) {      // removeUnconsciousBodyItem
					bitem_t b;
					far_read(FAR(page + 1, TL_BITEMS) + k * sizeof b, &b, sizeof b);
					if (b.owner == BI_GROUND && b.item == body && b.x == u.x && b.y == u.y && b.z == u.z) {
						far_fill(FAR(page + 1, TL_BITEMS) + k * sizeof b, BI_NONE, 1);
						break;
					}
				}
			}
			if (!busy) { u.alive = 1; u.kneel = 0; u.tu = f == side ? u.tu_max : 0; up++; }
		}
		u_set(i, &u);
	}
	if (up) fl_rebuild(page);
	return up;
}

// Выстрел берсерка (UnitPanicBState::think :62-125): оружие в руке с очередью (иначе навскидку),
// времени хватает — по ближайшему врагу, которого он видит (в секторе), иначе в случайную клетку
// ±6. Заполнить в q карту, стрелка и n. 1 — выстрелил
uint8_t ba_berserk(bafire_t *q) __banked
{
	bashot_t *a = &q->s;
	uint8_t i = a->shooter, right = 0xFF;
	unit_t u, x;
	rtab_t t;
	r_items_t w;
	bitem_t b;
	u_get(i, &u);
	if (!u.alive || !rtab_open(RES_RULE_ITEMS, &t)) return 0;
	for (uint8_t h = 1; h != 0xFF; h--) {            // getMainHandWeapon
		if (!bi_hand(a->page, i, h, &b) || b.item >= t.n) continue;
		rtab_get(&t, b.item, &w);
		if (w.battle_type == BT_FIREARM && (w.tu_snap || w.tu_auto)) { right = h; break; }
	}
	if (right == 0xFF) return 0;
	uint8_t pct = w.tu_auto ? w.tu_auto : w.tu_snap;
	uint8_t cost = (w.flags & ITEMS_F_FLAT_RATE) ? pct : (uint8_t)((uint16_t)u.tu_max * pct / 100);
	if (u.tu < cost) return 0;
	vu_t v[MAX_BUNITS];
	uint8_t n = a->n > MAX_BUNITS ? MAX_BUNITS : a->n, tg = 0xFF;
	for (uint8_t k = 0; k < n; k++) {
		u_get(k, &x);
		v[k].x = x.x; v[k].y = x.y; v[k].z = x.z; v[k].dir = x.dir;
		v[k].flags = (x.alive ? VU_ALIVE : 0) | (x.size > 1 ? VU_BIG : 0) | VU_H(UNIT_H(&x));
	}
	uint16_t best = 0xFFFF;
	for (uint8_t k = 0; k < n; k++) {                // ближайший видимый враг
		u_get(k, &x);
		uint8_t f = x.faction, uf = u.faction;       // сравнения — с копиями (SDCC 4.5)
		if (k == i || f == uf || (uf == FACTION_PLAYER && f == FACTION_NEUTRAL)) continue;
		int16_t dx = (int16_t)x.x - u.x, dy = (int16_t)x.y - u.y;
		uint16_t d2 = (uint16_t)(dx * dx + dy * dy);
		if (d2 < best && vis_pair(v, i, k, 1)) { best = d2; tg = k; }
	}
	if (tg != 0xFF) { a->tx = v[tg].x; a->ty = v[tg].y; a->tz = v[tg].z; }
	else {                                           // наугад ±6
		int16_t tx = (int16_t)u.x + (int16_t)rng_range(0, 12) - 6, ty = (int16_t)u.y + (int16_t)rng_range(0, 12) - 6;
		a->tx = (uint8_t)(tx < 0 ? 0 : tx >= a->sx ? a->sx - 1 : tx);
		a->ty = (uint8_t)(ty < 0 ? 0 : ty >= a->sy ? a->sy - 1 : ty);
		a->tz = u.z;
	}
	uint16_t acc = (uint16_t)u.firing * (w.tu_auto ? w.accuracy_auto : w.accuracy_snap) / 100;
	if (u.kneel) acc = acc * 115 / 100;
	a->action = w.tu_auto ? BA_AUTOSHOT : BA_SNAPSHOT;
	a->acc = acc > 255 ? 255 : (uint8_t)acc;
	q->hand = right; q->tu = cost;
	return ba_fire(q) == 0;
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
