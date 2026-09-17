// Новая игра (банк 7): Mod::newSave OpenXcom (tmp/state_model.md §2).
// Страны — финансирование RNG(base, 2*base) тыс., выровненное к initialFunding;
// стартовая база, корабли, склад, персонал — из RES_RULE_VARS; солдаты — genSoldier.
// База ещё не поставлена: имя пустое, months = -1 (ставит BuildNewBase + BaseName).
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "tsconf.h"
#include "pages.h"
#include "far.h"
#include "res.h"
#include "res_ids.h"
#include "rules.h"
#include "text.h"
#include "state.h"
#include "game.h"

extern volatile uint16_t frames;          // crt0.s
vars_t gv;                                // глобальные значения правил (не сохраняются)
static uint8_t vbuf[512];
static uint16_t vp;

static uint8_t v8(void) { return vbuf[vp++]; }
static uint16_t v16(void) { uint16_t v = vbuf[vp] | (vbuf[vp + 1] << 8); vp += 2; return v; }
static uint32_t v32(void) { uint32_t v = v16(); return v | ((uint32_t)v16() << 16); }

// Начало стартовой базы в vars (после глобальных значений) — для game_new.
static uint16_t vars_base;

void vars_load(void) __banked
{
	res_t r;
	memset(vbuf, 0, sizeof vbuf);
	if (res_find(RES_RULE_VARS, &r))
		far_read(r.phys, vbuf, r.size < sizeof vbuf ? (uint16_t)r.size : sizeof vbuf);
	vp = 8;                                   // время старта — в game_new
	gv.initial_funding = v32();
	gv.cost_engineer = v32();
	gv.cost_scientist = v32();
	gv.time_personnel = v16();
	gv.alien_fuel = v16();
	gv.alien_fuel_amount = v16();
	for (uint8_t i = 0; i < 5; i++) gv.diff_coef[i] = v8();
	gv.defeat_score = (int32_t)v32();
	gv.defeat_funds = (int32_t)v32();
	vars_base = vp;
}

// Пустые пулы: NONE8 в полях «свободно» (st_clear даёт нули).
static void init_pools(void)
{
	for (uint8_t b = 0; b < MAX_BASES; b++)
		for (uint8_t f = 0; f < MAX_FACILITIES; f++) ST->base[b].fac[f].type = NONE8;
	for (uint8_t i = 0; i < MAX_CRAFTS; i++) { ST->craft[i].type = NONE8; ST->craft[i].cargo = NONE8; }
	for (uint8_t i = 0; i < MAX_CARGO; i++) {
		for (uint8_t k = 0; k < CARGO_ITEMS; k++) ST->cargo[i].it[k].item = NONE8;
		for (uint8_t k = 0; k < CARGO_VEH; k++) ST->cargo[i].veh[k].type = NONE8;
	}
	for (uint8_t i = 0; i < MAX_TRANSFERS; i++) ST->transfer[i].base = NONE8;
	for (uint8_t i = 0; i < MAX_RESEARCH; i++) ST->research[i].base = NONE8;
	for (uint8_t i = 0; i < MAX_PRODS; i++) ST->prod[i].base = NONE8;
	for (uint8_t i = 0; i < MAX_UFOS; i++) ST->ufo[i].type = NONE8;
	for (uint8_t i = 0; i < MAX_MISSIONS; i++) ST->mission[i].type = NONE8;
	soldiers_clear();
}

void game_new(uint8_t difficulty, uint8_t ironman) __banked
{
	rtab_t t;
	st_clear();
	init_pools();
	vars_load();
	df_reset();
	ST->difficulty = difficulty;
	ST->ironman = ironman;
	ST->months = -1;
	ST->hist_len = 1;
	// под сценарием эмулятора — постоянное зерно: тесты не зависят от тайминга загрузки
	ST->rng = 0x9E3779B9ul;
	if (OXZ_DBG_SCRIPT != OXZ_SCRIPT_SIG) ST->rng ^= ((uint32_t)frames << 7) ^ frames;
	ST->sel_base = 0;

	// время старта (vars: sec, min, hour, weekday, day, month, year)
	ST->second = vbuf[0]; ST->minute = vbuf[1]; ST->hour = vbuf[2];
	ST->weekday = vbuf[3]; ST->day = vbuf[4]; ST->month = vbuf[5];
	ST->year = vbuf[6] | (vbuf[7] << 8);

	// страны: RNG(base, 2*base) тыс., затем выравнивание к initialFunding (Mod.cpp:1645-1662)
	rtab_open(RES_RULE_COUNTRIES, &t);
	uint8_t nc = t.n > MAX_COUNTRIES ? MAX_COUNTRIES : (uint8_t)t.n;
	int32_t sum = 0;
	for (uint8_t i = 0; i < nc; i++) {
		uint16_t fb = rtab_word(&t, i, offsetof(r_countries_t, funding_base));
		ST->country[i].funding[0] = rng_range(fb, fb * 2);
		sum += ST->country[i].funding[0];
	}
	if (nc) {
		int16_t missing = (int16_t)(((int32_t)gv.initial_funding - sum) / nc);
		sum = 0;
		for (uint8_t i = 0; i < nc; i++) {
			int32_t f = (int32_t)ST->country[i].funding[0] + missing;
			if (f >= 0) ST->country[i].funding[0] = (uint16_t)f;
			sum += ST->country[i].funding[0];
		}
	}
	ST->funds = sum * 1000;
	ST->fin.balance[0] = ST->funds;

	strategy_init();                          // AlienStrategy::init: веса регионов и их миссий

	// стартовая база (base 0, ещё без имени и места)
	base_t *b = &ST->base[0];
	vp = vars_base;
	b->scientists = v16();
	b->engineers = v16();
	uint16_t nsold = v16();
	uint8_t nf = v8();
	for (uint8_t i = 0; i < nf && i < MAX_FACILITIES; i++) {
		b->fac[i].type = (uint8_t)v16();
		uint8_t x = v8(), y = v8();
		b->fac[i].xy = x | (y << 4);
		b->fac[i].days = 0;
	}
	uint8_t ncr = v8(), transport = NONE8;
	rtab_open(RES_RULE_CRAFTS, &t);
	for (uint8_t i = 0; i < ncr && i < MAX_CRAFTS; i++) {
		craft_t *c = &ST->craft[i];
		c->type = (uint8_t)v16();
		c->num = v16();
		c->fuel = v16();
		c->damage = v16();
		c->base = 0;
		c->status = CS_READY;
		c->weap[0].type = c->weap[1].type = NONE8;
		uint8_t nw = v8();
		for (uint8_t k = 0; k < nw; k++) {
			uint8_t wt = (uint8_t)v16();
			uint16_t am = v16();
			if (k < 2) { c->weap[k].type = wt; c->weap[k].ammo = am; }
		}
		if (ST->ids[ID_CRAFT + c->type] < c->num) ST->ids[ID_CRAFT + c->type] = c->num;
		uint8_t soldiers = (uint8_t)rtab_word(&t, c->type, offsetof(r_crafts_t, soldiers));
		uint8_t ni = v8();
		if (soldiers && transport == NONE8) transport = i;
		if (soldiers || ni) c->cargo = cargo_alloc();
		for (uint8_t k = 0; k < ni; k++) {
			uint8_t it = (uint8_t)v16();
			uint16_t q = v16();
			cargo_add(c->cargo, it, q);
		}
		uint8_t nv = v8();
		for (uint8_t k = 0; k < nv; k++) {
			uint8_t it = (uint8_t)v16();
			int16_t am = (int16_t)v16();
			if (c->cargo != NONE8 && k < CARGO_VEH) { ST->cargo[c->cargo].veh[k].type = it; ST->cargo[c->cargo].veh[k].ammo = am; }
		}
	}
	uint16_t nit = v16();
	for (uint16_t i = 0; i < nit; i++) {
		uint8_t it = (uint8_t)v16();
		uint16_t q = v16();
		if (it < MAX_ITEMS) b->items[it] += q;
	}

	// солдаты: первые — в транспорт, пока есть места (Mod.cpp:1683-1779)
	uint8_t seats = transport != NONE8 ? (uint8_t)rtab_word(&t, ST->craft[transport].type, offsetof(r_crafts_t, soldiers)) : 0;
	for (uint16_t i = 0; i < nsold; i++) {
		uint8_t s = soldier_new(0);
		if (s != NONE8 && seats) {
			soldier_t so;
			soldier_get(s, &so);
			so.craft = transport;
			soldier_put(s, &so);
			seats--;
		}
	}
}
