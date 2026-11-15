// Пси-атака пришельца (банк 35): AIModule::psiAction (AIModule.cpp:1884-1990) и
// PsiAttackBState::psiAttack (PsiAttackBState.cpp:131-218).
//
// Пришелец с пси-навыком (оружие ALIEN_PSI_WEAPON — у нас первое пси-оружие правил без исследований:
// tuUse 25, flatRate) раз за ход выбирает известного бойца X-COM 1x1 с наибольшим шансом
// атака − 0.4 · навык цели − расстояние − сила цели + 55..105 (атака = навык · сила / 50); с оружием
// в руке — только если мощность его патронов ниже шанса, без оружия — если 35..155 ниже шанса.
// Удар: атака − расстояние + 0..55 против силы цели + 10 (+ навык / 5): паника — минус мораль
// 110 − храбрость (сама паника — в начале хода X-COM). Контроля разума у нас нет: где оригинал
// выбрал бы его (controlOdds), тоже паника.
#include <stdint.h>
#include <stddef.h>
#include "far.h"
#include "res.h"
#include "res_ids.h"
#include "rules.h"
#include "units.h"
#include "str_ids.h"
#include "game.h"
#include "bitems.h"
#include "bai.h"
#include "bpsi.h"

#define BT_PSIAMP 9
#define TSS_FAR FAR(0xED, 0x3F40)        // ai_tss (bai.h) — память банка 36, страница #ED

static uint16_t isqrt16(uint16_t v)
{
	uint16_t r = 0;
	for (uint16_t b = 0x80; b; b >>= 1) if ((r | b) * (r | b) <= v) r |= b;
	return r;
}

// Пси-атака бойца a пси-усилителем (BA_PANIC 12 / BA_MINDCONTROL 11; PsiAttackBState::init, psiAttack)
// по юниту не X-COM в клетке (tx, ty, tz), время tu. Вернёт строку для InfoboxState при успехе,
// STR_NOT_ENOUGH_TIME_UNITS — времени нет, 0 — не вышло или некого (время не тратится, если некого)
uint16_t psi_player(uint8_t a, uint8_t tx, uint8_t ty, uint8_t tz, uint8_t act, uint8_t tu, uint8_t n) __banked
{
	unit_t u, x;
	uint8_t tg = 0xFF;
	for (uint8_t i = 0; i < n && tg == 0xFF; i++) {
		far_read(UNITS_FAR + (uint16_t)i * sizeof x, &x, sizeof x);
		uint8_t f = x.faction;
		if (x.alive && f != FACTION_PLAYER && x.z == tz && (uint8_t)(tx - x.x) < x.size && (uint8_t)(ty - x.y) < x.size) tg = i;
	}
	if (tg == 0xFF) return 0;
	uint8_t xf = x.faction;
	if (act == 11 && xf != FACTION_HOSTILE) return 0;   // контроль — только пришельцев
	far_read(UNITS_FAR + (uint16_t)a * sizeof u, &u, sizeof u);
	if (u.tu < tu) return STR_NOT_ENOUGH_TIME_UNITS;
	u.tu -= tu;
	far_write(UNITS_FAR + (uint16_t)a * sizeof u, &u, sizeof u);
	int16_t dx = (int16_t)x.x - u.x, dy = (int16_t)x.y - u.y, dz = (int16_t)x.z - u.z;
	int16_t s = (int16_t)((uint16_t)u.psi_str * u.psi_skill / 50) - (int16_t)isqrt16((uint16_t)(dx * dx + dy * dy + dz * dz))
		+ (int16_t)rng_range(0, 55);
	int16_t def = x.psi_str + (x.psi_skill ? 10 + x.psi_skill / 5 : 10) + (act == 11 ? 20 : 0);
	if (s <= def) return 0;
	if (act == 11) {                         // convertToFaction: к X-COM до начала следующего хода X-COM
		x.faction = FACTION_PLAYER;
		x.tu = x.tu_max;
	} else {
		int16_t loss = 110 - x.bravery;
		x.mor = loss >= x.mor ? 0 : (uint8_t)(x.mor - loss);
	}
	far_write(UNITS_FAR + (uint16_t)tg * sizeof x, &x, sizeof x);
	return act == 11 ? STR_MIND_CONTROL_SUCCESSFUL : STR_MORALE_ATTACK_SUCCESSFUL;
}

uint8_t ai_psi(const aictx_t *c, uint8_t a) __banked
{
	rtab_t t, tu;
	r_items_t w;
	unit_t u, x;
	far_read(UNITS_FAR + (uint16_t)a * sizeof u, &u, sizeof u);
	// под контролем (боец за пришельцев) — не атакует пси (originalFaction == faction)
	if (!u.psi_skill || u.type == 0xFFFF || !rtab_open(RES_RULE_ITEMS, &t)) return 0;
	uint16_t pw;
	for (pw = 0; pw < t.n; pw++) {           // ALIEN_PSI_WEAPON
		rtab_get(&t, pw, &w);
		if (w.battle_type == BT_PSIAMP && !w.requires.n) break;
	}
	if (pw >= t.n) return 0;
	uint8_t cost = (w.flags & ITEMS_F_FLAT_RATE) ? w.tu_use : (uint8_t)((uint16_t)u.tu_max * w.tu_use / 100);
	if (u.tu <= cost) return 0;
	uint8_t intel = rtab_open(RES_RULE_UNITS, &tu) && u.type < tu.n
		? (uint8_t)rtab_word(&tu, u.type, offsetof(r_units_t, intelligence)) : 0;
	int16_t atk = (int16_t)((uint16_t)u.psi_skill * u.psi_str / 50), best = 0;
	uint8_t tg = 0xFF;
	for (uint8_t i = 0; i < c->nsold; i++) {
		far_read(UNITS_FAR + (uint16_t)i * sizeof x, &x, sizeof x);
		uint8_t f = x.faction;               // сравнения — с копиями (SDCC 4.5)
		if (!x.alive || x.size != 1 || f != FACTION_PLAYER || far_byte(TSS_FAR + i) > intel) continue;
		int16_t dx = (int16_t)x.x - u.x, dy = (int16_t)x.y - u.y, dz = (int16_t)x.z - u.z;
		int16_t ch = atk - (int16_t)(x.psi_skill * 2 / 5) - (int16_t)isqrt16((uint16_t)(dx * dx + dy * dy + dz * dz))
			- x.psi_str + (int16_t)rng_range(55, 105);
		if (ch > best) { best = ch; tg = i; }
	}
	if (tg == 0xFF || best <= 0) return 0;
	bitem_t b;                               // с оружием: мощность патронов против шанса
	uint8_t armed = 0;
	for (uint8_t h = 0; h < 2 && !armed; h++)
		if (bi_hand(c->page, a, h, &b) && b.ammo != 0xFFFF && b.ammo < t.n) {
			r_items_t am;
			rtab_get(&t, b.ammo, &am);
			if ((int16_t)am.power >= best) return 0;
			armed = 1;
		}
	if (!armed && (int16_t)rng_range(35, 155) >= best) return 0;
	far_read(UNITS_FAR + (uint16_t)tg * sizeof x, &x, sizeof x);
	uint8_t mc = 0;                          // контроль или паника (:1953-1986)
	if (best >= 30) {
		int8_t odds = 40;
		uint8_t br = (uint8_t)((110 - x.bravery) / 10), m = x.mor;
		if (br > 6) odds -= 15;
		if (br < 4) odds += 15;
		if (m >= 40) { if ((int16_t)m - 10 * br < 50) odds -= 15; }
		else odds += 15;
		if (!m) odds = 100;
		mc = rng_percent((uint8_t)odds);
	}
	u.tu -= cost;
	far_write(UNITS_FAR + (uint16_t)a * sizeof u, &u, sizeof u);
	int16_t dx = (int16_t)x.x - u.x, dy = (int16_t)x.y - u.y, dz = (int16_t)x.z - u.z;
	int16_t s = atk - (int16_t)isqrt16((uint16_t)(dx * dx + dy * dy + dz * dz)) + (int16_t)rng_range(0, 55);
	int16_t def = x.psi_str + (x.psi_skill ? 10 + x.psi_skill / 5 : 10) + (mc ? 20 : 0);
	if (s <= def) return 1;
	if (mc) {                                // convertToFaction: за пришельцев, время заново
		x.faction = FACTION_HOSTILE;
		x.tu = x.tu_max;
		x.kneel = 0;
	} else {                                 // паника: мораль − (110 − храбрость)
		int16_t loss = 110 - x.bravery;
		x.mor = loss >= x.mor ? 0 : (uint8_t)(x.mor - loss);
	}
	far_write(UNITS_FAR + (uint16_t)tg * sizeof x, &x, sizeof x);
	return 1;
}
