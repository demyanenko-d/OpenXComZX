// Снаряжение отряда из груза корабля (банк 15): порт той части BattlescapeGenerator::deployXCOM
// (REF/OpenXcom/src/Battlescape/BattlescapeGenerator.cpp:719-797), что идёт после раскладок:
// груз корабля ложится на «землю» корабля (в порядке std::map — sort_rank), loadWeapons заряжает
// оружие обоймами с земли, autoEquip (там же, :799-866) в четыре прохода — винтовки, пистолеты,
// обоймы, остальное — раздаёт бойцам без раскладки по правилам _addItem (:1361-1534): оружие —
// в правую руку, если сил хватает (strength · 0.66 >= вес), обоймы — только к оружию в руках,
// прочее — в первую подходящую клетку секций-сеток. Не разобранное остаётся на земле.
// Бой груз кампании не трогает: что вернётся на корабль — дело разбора после боя.
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "far.h"
#include "pages.h"
#include "res.h"
#include "res_ids.h"
#include "rules.h"
#include "str_ids.h"
#include "state.h"
#include "loader.h"
#include "units.h"
#include "bitems.h"
#include "bequip.h"

#define BT_FLARE  10
#define GI_MAX    96
typedef struct { uint16_t item, ammo; uint8_t rounds, used; } gitem_t;
// Память банка (с #B300; Win1 забит): земля корабля, правила, секции
static gitem_t __at(0xB300) gi[GI_MAX];  // до #B540
static rtab_t __at(0xB540) ti;           // правила items
static rtab_t __at(0xB550) tv;           //   и invs
static uint8_t __at(0xB560) sec_r;       // секции: правая и левая рука, земля
static uint8_t __at(0xB561) sec_l;
static uint8_t __at(0xB562) sec_g;
static uint8_t __at(0xB563) eq_page;     // рабочая страница боя
static uint8_t __at(0xB564) eq_n;        // записей bitem_t

static far_t bat(uint8_t i)
{
	return FAR(eq_page + 1, TL_BITEMS) + (uint16_t)i * sizeof(bitem_t);
}

static uint8_t weight(uint16_t item)
{
	return (uint8_t)rtab_word(&ti, item, offsetof(r_items_t, weight));
}

static uint8_t rounds_of(uint16_t ammo)
{
	int16_t c = (int16_t)rtab_word(&ti, ammo, offsetof(r_items_t, clip_size));
	return c < 0 || c > 255 ? 255 : (uint8_t)c;
}

// ammo подходит оружию weapon (compatibleAmmo)
static uint8_t compat(uint16_t weapon, uint16_t ammo)
{
	r_items_t it;
	rtab_get(&ti, weapon, &it);
	for (uint8_t k = 0; k < it.compatible_ammo.n; k++) {
		uint16_t a;
		rtab_tail(&ti, it.compatible_ammo.off + k * 2, &a, 2);
		if (a == ammo) return 1;
	}
	return 0;
}

static uint8_t sec_named(uint16_t name)
{
	for (uint16_t i = 0; i < tv.n; i++) {
		uint16_t w = rtab_word(&tv, i, 0);   // сравнение — с копией (SDCC 4.5)
		if (w == name) return (uint8_t)i;
	}
	return 0xFF;
}

static void put(uint8_t owner, uint8_t slot, uint8_t x, uint8_t y, uint8_t z, const gitem_t *g)
{
	bitem_t b;
	if (eq_n >= BI_MAX) return;
	b.owner = owner; b.slot = slot; b.x = x; b.y = y; b.z = z;
	b.item = g->item; b.ammo = g->ammo; b.rounds = g->rounds;
	far_write(bat(eq_n++), &b, sizeof b);
}

// Предмет секции s бойца u занимает клетку прямоугольника [x, x + w) x [y, y + h)
// (Inventory::overlapItems)
static uint8_t overlap(uint8_t u, uint8_t s, uint8_t x, uint8_t y, uint8_t w, uint8_t h)
{
	bitem_t b;
	for (uint8_t i = 0; i < eq_n; i++) {
		far_read(bat(i), &b, sizeof b);
		if (b.owner != u || b.slot != s) continue;
		uint16_t wh = rtab_word(&ti, b.item, offsetof(r_items_t, inv_width));   // inv_width, inv_height
		uint8_t bw = (uint8_t)wh, bh = (uint8_t)(wh >> 8);
		if (!bw) bw = 1;
		if (!bh) bh = 1;
		if (b.x < x + w && x < b.x + bw && b.y < y + h && y < b.y + bh) return 1;
	}
	return 0;
}

// Первая свободная клетка секций-сеток, куда предмет w x h помещается целиком
// (RuleInventory::fitItemInSlot), в порядке секций и клеток правил
static uint8_t place_grid(uint8_t u, const gitem_t *g, uint8_t w, uint8_t h)
{
	for (uint8_t s = 0; s < tv.n; s++) {
		r_invs_t v;
		rtab_get(&tv, s, &v);
		if (v.type != 0) continue;
		for (uint8_t y = 0; y < v.rows; y++)
			for (uint8_t x = 0; x < v.cols; x++) {
				if (!(v.mask & (1u << (y * 4 + x)))) continue;
				uint8_t fit = 1;
				for (uint8_t yy = y; yy < y + h && fit; yy++)
					for (uint8_t xx = x; xx < x + w && fit; xx++)
						if (xx >= 4 || !(v.mask & (1u << (yy * 4 + xx)))) fit = 0;
				if (!fit || overlap(u, s, x, y, w, h)) continue;
				put(u, s, x, y, 0, g);
				return 1;
			}
	}
	return 0;
}

// _addItem для бойца X-COM: 1 — взял
static uint8_t add_item(const gitem_t *g, uint8_t u, uint8_t str, uint8_t second)
{
	r_items_t it;
	bitem_t b;
	uint16_t wt = 0, rw = 0xFFFF, lw = 0xFFFF;
	uint8_t tally = 0;
	for (uint8_t i = 0; i < eq_n; i++) {         // что уже несёт: вес, руки, такие же предметы
		far_read(bat(i), &b, sizeof b);
		if (b.owner != u) continue;
		wt += weight(b.item);
		if (b.ammo != 0xFFFF) wt += weight(b.ammo);
		if (b.item == g->item) tally++;
		uint8_t s = b.slot;
		if (s == sec_r) rw = b.item;
		if (s == sec_l) lw = b.item;
	}
	rtab_get(&ti, g->item, &it);
	wt += it.weight;
	if (g->ammo != 0xFFFF) wt += weight(g->ammo);
	if (!it.compatible_ammo.n) {                 // одного такого хватит (второй обоймы — в последнем проходе)
		if (second && it.battle_type == BT_AMMO) { if (tally >= 2) return 0; }
		else if (tally) return 0;
	}
	if (it.flags & ITEMS_F_FIXED_WEAPON) return 0;
	switch (it.battle_type) {
	case BT_FIREARM:
	case BT_MELEE:                               // заряженное — в правую руку
		if ((g->ammo != 0xFFFF || !it.compatible_ammo.n) && rw == 0xFFFF && (uint16_t)str * 66 >= wt * 100) {
			put(u, sec_r, 0, 0, 0, g);
			return 1;
		}
		return 0;
	case BT_AMMO:                                // обоймы — только к оружию в руках
		if (!((rw != 0xFFFF && compat(rw, g->item)) || (lw != 0xFFFF && compat(lw, g->item)))) return 0;
		/* fallthrough */
	default:
		if (str < wt) return 0;
		return place_grid(u, g, it.inv_width ? it.inv_width : 1, it.inv_height ? it.inv_height : 1);
	}
}

// _addItem для пришельца: оружие — в правую руку, занята — в левую; обойма заряжает оружие в
// руке, иначе — как прочее: в первую подходящую клетку секций (вес пришельцам не считается)
static void alien_item(uint8_t u, uint16_t item)
{
	r_items_t it;
	bitem_t b;
	gitem_t g;
	uint8_t ri = 0xFF, li = 0xFF;
	for (uint8_t i = 0; i < eq_n; i++) {
		far_read(bat(i), &b, sizeof b);
		if (b.owner != u) continue;
		uint8_t s = b.slot;
		if (s == sec_r) ri = i;
		if (s == sec_l) li = i;
	}
	rtab_get(&ti, item, &it);
	g.item = item; g.ammo = 0xFFFF; g.rounds = rounds_of(item); g.used = 0;
	if ((it.flags & ITEMS_F_FIXED_WEAPON) || it.battle_type == BT_FIREARM || it.battle_type == BT_MELEE) {
		if (ri == 0xFF) put(u, sec_r, 0, 0, 0, &g);
		else if (li == 0xFF) put(u, sec_l, 0, 0, 0, &g);
		return;
	}
	if (it.battle_type == BT_AMMO)
		for (uint8_t h = 0; h < 2; h++) {
			uint8_t i = h ? li : ri;
			if (i == 0xFF) continue;
			far_read(bat(i), &b, sizeof b);
			if (b.ammo != 0xFFFF || !compat(b.item, item)) continue;
			b.ammo = item;
			b.rounds = g.rounds;
			far_write(bat(i), &b, sizeof b);
			return;
		}
	place_grid(u, &g, it.inv_width ? it.inv_width : 1, it.inv_height ? it.inv_height : 1);
}

// Список уровня *lv из [n, предметы] x уровней (с байта off, элементов cnt) — в lst, вернёт n.
// Уровня нет — последний, и *lv урезается до него (как itemLevel в deployAliens)
static uint8_t pick_level(const rtab_t *t, uint16_t off, uint8_t cnt, uint8_t *lv, uint16_t *lst)
{
	uint8_t p = 0, l = 0;
	while (p < cnt) {
		uint16_t m;
		rtab_tail(t, off + p * 2, &m, 2);
		p++;
		uint8_t want = *lv;
		if (l == want || p + m >= cnt) {
			uint8_t n = m > 24 ? 24 : (uint8_t)m;
			if (n) rtab_tail(t, off + p * 2, lst, (uint16_t)n * 2);
			*lv = l;
			return n;
		}
		p += (uint8_t)m;
		l++;
	}
	return 0;
}

uint8_t be_aliens(uint8_t page, uint8_t nb, uint8_t first) __banked
{
	rtab_t tu, td;
	r_units_t ur;
	r_alienDeployments_t d;
	uint16_t dep, lst[24];
	far_t t2 = FAR(page + 1, 0);
	eq_page = page;
	eq_n = nb;
	uint8_t na = far_byte(t2 + TL_ANUM);
	far_read(t2 + TL_ADEP, &dep, 2);
	if (!na || !rtab_open(RES_RULE_ITEMS, &ti) || !rtab_open(RES_RULE_INVS, &tv) || !rtab_open(RES_RULE_UNITS, &tu)
	    || !rtab_open(RES_RULE_ALIENDEPLOYMENTS, &td) || dep >= td.n) return eq_n;
	sec_r = sec_named(STR_RIGHT_HAND);
	sec_l = sec_named(STR_LEFT_HAND);
	rtab_get(&td, dep, &d);
	for (uint8_t k = 0; k < na; k++) {
		unit_t a;
		far_read(t2 + TL_AUNITS + (uint16_t)k * sizeof a, &a, sizeof a);
		if (a.type >= tu.n) continue;
		rtab_get(&tu, a.type, &ur);
		uint8_t lv = a.ilevel, n;
		if (ur.built_in_weapons.n) {             // встроенное оружие — при любом снаряжении
			n = pick_level(&tu, ur.built_in_weapons.off, (uint8_t)ur.built_in_weapons.n, &lv, lst);
			for (uint8_t i = 0; i < n; i++) alien_item(first + k, lst[i]);
		}
		if (ur.living_weapon_item != 0xFFFF) { alien_item(first + k, ur.living_weapon_item); continue; }
		uint8_t p = 0, cnt = (uint8_t)d.item_sets.n;   // itemSets записи dset: записи до неё — мимо
		for (uint8_t e = 0; e <= a.dset && p < cnt; e++) {
			uint16_t nl;
			rtab_tail(&td, d.item_sets.off + p * 2, &nl, 2);
			p++;
			if (e == a.dset) {
				if (!nl) break;
				if (lv >= nl) lv = (uint8_t)(nl - 1);
				n = pick_level(&td, d.item_sets.off + p * 2, (uint8_t)(cnt - p), &lv, lst);
				for (uint8_t i = 0; i < n; i++) alien_item(first + k, lst[i]);
				break;
			}
			for (uint16_t l = 0; l < nl && p < cnt; l++) {
				uint16_t m;
				rtab_tail(&td, d.item_sets.off + p * 2, &m, 2);
				p += 1 + (uint8_t)m;
			}
		}
	}
	return eq_n;
}

uint8_t be_auto(const beq_t *q) __banked
{
	eq_page = q->page;
	eq_n = q->nb;
	if (!rtab_open(RES_RULE_ITEMS, &ti) || !rtab_open(RES_RULE_INVS, &tv)) return eq_n;
	sec_r = sec_named(STR_RIGHT_HAND);
	sec_l = sec_named(STR_LEFT_HAND);
	sec_g = sec_named(STR_GROUND);
	// Груз корабля — на землю в порядке std::map (sort_rank), каждая штука отдельно
	uint8_t ci[CARGO_ITEMS], cq[CARGO_ITEMS], nc = 0, ng = 0;
	uint8_t old = pg_map3(STATE_PAGE);
	uint8_t cg = q->craft < MAX_CRAFTS ? ST->craft[q->craft].cargo : NONE8;
	if (cg != NONE8 && cg < MAX_CARGO)
		for (uint8_t k = 0; k < CARGO_ITEMS; k++) {
			uint8_t it = ST->cargo[cg].it[k].item, n = ST->cargo[cg].it[k].qty;
			if (it != NONE8 && n) { ci[nc] = it; cq[nc] = n; nc++; }
		}
	pg_map3(old);
	for (uint8_t a = 1; a < nc; a++)                 // вставками по sort_rank
		for (uint8_t b = a; b && rtab_word(&ti, ci[b - 1], offsetof(r_items_t, sort_rank)) > rtab_word(&ti, ci[b], offsetof(r_items_t, sort_rank)); b--) {
			uint8_t x = ci[b]; ci[b] = ci[b - 1]; ci[b - 1] = x;
			x = cq[b]; cq[b] = cq[b - 1]; cq[b - 1] = x;
		}
	for (uint8_t k = 0; k < nc; k++)
		for (uint8_t c = 0; c < cq[k] && ng < GI_MAX; c++) {
			gi[ng].item = ci[k];
			gi[ng].ammo = 0xFFFF;
			gi[ng].rounds = rounds_of(ci[k]);
			gi[ng].used = 0;
			ng++;
		}
	// loadWeapons: оружие с земли заряжается первой подходящей обоймой с земли
	for (uint8_t i = 0; i < ng; i++) {
		r_items_t it;
		rtab_get(&ti, gi[i].item, &it);
		if (gi[i].used || (it.flags & ITEMS_F_FIXED_WEAPON) || !it.compatible_ammo.n
		    || (it.battle_type != BT_FIREARM && it.battle_type != BT_MELEE)) continue;
		for (uint8_t j = 0; j < ng; j++) {
			if (j == i || gi[j].used || !compat(gi[i].item, gi[j].item)) continue;
			gi[i].ammo = gi[j].item;
			gi[i].rounds = gi[j].rounds;
			gi[j].used = 1;
			break;
		}
	}
	// autoEquip: винтовки, пистолеты, обоймы, остальное (осветительные — только ночью: пока день)
	uint8_t str[MAX_UNITS];
	for (uint8_t u = 0; u < q->n && u < MAX_UNITS; u++) {
		str[u] = 0;
		uint8_t s = q->who[u];
		if (s == 0xFF) continue;
		soldier_t sd;
		far_read(FAR(SOLDIER_PAGE, 0) + (uint16_t)s * sizeof sd, &sd, sizeof sd);
		str[u] = sd.cur.strength;
	}
	for (uint8_t pass = 0; pass < 4; pass++)
		for (uint8_t i = 0; i < ng; i++) {
			if (gi[i].used) continue;
			r_items_t it;
			rtab_get(&ti, gi[i].item, &it);
			uint8_t arm = it.battle_type == BT_FIREARM || it.battle_type == BT_MELEE;
			uint8_t two = (it.flags & ITEMS_F_TWO_HANDED) != 0;
			uint8_t add = pass == 0 ? arm && two : pass == 1 ? arm && !two
				: pass == 2 ? it.battle_type == BT_AMMO : !arm && it.battle_type != BT_FLARE;
			if (!add) continue;
			for (uint8_t u = 0; u < q->n && u < MAX_UNITS; u++) {
				if (q->who[u] == 0xFF) continue;
				if (add_item(&gi[i], u, str[u], pass == 3)) { gi[i].used = 1; break; }
			}
		}
	for (uint8_t i = 0; i < ng; i++)                // не разобранное — на земле корабля
		if (!gi[i].used) put(BI_GROUND, sec_g, q->gx, q->gy, q->gz, &gi[i]);
	return eq_n;
}
