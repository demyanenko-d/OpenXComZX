// Предметы боя (банк 33): список bitem_t в рабочей странице боя (loader.h TL_BITEMS).
// Порт той части BattlescapeGenerator::deployXCOM (REF/OpenXcom/src/Battlescape/
// BattlescapeGenerator.cpp:645-796), что раскладывает снаряжение по бойцам: placeItemByLayout —
// раскладка бойца в кампании (inv_t, state.h) уже готова, её просто переносим. autoEquip (бойцу без
// раскладки — оружие из груза корабля) — вместе с грузом на земле; пока в отладочном бою без
// экипажа бойцу даётся первое доступное без исследований оружие с обоймой.
#include <stdint.h>
#include <string.h>
#include "far.h"
#include "pages.h"
#include "res.h"
#include "rules.h"
#include "res_ids.h"
#include "str_ids.h"
#include "state.h"
#include "loader.h"
#include "units.h"
#include "bitems.h"

static uint8_t bi_n;                     // занято записей

static far_t bi_at(uint8_t page, uint8_t i)
{
	return FAR(page + 1, TL_BITEMS) + (uint16_t)i * sizeof(bitem_t);
}

// Секция инвентаря по названию (STR_RIGHT_HAND и т. п.), #FF — нет
static uint8_t sec_named(uint16_t name)
{
	rtab_t t;
	if (!rtab_open(RES_RULE_INVS, &t)) return 0xFF;
	for (uint16_t i = 0; i < t.n; i++) {
		uint16_t w = rtab_word(&t, i, 0);    // сравнение — с копией (SDCC 4.5)
		if (w == name) return (uint8_t)i;
	}
	return 0xFF;
}

// Отладочное оружие: первое ручное огнестрельное без исследований, что продаётся, и его обойма
static uint8_t debug_gun(uint16_t *gun, uint16_t *clip, uint8_t *rounds)
{
	rtab_t t;
	r_items_t it;
	if (!rtab_open(RES_RULE_ITEMS, &t)) return 0;
	for (uint16_t i = 0; i < t.n; i++) {
		rtab_get(&t, i, &it);
		if (it.battle_type != BT_FIREARM || it.requires.n || !it.cost_buy || !it.compatible_ammo.n
		    || (it.flags & ITEMS_F_FIXED_WEAPON)) continue;   // не оружие танка
		uint16_t a;
		rtab_tail(&t, it.compatible_ammo.off, &a, 2);
		*gun = i; *clip = a;
		rtab_get(&t, a, &it);
		*rounds = it.clip_size > 0 ? (uint8_t)it.clip_size : 0;
		return 1;
	}
	return 0;
}

static void hands_build(uint8_t page, uint8_t n);

uint8_t bi_crew(uint8_t page, const uint8_t *sol, uint8_t n) __banked
{
	bitem_t b;
	bi_n = 0;
	far_fill(FAR(page + 1, TL_BITEMS), BI_NONE, (uint16_t)BI_MAX * sizeof(bitem_t));
	uint8_t old = pg_map3(STATE_PAGE);
	uint16_t ninv = ST->ninv;
	pg_map3(old);
	uint8_t rh = sec_named(STR_RIGHT_HAND);
	for (uint8_t u = 0; u < n; u++) {
		uint8_t got = 0, s = sol[u];
		if (s != 0xFF)
			for (uint16_t k = 0; k < ninv && bi_n < BI_MAX; k++) {
				inv_t v;
				far_read(FAR(INV_PAGE, 0) + k * sizeof v, &v, sizeof v);
				if (v.soldier != s) continue;
				b.owner = u; b.slot = v.slot; b.x = v.x; b.y = v.y;
				b.item = v.item; b.ammo = v.ammo;
				b.rounds = v.rounds > 255 ? 255 : (uint8_t)v.rounds;
				b.z = 0;
				far_write(bi_at(page, bi_n++), &b, sizeof b);
				got = 1;
			}
		if (!got && s == 0xFF && rh != 0xFF && bi_n < BI_MAX) {   // отладочный бой
			uint16_t gun, clip;
			uint8_t r;
			if (debug_gun(&gun, &clip, &r)) {
				b.owner = u; b.slot = rh; b.x = b.y = 0; b.z = 0;
				b.item = gun; b.ammo = clip; b.rounds = r;
				far_write(bi_at(page, bi_n++), &b, sizeof b);
			}
		}
	}
	hands_build(page, n);
	return bi_n;
}

// Оружие в руках для вида (UnitSprite::drawRoutine0: itemR / itemL — кадр HANDOB.PCK
// handSprite + направление): у бойца — слоты кадров предметов в руках (loader.h TL_UHS), кадры
// слота — 8 направлений; пиксели нужных кадров копируются подряд в свою страницу (TL_HANDPG):
// весь HANDOB TFTD (440 кадров) держать в бою не на что. HANDOB читается в слот кэша SD.
static void hands_build(uint8_t page, uint8_t n)
{
	far_t t = FAR(page + 1, 0);
	uint8_t hp;
	far_read(t + TL_HANDPG, &hp, 1);
	far_fill(t + TL_UHS, 0, MAX_UNITS * 2);
	far_fill(t + TL_HAND, 0, HAND_MAX * 8 * TE_SIZE);
	res_t r;
	if (hp == PG_NONE || !res_find(RES_HANDOB_PCK, &r)) return;
	uint16_t nfr = far_word(r.phys), pos = 0;
	far_t fdata = r.phys + 4 + (uint32_t)nfr * 6;
	int16_t spr[HAND_MAX];
	uint8_t ns = 0;
	if (n > MAX_UNITS) n = MAX_UNITS;
	for (uint8_t u = 0; u < n; u++) {
		uint8_t hs[2];
		hs[0] = hs[1] = 0;
		for (uint8_t side = 0; side < 2; side++) {   // 0 — правая рука, 1 — левая
			bitem_t b;
			rtab_t it_t;
			r_items_t it;
			if (!bi_hand(page, u, side == 0, &b) || !rtab_open(RES_RULE_ITEMS, &it_t) || b.item >= it_t.n) continue;
			rtab_get(&it_t, b.item, &it);
			int16_t hsp = it.hand_sprite;
			if (hsp < 0) continue;
			uint8_t s = 0;
			while (s < ns && spr[s] != hsp) s++;
			if (s == ns) {
				if (ns == HAND_MAX) continue;
				for (uint8_t d = 0; d < 8; d++) {
					uint16_t fr = (uint16_t)hsp + d;
					uint8_t e6[6], te[TE_SIZE];
					memset(te, 0, TE_SIZE);
					if (fr < nfr) {
						far_read(r.phys + 4 + (uint32_t)fr * 6, e6, 6);
						uint16_t sz = (uint16_t)e6[2] * e6[3];
						if (sz && pos + sz <= 16384) {   // ширина кадра SPRSET чётная (DMA словами)
							far_copy(FAR(hp, pos), fdata + ((uint32_t)(e6[4] | (e6[5] << 8)) << 1), sz);
							te[0] = hp; te[1] = (uint8_t)pos; te[2] = (uint8_t)(pos >> 8);
							te[3] = (uint8_t)(e6[2] / 2 - 1); te[4] = e6[3];
							te[5] = e6[0]; te[6] = e6[1]; te[7] = e6[2];
							pos += (sz + 1) & ~1u;
						}
					}
					far_write(t + TL_HAND + (uint16_t)(ns * 8 + d) * TE_SIZE, te, TE_SIZE);
				}
				spr[ns++] = hsp;
			}
			hs[side] = (uint8_t)(s + 1) | ((it.flags & ITEMS_F_TWO_HANDED) ? 0x80 : 0);
		}
		far_write(t + TL_UHS + u * 2, hs, 2);
	}
}

uint8_t bi_spend(uint8_t page, uint8_t unit, uint8_t right) __banked
{
	uint8_t h = sec_named(right ? STR_RIGHT_HAND : STR_LEFT_HAND);
	bitem_t b;
	for (uint8_t i = 0; i < bi_n; i++) {
		far_read(bi_at(page, i), &b, sizeof b);
		if (b.owner != unit || b.slot != h) continue;
		if (b.rounds) { b.rounds--; far_write(bi_at(page, i), &b, sizeof b); }
		return b.rounds;
	}
	return 0;
}

uint8_t bi_hand(uint8_t page, uint8_t unit, uint8_t right, bitem_t *out) __banked
{
	uint8_t h = sec_named(right ? STR_RIGHT_HAND : STR_LEFT_HAND);
	if (h == 0xFF) return 0;
	for (uint8_t i = 0; i < bi_n; i++) {
		far_read(bi_at(page, i), out, sizeof *out);
		if (out->owner == unit && out->slot == h) return 1;
	}
	return 0;
}
