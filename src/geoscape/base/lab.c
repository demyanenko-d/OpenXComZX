// Исследования и производство (банк 8). По OpenXcom:
//   доступные темы — SavedGame::getAvailableResearchProjects;
//   завершение — GeoscapeState::time1Day + SavedGame::addFinishedResearch
//   (каскад бесплатных тем, getOneFree, lookup, «новые возможные» списки);
//   производство — SavedGame::getAvailableProductions, Production::step/startItem.
// Проекты — пулы ST->research / ST->prod (state.h).
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "far.h"
#include "res.h"
#include "res_ids.h"
#include "rules.h"
#include "state.h"
#include "game.h"

uint8_t lab_newres[LAB_LIST], lab_nnewres;   // для окон NewPossibleResearch/Manufacture
uint8_t lab_newman[LAB_LIST], lab_nnewman;

static rtab_t trs;                           // таблица research (открывается в входных функциях)
static r_research_t rr;
static uint8_t unl[RES_BITS];                // «разблокированные» (unlocks исследованного)
static uint8_t bits[RES_BITS];

#define BIT(a, i) (((a)[(i) >> 3] >> ((i) & 7)) & 1)
#define SETBIT(a, i) ((a)[(i) >> 3] |= (uint8_t)(1 << ((i) & 7)))

uint8_t res_done(uint16_t t) __banked
{
	return t < RES_BITS * 8 && BIT(ST->discovered, t);
}

static uint8_t done(uint16_t t) { return t < RES_BITS * 8 && BIT(ST->discovered, t); }

// Элементы списка из хвоста таблицы (до max)
static uint8_t rl(const rtab_t *t, const rlist_t *l, uint16_t *out, uint8_t max)
{
	uint8_t n = l->n > max ? max : l->n;
	if (n) rtab_tail(t, l->off, out, n * 2);
	return n;
}

// То же, но список пар (ref, количество): requiredItems и producedItems правил хранятся
// парами (refmap, OxzConv/Core/Rules.cs), и на пару приходится 4 байта. Раньше их читали
// как одиночные значения, и количество бралось из неинициализированного стека: на склад
// попадало мусорное число штук, материалы списывались как попало (ревизия 2026-09-26).
static uint8_t rl_pairs(const rtab_t *t, const rlist_t *l, uint16_t *out, uint8_t max)
{
	uint8_t n = l->n > max / 2 ? (uint8_t)(max / 2) : l->n;
	if (n) rtab_tail(t, l->off, out, (uint16_t)n * 4);
	return n;
}

// Все темы списка исследованы (пустой — да)
static uint8_t all_done(const rtab_t *t, const rlist_t *l)
{
	uint16_t v[32];
	uint8_t n = rl(t, l, v, 32);
	for (uint8_t i = 0; i < n; i++) if (!done(v[i])) return 0;
	return 1;
}

uint8_t res_list_done(uint16_t table, uint16_t rec, uint8_t off) __banked
{
	rtab_t t;
	rlist_t l;
	if (!rtab_open(table, &t) || rec >= t.n) return 0;
	far_read(t.base + 8 + (uint32_t)rec * t.size + off, &l, sizeof l);
	return all_done(&t, &l);
}

// Среди unlocks есть неисследованная тема с requires (hasUndiscoveredProtectedUnlock)
static uint8_t prot_unlock(const r_research_t *r)
{
	uint16_t v[32];
	r_research_t u;
	uint8_t n = rl(&trs, &r->unlocks, v, 32);
	for (uint8_t i = 0; i < n; i++) {
		if (v[i] >= trs.n) continue;
		rtab_get(&trs, v[i], &u);
		if (u.requires.n && !done(v[i])) return 1;
	}
	return 0;
}

static void make_unlocked(void)
{
	uint16_t v[32];
	memset(unl, 0, sizeof unl);
	for (uint8_t i = 0; i < trs.n && i < RES_BITS * 8; i++) {
		if (!done(i)) continue;
		rtab_get(&trs, i, &rr);
		uint8_t n = rl(&trs, &rr.unlocks, v, 32);
		for (uint8_t k = 0; k < n; k++) if (v[k] < RES_BITS * 8) SETBIT(unl, v[k]);
	}
}

// Проект темы на базе (пул ST->research) или NONE8
uint8_t research_find(uint8_t b, uint8_t topic) __banked
{
	for (uint8_t i = 0; i < MAX_RESEARCH; i++)
		if (ST->research[i].base == b && ST->research[i].topic == topic) return i;
	return NONE8;
}

// Доступные темы базы -> битовая карта bits (b = NONE8 — без проверок базы)
static uint8_t avail_bits(uint8_t b)
{
	uint8_t cnt = 0;
	make_unlocked();
	memset(bits, 0, sizeof bits);
	for (uint8_t i = 0; i < trs.n && i < RES_BITS * 8; i++) {
		rtab_get(&trs, i, &rr);
		if (!BIT(unl, i) && !all_done(&trs, &rr.dependencies)) continue;
		if (!all_done(&trs, &rr.requires)) continue;
		if (done(i) && all_done(&trs, &rr.get_one_free) && !prot_unlock(&rr)) continue;
		if (b != NONE8) {
			if (research_find(b, i) != NONE8) continue;
			if ((rr.flags & RESEARCH_F_NEED_ITEM) && (rr.item >= MAX_ITEMS || !ST->base[b].items[rr.item])) continue;
		}
		SETBIT(bits, i);
		cnt++;
	}
	return cnt;
}

// NewResearchListState::fillProjectList: доступные без requires, по порядку таблицы
uint8_t research_avail(uint8_t b, uint8_t *out, uint8_t max) __banked
{
	uint8_t n = 0;
	rtab_open(RES_RULE_RESEARCH, &trs);
	avail_bits(b);
	for (uint8_t i = 0; i < trs.n && i < RES_BITS * 8 && n < max; i++) {
		if (!BIT(bits, i)) continue;
		rtab_get(&trs, i, &rr);
		if (!rr.requires.n) out[n++] = i;
	}
	return n;
}

// ResearchInfoState(base, rule): проект сразу на базе, стоимость cost * RNG(50,150)/100;
// нужный предмет со склада уходит, если destroyItem.
uint8_t research_start(uint8_t b, uint8_t topic) __banked
{
	rtab_open(RES_RULE_RESEARCH, &trs);
	rtab_get(&trs, topic, &rr);
	for (uint8_t i = 0; i < MAX_RESEARCH; i++) {
		research_t *p = &ST->research[i];
		if (p->base != NONE8) continue;
		p->base = b;
		p->topic = topic;
		p->assigned = 0;
		p->spent = 0;
		p->cost = (uint16_t)((uint32_t)rr.cost * rng_range(50, 150) / 100);
		if ((rr.flags & RESEARCH_F_NEED_ITEM) && (rr.flags & RESEARCH_F_DESTROY_ITEM) && rr.item < MAX_ITEMS && ST->base[b].items[rr.item])
			ST->base[b].items[rr.item]--;
		return i;
	}
	return NONE8;
}

// Base::removeResearch: учёные — назад на базу; предмет — назад, если не доделан.
void research_cancel(uint8_t slot) __banked
{
	research_t *p = &ST->research[slot];
	if (p->base == NONE8) return;
	rtab_open(RES_RULE_RESEARCH, &trs);
	rtab_get(&trs, p->topic, &rr);
	ST->base[p->base].scientists += p->assigned;
	if ((rr.flags & RESEARCH_F_NEED_ITEM) && (rr.flags & RESEARCH_F_DESTROY_ITEM) && rr.item < MAX_ITEMS && p->spent < p->cost)
		ST->base[p->base].items[rr.item]++;
	p->base = NONE8;
}

// SavedGame::addFinishedResearch: тема и все ставшие доступными бесплатные темы
// (cost 0; с requires — только из unlocks текущей), очки месяца.
static void add_finished(uint8_t topic, uint8_t b)
{
	uint8_t q[48], qn = 0, qi = 0;
	uint16_t ul[32];
	q[qn++] = topic;
	while (qi < qn) {
		uint8_t cur = q[qi++];
		rtab_get(&trs, cur, &rr);
		uint8_t prot = prot_unlock(&rr);
		if (!done(cur)) {
			SETBIT(ST->discovered, cur);
			ST->fin.research[ST->hist_len - 1] += (int16_t)rr.points;
		} else if (!prot)
			continue;
		uint8_t nu = rl(&trs, &rr.unlocks, ul, 32);
		avail_bits(b);
		for (uint8_t i = 0; i < trs.n && i < RES_BITS * 8 && qn < sizeof q; i++) {
			if (!BIT(bits, i)) continue;
			rtab_get(&trs, i, &rr);
			if (rr.cost) continue;
			uint8_t k = 0;
			while (k < qn && q[k] != i) k++;
			if (k < qn) continue;
			if (!rr.requires.n) { q[qn++] = i; continue; }
			for (k = 0; k < nu; k++) if (ul[k] == i) { q[qn++] = i; break; }
		}
	}
}

static void lookup_add(uint8_t t, uint8_t b)
{
	rtab_get(&trs, t, &rr);
	if (rr.lookup < trs.n) add_finished((uint8_t)rr.lookup, b);
}

static void event(uint8_t kind, uint8_t base, uint16_t what, uint16_t qty)
{
	if (gev_n >= GEV_MAX) return;
	gev[gev_n].kind = kind; gev[gev_n].base = base; gev[gev_n].what = what; gev[gev_n].qty = qty;
	gev_n++;
}

static uint8_t before[RES_BITS];

// Проект slot закончен (GeoscapeState::time1Day, шаги 2–3j).
void research_finish(uint8_t slot) __banked
{
	research_t *p = &ST->research[slot];
	uint8_t b = p->base, topic = p->topic;
	uint16_t v[32];
	rtab_open(RES_RULE_RESEARCH, &trs);
	avail_bits(b);
	memcpy(before, bits, sizeof before);
	// 3a. снять проект с базы (учёные — назад)
	ST->base[b].scientists += p->assigned;
	p->base = NONE8;
	// 3c. getOneFree: случайная неисследованная (+ её lookup)
	uint16_t bonus = NONE16;
	rtab_get(&trs, topic, &rr);
	uint8_t n = rl(&trs, &rr.get_one_free, v, 32), m = 0;
	for (uint8_t i = 0; i < n; i++) if (!done(v[i])) v[m++] = v[i];
	if (m) {
		bonus = v[rng_range(0, m - 1)];
		add_finished((uint8_t)bonus, b);
		lookup_add((uint8_t)bonus, b);
	}
	// 3d. статья снова не всплывает, если тема (или её lookup) уже известна
	rtab_get(&trs, topic, &rr);
	uint16_t lk = rr.lookup, item = rr.item;
	uint8_t fresh = !done(lk < trs.n ? lk : topic);
	// 3e. сама тема и её lookup
	add_finished(topic, b);
	lookup_add(topic, b);
	event(GE_RESEARCH, b, topic | (fresh ? 0 : GE_OLD), bonus);
	// 3g. оружие исследовано раньше своих боеприпасов (ResearchRequiredState)
	if (fresh && item < MAX_ITEMS) {
		rtab_t ti, tm;
		rtab_open(RES_RULE_ITEMS, &ti);
		rtab_open(RES_RULE_MANUFACTURE, &tm);
		uint8_t bt = (uint8_t)rtab_word(&ti, item, offsetof(r_items_t, battle_type));
		rlist_t am;
		far_read(ti.base + 8 + (uint32_t)item * ti.size + offsetof(r_items_t, compatible_ammo), &am, sizeof am);
		uint16_t iname = rtab_word(&ti, item, 0);
		if (bt == 1 && am.n) {                    // BT_FIREARM
			uint16_t ammo;
			rtab_tail(&ti, am.off, &ammo, 2);
			for (uint8_t k = 0; k < tm.n; k++) {
				r_manufacture_t mf;
				rtab_get(&tm, k, &mf);
				if (mf.name != iname || !mf.requires.n) continue;
				// тема с тем же id, что у боеприпаса, среди requires и не всё исследовано
				uint8_t nr = rl(&tm, &mf.requires, v, 32), hit = 0;
				for (uint8_t j = 0; j < nr; j++) {
					if (v[j] >= trs.n) continue;
					if (rtab_word(&trs, v[j], offsetof(r_research_t, item)) == ammo) hit = 1;
				}
				if (hit && !all_done(&tm, &mf.requires)) event(GE_RESREQ, b, item, ammo);
				break;
			}
		}
	}
	// 3h. новые возможные исследования (без requires, ещё не показанные и не исследованные)
	avail_bits(b);
	for (uint8_t i = 0; i < trs.n && i < RES_BITS * 8; i++) {
		if (!BIT(bits, i) || BIT(before, i) || BIT(ST->popped, i) || done(i)) continue;
		rtab_get(&trs, i, &rr);
		if (rr.requires.n) continue;
		SETBIT(ST->popped, i);
		if (lab_nnewres < LAB_LIST) lab_newres[lab_nnewres++] = i;
	}
	if (lab_nnewres) event(GE_NEWRES, b, 0, 0);
	// 3i. новое производство: все requires исследованы и среди них — эта тема
	{
		rtab_t tm;
		r_manufacture_t mf;
		uint8_t any = 0;
		rtab_open(RES_RULE_MANUFACTURE, &tm);
		for (uint8_t k = 0; k < tm.n; k++) {
			rtab_get(&tm, k, &mf);
			uint8_t nr = rl(&tm, &mf.requires, v, 32), hit = 0;
			for (uint8_t j = 0; j < nr; j++) if (v[j] == topic) hit = 1;
			if (!hit || !all_done(&tm, &mf.requires)) continue;
			if (lab_nnewman < LAB_LIST) lab_newman[lab_nnewman++] = k;
			any = 1;
		}
		if (any) event(GE_NEWMAN, b, 0, 0);
	}
	// 3j. та же тема на других базах — снять, если больше ничего не даст
	rtab_get(&trs, topic, &rr);
	if (all_done(&trs, &rr.get_one_free) && !prot_unlock(&rr))
		for (uint8_t i = 0; i < MAX_RESEARCH; i++)
			if (ST->research[i].base != NONE8 && ST->research[i].topic == topic) research_cancel(i);
}

// Прогресс проекта (ResearchProject::getResearchProgress): 0 — нет учёных,
// 1 — неизвестно, 2..5 — плохо/средне/хорошо/отлично.
uint8_t research_progress(uint8_t slot) __banked
{
	research_t *p = &ST->research[slot];
	if (!p->assigned) return 0;
	if (!rtab_open(RES_RULE_RESEARCH, &trs)) return 1;
	uint16_t cost = rtab_word(&trs, p->topic, offsetof(r_research_t, cost));
	if (!cost || (uint32_t)p->spent * 1000 <= (uint32_t)cost * 333) return 1;
	uint32_t a = (uint32_t)p->assigned * 100;
	if (a <= (uint32_t)cost * 7) return 2;
	if (a <= (uint32_t)cost * 13) return 3;
	if (a <= (uint32_t)cost * 25) return 4;
	return 5;
}

// ---------------------------------------------------------------- производство

static rtab_t tmf;
static r_manufacture_t mr;

uint8_t prod_find(uint8_t b, uint8_t manuf) __banked
{
	for (uint8_t i = 0; i < MAX_PRODS; i++)
		if (ST->prod[i].base == b && ST->prod[i].manuf == manuf) return i;
	return NONE8;
}

// SavedGame::getAvailableProductions: requires исследованы, на базе ещё не идёт
uint8_t manuf_avail(uint8_t b, uint8_t *out, uint8_t max) __banked
{
	uint8_t n = 0;
	rtab_open(RES_RULE_MANUFACTURE, &tmf);
	for (uint8_t i = 0; i < tmf.n && n < max; i++) {
		rtab_get(&tmf, i, &mr);
		if (!all_done(&tmf, &mr.requires) || prod_find(b, i) != NONE8) continue;
		out[n++] = i;
	}
	return n;
}

static uint8_t craft_count(uint8_t b, uint8_t type)
{
	uint8_t n = 0;
	for (uint8_t c = 0; c < MAX_CRAFTS; c++)
		if (ST->craft[c].type == type && ST->craft[c].base == b && !ST->craft[c].transit) n++;
	return n;
}

// Сколько есть на базе того, что нужно (ref — items или RREF_ALT | crafts)
uint16_t manuf_have(uint8_t b, uint16_t ref) __banked
{
	if (ref & RREF_ALT) return craft_count(b, (uint8_t)(ref & 0xFF));
	return ref < MAX_ITEMS ? ST->base[b].items[ref] : 0;
}

// Production::haveEnoughMaterialsForOneMoreUnit
static uint8_t materials_ok(uint8_t b)
{
	uint16_t v[32];
	uint8_t n = rl_pairs(&tmf, &mr.required_items, v, 32);
	for (uint8_t i = 0; i + 1 < n * 2; i += 2)
		if (manuf_have(b, v[i]) < v[i + 1]) return 0;
	return 1;
}

static uint8_t money_ok(void) { return !mr.cost || ST->funds >= (int32_t)mr.cost; }

// Production::startItem: деньги, материалы и корабли — со склада базы
static void start_item(uint8_t b)
{
	uint16_t v[32];
	funds_add(-(int32_t)mr.cost);
	uint8_t n = rl_pairs(&tmf, &mr.required_items, v, 32);
	for (uint8_t i = 0; i + 1 < n * 2; i += 2) {
		uint16_t ref = v[i], q = v[i + 1];
		if (ref & RREF_ALT) {
			for (uint8_t c = 0; c < MAX_CRAFTS && q; c++)
				if (ST->craft[c].type == (ref & 0xFF) && ST->craft[c].base == b && !ST->craft[c].transit) { craft_remove(c); q--; }
		} else if (ref < MAX_ITEMS)
			ST->base[b].items[ref] -= ST->base[b].items[ref] < q ? ST->base[b].items[ref] : q;
	}
}

// ManufactureStartState: можно ли начать (деньги, место в мастерских, материалы)
uint8_t manuf_can_start(uint8_t b, uint8_t manuf) __banked
{
	caps_t a, u;
	rtab_open(RES_RULE_MANUFACTURE, &tmf);
	rtab_get(&tmf, manuf, &mr);
	base_caps(b, &a, &u);
	return money_ok() && a.workshops > u.workshops && materials_ok(b);
}

// Новое производство (ManufactureInfoState(base, item)): 1 штука, без инженеров;
// первая единица оплачивается при OK (prod_confirm).
uint8_t prod_new(uint8_t b, uint8_t manuf) __banked
{
	for (uint8_t i = 0; i < MAX_PRODS; i++) {
		prod_t *p = &ST->prod[i];
		if (p->base != NONE8) continue;
		memset(p, 0, sizeof *p);
		p->base = b;
		p->manuf = manuf;
		p->amount = 1;
		return i;
	}
	return NONE8;
}

void prod_confirm(uint8_t slot) __banked
{
	prod_t *p = &ST->prod[slot];
	rtab_open(RES_RULE_MANUFACTURE, &tmf);
	rtab_get(&tmf, p->manuf, &mr);
	start_item(p->base);
}

// Base::removeProduction: инженеры — назад
void prod_stop(uint8_t slot) __banked
{
	prod_t *p = &ST->prod[slot];
	if (p->base == NONE8) return;
	ST->base[p->base].engineers += p->engineers;
	p->base = NONE8;
}

uint16_t prod_done_units(uint8_t slot) __banked
{
	prod_t *p = &ST->prod[slot];
	rtab_open(RES_RULE_MANUFACTURE, &tmf);
	uint16_t t = rtab_word(&tmf, p->manuf, offsetof(r_manufacture_t, time));
	return t ? (uint16_t)(p->spent / t) : p->amount;
}

// Выпуск одной единицы: предметы — на склад или в продажу, корабль — в ангар
static void make_unit(prod_t *p)
{
	uint16_t v[32];
	uint8_t n = rl_pairs(&tmf, &mr.produced_items, v, 32);
	for (uint8_t i = 0; i + 1 < n * 2; i += 2) {
		uint16_t ref = v[i], q = v[i + 1];
		if (ref & RREF_ALT) { craft_add(p->base, (uint8_t)(ref & 0xFF), 0); break; }
		if (ref >= MAX_ITEMS) continue;
		if (p->flags & PF_SELL) funds_add(price(BUY_ITEM, (uint8_t)ref, 1) * (int32_t)q);
		else ST->base[p->base].items[ref] += q;
	}
}

// Час производства всех баз (Production::step + GeoscapeState::time1Hour)
void prod_hour(void) __banked
{
	rtab_open(RES_RULE_MANUFACTURE, &tmf);
	for (uint8_t i = 0; i < MAX_PRODS; i++) {
		prod_t *p = &ST->prod[i];
		if (p->base == NONE8) continue;
		rtab_get(&tmf, p->manuf, &mr);
		uint32_t t = mr.time;
		uint16_t done0 = t ? (uint16_t)(p->spent / t) : p->amount;
		p->spent += p->engineers;
		uint16_t now = t ? (uint16_t)(p->spent / t) : p->amount;
		uint8_t res = 0;                           // 0 — идёт, GE_* — снять
		if (done0 < now) {
			uint16_t produced = (p->flags & PF_INFINITE) ? now - done0 : (now < p->amount ? now : p->amount) - done0;
			for (uint16_t count = 0; count < produced;) {
				make_unit(p);
				if (++count < produced) {
					if (!money_ok()) { res = GE_NO_MONEY; break; }
					if (!materials_ok(p->base)) { res = GE_NO_MATERIALS; break; }
					start_item(p->base);
				}
			}
		}
		if (!res) {
			if (now >= p->amount && !(p->flags & PF_INFINITE)) res = GE_PRODUCTION;
			else if (done0 < now) {
				if (!money_ok()) res = GE_NO_MONEY;
				else if (!materials_ok(p->base)) res = GE_NO_MATERIALS;
				else start_item(p->base);
			}
		}
		if (res) {
			event(res, p->base, p->manuf, p->amount);
			prod_stop(i);
		}
	}
}
