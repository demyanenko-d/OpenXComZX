// Конец месяца (банк 16): GeoscapeState::time1Month — пси-тренировка, SavedGame::
// monthlyFunding, MonthlyReportState::calculateChanges (Country::newMonth, очки совета,
// поражение). Итоги — report (Win1) для окна отчёта. Всё о закрываемом месяце
// считается до сдвига историй (при полной истории PUSH сдвигает индексы).
// Миссии месяца — alien_month (alien.c), 20 % обнаружения базы пришельцев агентами —
// abases_month (ufo.c, из gtime.c), pactScore — pact_score (alien.c).
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "res.h"
#include "rules.h"
#include "state.h"
#include "game.h"

report_t report;

// Сдвиг истории: новый месяц — последний элемент (не больше HIST).
#define PUSH(arr, v) do { if (ST->hist_len < HIST) arr[ST->hist_len] = (v); else { memmove(arr, arr + 1, sizeof(arr[0]) * (HIST - 1)); arr[HIST - 1] = (v); } } while (0)

// Soldier::trainPsi (без anytimePsiTraining и allowPsiStrengthImprovement).
static void train_psi(soldier_t *s, const r_soldiers_t *r)
{
	uint8_t sk = s->cur.psi_skill, mx = r->max_stats.psi_skill, cap = r->stat_caps.psi_skill, imp = 0;
	if (sk + 10 < r->min_stats.psi_skill) sk = r->min_stats.psi_skill;
	else if (sk <= mx) imp = (uint8_t)rng_range(mx, mx + mx / 2);
	else if (sk <= cap / 2) imp = (uint8_t)rng_range(5, 12);
	else if (sk < cap) imp = (uint8_t)rng_range(1, 3);
	sk += imp;
	s->cur.psi_skill = sk > cap ? cap : sk;
	s->psi_improve = imp;
}

// Базы с пси-лабораторией: тренировка отмеченных солдат. 1 — есть такие базы (окно PsiTraining).
static uint8_t psi_month(void)
{
	rtab_t t;
	r_soldiers_t r;
	caps_t a, u;
	soldier_t s;
	uint8_t any = 0, n = ST->nsoldiers;
	rtab_open(RES_RULE_SOLDIERS, &t);
	for (uint8_t b = 0; b < MAX_BASES; b++) {
		if (!ST->base[b].name[0]) continue;
		base_caps(b, &a, &u);
		if (!a.psi) continue;
		any = 1;
		for (uint8_t i = 0; i < n; i++) {
			soldier_get(i, &s);
			if (s.base != b || s.transit || !(s.flags & SF_PSI)) continue;
			rtab_get(&t, s.type, &r);
			train_psi(&s, &r);
			soldier_put(i, &s);
		}
	}
	return any;
}

void month_end(void) __banked
{
	rtab_t tc;
	uint16_t nf[MAX_COUNTRIES];
	uint8_t m = ST->hist_len - 1;
	ST->months++;                                 // addMonth
	alien_month();                                // determineAlienMissions
	ST->speed = 0;                                // timerReset
	memset(&report, 0, sizeof report);
	if (psi_month()) report.flags |= RF_PSI;

	// SavedGame::monthlyFunding: деньги не через funds_add — в доход/расход месяца не идут
	rtab_open(RES_RULE_COUNTRIES, &tc);
	uint8_t nc = tc.n > MAX_COUNTRIES ? MAX_COUNTRIES : (uint8_t)tc.n;
	int32_t funding = 0, maint = total_maintenance();
	for (uint8_t i = 0; i < nc; i++) funding += (int32_t)ST->country[i].funding[m] * 1000;
	ST->funds += funding - maint;
	ST->fin.maintenance[m] = maint;
	ST->fin.balance[m] = ST->funds;

	// calculateChanges: очки регионов, прошлый месяц, бонус совета +400 со второго месяца
	int32_t last = 0, xcom = 0, alien = 0;
	for (uint8_t r = 0; r < MAX_REGIONS; r++) {
		if (m) last += ST->region[r].act_xcom[m - 1] - ST->region[r].act_alien[m - 1];
		xcom += ST->region[r].act_xcom[m];
		alien += ST->region[r].act_alien[m];
	}
	if (ST->months > 1) ST->fin.research[m] += 400;
	xcom += ST->fin.research[m];
	if (m) last += ST->fin.research[m - 1];

	// Country::newMonth(xcomTotal, alienTotal, pactScore)
	int16_t pact = pact_score();
	for (uint8_t i = 0; i < nc; i++) {
		country_t *c = &ST->country[i];
		int32_t f = c->funding[m], cap = rtab_word(&tc, i, offsetof(r_countries_t, funding_cap));
		int16_t good = (int16_t)(xcom / 10) + c->act_xcom[m], bad = (int16_t)(alien / 20) + c->act_alien[m];
		int32_t delta = f * rng_range(5, 20) / 100;
		uint8_t sat = 2;
		if (bad <= good + 30) {
			if (good > bad + 30 && (int16_t)rng_range(0, good) > bad) {
				if (f + delta > cap) delta = cap - f;    // выше потолка — и вниз до потолка (как в OpenXcom)
				if (delta) sat = 3;
			}
		} else if ((int16_t)rng_range(0, bad) > good && delta) {
			delta = f < delta ? -f : -delta;
			if (delta) sat = 1;
		}
		if ((c->flags & CF_NEWPACT) && !(c->flags & CF_PACT)) {
			report.pact |= 1u << i;
			c->flags = (c->flags & ~CF_NEWPACT) | CF_PACT;
			c->act_alien[m] += pact;                  // addActivityAlien(pactScore): закрываемый месяц
		}
		nf[i] = (c->flags & CF_PACT) ? 0 : (uint16_t)(sat != 2 ? f + delta : f);
		report.diff += ((int32_t)nf[i] - f) * 1000;
		report.income += (int32_t)nf[i] * 1000;
		if (sat == 1) report.sad |= 1u << i;
		else if (sat == 3) report.happy |= 1u << i;
	}
	report.rating = xcom - alien;
	report.threshold = gv.defeat_score + 100 * gv.diff_coef[ST->difficulty < 5 ? ST->difficulty : 4];
	report.maintenance = maint;

	// поражение: два месяца подряд рейтинг не выше порога; долг — со второго раза
	uint8_t reset = 1;
	if (last <= report.threshold && report.rating <= report.threshold) report.flags |= RF_GAME_OVER;
	else if (ST->funds <= gv.defeat_funds) {
		if (ST->warned) report.flags |= RF_GAME_OVER;
		else { report.flags |= RF_DEBTS; ST->warned = 1; reset = 0; }
	}
	if (reset) ST->warned = 0;
	if (report.flags & RF_GAME_OVER) { report.happy = report.sad = report.pact = 0; ST->ending = END_LOSE; }

	// новый месяц: истории (доход — финансирование стран до изменения, расход — содержание)
	for (uint8_t i = 0; i < nc; i++) {
		country_t *c = &ST->country[i];
		PUSH(c->funding, nf[i]);
		PUSH(c->act_xcom, 0);
		PUSH(c->act_alien, 0);
	}
	for (uint8_t r = 0; r < MAX_REGIONS; r++) { PUSH(ST->region[r].act_xcom, 0); PUSH(ST->region[r].act_alien, 0); }
	PUSH(ST->fin.balance, ST->funds);
	PUSH(ST->fin.maintenance, 0);
	PUSH(ST->fin.income, funding);
	PUSH(ST->fin.expenditure, maint);
	PUSH(ST->fin.research, 0);
	if (ST->hist_len < HIST) ST->hist_len++;
}
