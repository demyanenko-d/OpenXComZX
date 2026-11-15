// Мораль после гибели (банк 35): ba_morale (bact.h), вынесено из банка 36 — он полон.
#include <stdint.h>
#include "far.h"
#include "units.h"
#include "bact.h"

static void u_get(uint8_t i, unit_t *u)
{
	far_read(UNITS_FAR + (uint16_t)i * sizeof *u, u, sizeof *u);
}

static void u_set(uint8_t i, const unit_t *u)
{
	far_write(UNITS_FAR + (uint16_t)i * sizeof *u, u, sizeof *u);
}

static void mor_add(unit_t *x, int8_t d)
{
	int16_t m = (int16_t)x->mor + d;
	x->mor = m < 0 ? 0 : m > 100 ? 100 : (uint8_t)m;
}

// Мораль после гибели (checkForCasualties :672-729, модификатор званий — 100): убийце +20 за врага,
// −20 за своего, за мирного — бойцу −10, пришельцу +10; если погиб не мирный — его сторона теряет
// 2 · (110 − храбрость) / 10, остальные получают +10 (живые, 1x1). m — убийца (#FF — неизвестен)
void ba_morale(uint8_t victim, uint8_t m, uint8_t n) __banked
{
	unit_t v, x;
	u_get(victim, &v);
	if (m < n) {
		u_get(m, &x);
		uint8_t vf = v.faction, mf = x.faction;
		if (vf == FACTION_NEUTRAL) mor_add(&x, mf == FACTION_PLAYER ? -10 : 10);
		else if (vf == mf) mor_add(&x, -20);
		else mor_add(&x, 20);
		if (vf == FACTION_HOSTILE && mf == FACTION_PLAYER) ba_exp(m, EXP_KILLS);   // addKillCount
		u_set(m, &x);
		if (mf == FACTION_PLAYER && vf != FACTION_PLAYER) { v.done = 1; u_set(victim, &v); }   // killedBy
	}
	if (v.faction == FACTION_NEUTRAL) return;
	for (uint8_t i = 0; i < n; i++) {
		if (i == victim) continue;
		u_get(i, &x);
		if (!x.alive || x.size != 1) continue;
		if (x.faction == v.faction) mor_add(&x, (int8_t)-(2 * ((110 - x.bravery) / 10)));
		else mor_add(&x, 10);
		u_set(i, &x);
	}
}
