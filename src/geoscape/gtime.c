// Время кампании (банк 8): часы и периодические обработчики GeoscapeState
// (timeAdvance, time5Seconds … time1Month; tmp/state_model.md §3.7, 14_todo §3.1).
// Время идёт шагами по 5 с, как в OpenXcom, но макрошагами: до ближайшей границы
// 10 минут или события НЛО (прибытие, взлёт) — между ними объекты просто сдвигаются.
// На границе — каскад месяц → день → час → 30 мин → 10 мин, затем шаг 5 с.
// Событие для окна останавливает ход до его показа (как timerReset; в OpenXcom
// остаток хода досчитывается, окна — после).
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "pages.h"
#include "far.h"
#include "res.h"
#include "rules.h"
#include "state.h"
#include "game.h"

gevent_t gev[GEV_MAX];                   // очередь событий для окон (Win1)
uint8_t gev_n;

static void event(uint8_t kind, uint8_t base, uint16_t what, uint16_t qty)
{
	if (gev_n >= GEV_MAX) return;
	gev[gev_n].kind = kind;
	gev[gev_n].base = base;
	gev[gev_n].what = what;
	gev[gev_n].qty = qty;
	gev_n++;
}

void gev_push(uint8_t kind, uint8_t base, uint16_t what, uint16_t qty) __banked
{
	event(kind, base, what, qty);
}

static uint8_t leap(uint16_t y) { return (!(y & 3) && (y % 100)) || !(y % 400); }
static const uint8_t mdays[12] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };

// ---------------------------------------------------------------- корабли

static uint16_t craft_word(rtab_t *t, uint8_t type, uint8_t off) { return rtab_word(t, type, off); }

// Craft::checkup: после прибытия — ремонт, перевооружение или заправка.
void craft_checkup(uint8_t c) __banked
{
	rtab_t tw;
	craft_t *cr = &ST->craft[c];
	if (cr->damage) { cr->status = CS_REPAIR; return; }
	rtab_open(RES_RULE_CRAFTWEAPONS, &tw);
	for (uint8_t k = 0; k < 2; k++)
		if (cr->weap[k].type != NONE8 && cr->weap[k].ammo < rtab_word(&tw, cr->weap[k].type, offsetof(r_craftWeapons_t, ammo_max))) {
			cr->status = CS_REARM;
			return;
		}
	cr->status = CS_REFUEL;
}

static void refuel(void);

// time30Minutes: миссии, обломки, заправка, обнаружение НЛО и очки, места миссий
static void t30min(void)
{
	alien_30min();
	ufo_30min();
	refuel();
	ufo_detect_all();
	sites_30min();
}

static void refuel(void)
{
	rtab_t t, tw;
	rtab_open(RES_RULE_CRAFTS, &t);
	rtab_open(RES_RULE_CRAFTWEAPONS, &tw);
	for (uint8_t c = 0; c < MAX_CRAFTS; c++) {
		craft_t *cr = &ST->craft[c];
		if (cr->type == NONE8 || cr->transit || cr->status != CS_REFUEL) continue;
		// Craft::refuel: +refuelRate; с предметом-топливом — 1 предмет за тик; предмета
		// нет — окно один раз (_lowFuel), с остатком топлива — готов, без — «мало топлива»
		uint16_t fmax = craft_word(&t, cr->type, offsetof(r_crafts_t, fuel_max));
		uint8_t rate = (uint8_t)craft_word(&t, cr->type, offsetof(r_crafts_t, refuel_rate));
		uint16_t item = craft_word(&t, cr->type, offsetof(r_crafts_t, refuel_item));
		cr = &ST->craft[c];
		if (cr->fuel < fmax) {
			uint8_t add = 1;
			if (item < MAX_ITEMS) {
				if (ST->base[cr->base].items[item]) { ST->base[cr->base].items[item]--; cr->flags &= ~CRF_LOWFUEL; }
				else {
					add = 0;
					if (!(cr->flags & CRF_LOWFUEL)) {
						event(GE_NO_FUEL, cr->base, c, item);
						if (cr->fuel) cr->status = CS_READY; else cr->flags |= CRF_LOWFUEL;
					}
				}
			}
			if (add) cr->fuel = cr->fuel + rate >= fmax ? fmax : cr->fuel + rate;
		}
		if (cr->fuel >= fmax) {
			cr->status = CS_READY;
			for (uint8_t k = 0; k < 2; k++)
				if (cr->weap[k].type != NONE8 && cr->weap[k].ammo < rtab_word(&tw, cr->weap[k].type, offsetof(r_craftWeapons_t, ammo_max)))
					cr->status = CS_REARM;
		}
	}
}

// Craft::rearm: первое оружие, которому нужны снаряды, обоймы — со склада.
static void craft_rearm(uint8_t c)
{
	rtab_t tw, ti;
	craft_t *cr = &ST->craft[c];
	rtab_open(RES_RULE_CRAFTWEAPONS, &tw);
	rtab_open(RES_RULE_ITEMS, &ti);
	for (uint8_t k = 0; k < 2; k++) {
		uint8_t w = cr->weap[k].type;
		if (w == NONE8) continue;
		uint16_t amax = rtab_word(&tw, w, offsetof(r_craftWeapons_t, ammo_max));
		if (cr->weap[k].ammo >= amax) continue;
		uint8_t rate = (uint8_t)rtab_word(&tw, w, offsetof(r_craftWeapons_t, rearm_rate));
		uint16_t clip = rtab_word(&tw, w, offsetof(r_craftWeapons_t, clip));
		uint16_t add = rate;
		if (clip < MAX_ITEMS) {
			int16_t cs = (int16_t)rtab_word(&ti, clip, offsetof(r_items_t, clip_size));
			if (cs <= 0) cs = 1;
			uint16_t need = amax - cr->weap[k].ammo;
			uint16_t clips = ((rate < need ? rate : need) + cs - 1) / cs;
			uint16_t have = ST->base[cr->base].items[clip];
			if (!have) { event(GE_NO_AMMO, cr->base, c, clip); cr->status = CS_REFUEL; return; }
			if (clips > have) clips = have;
			ST->base[cr->base].items[clip] -= clips;
			add = clips * cs;
		}
		cr->weap[k].ammo = cr->weap[k].ammo + add > amax ? amax : cr->weap[k].ammo + add;
		return;
	}
	cr->status = CS_REFUEL;
}

static void t1hour(void)
{
	rtab_t t;
	rtab_open(RES_RULE_CRAFTS, &t);
	for (uint8_t c = 0; c < MAX_CRAFTS; c++) {
		craft_t *cr = &ST->craft[c];
		if (cr->type == NONE8) continue;
		if (cr->transit) {                         // корабль в пути на базу
			if (!--cr->transit) { event(GE_ARRIVED, cr->base, TK_CRAFT_ARRIVED, c); craft_checkup(c); }
			continue;
		}
		if (cr->status == CS_REPAIR) {
			uint8_t rr = (uint8_t)rtab_word(&t, cr->type, offsetof(r_crafts_t, repair_rate));
			if (!rr) rr = 1;
			cr->damage = cr->damage > rr ? cr->damage - rr : 0;
			if (!cr->damage) cr->status = CS_REARM;
		} else if (cr->status == CS_REARM)
			craft_rearm(c);
	}
	// поставки
	for (uint8_t i = 0; i < MAX_TRANSFERS; i++) {
		transfer_t *tr = &ST->transfer[i];
		if (tr->base == NONE8) continue;
		if (tr->hours > 1) { tr->hours--; continue; }
		base_t *b = &ST->base[tr->base];
		if (tr->kind == TK_ITEM && tr->item < MAX_ITEMS) b->items[tr->item] += tr->qty;
		else if (tr->kind == TK_SCIENTIST) b->scientists += tr->qty;
		else if (tr->kind == TK_ENGINEER) b->engineers += tr->qty;
		event(GE_ARRIVED, tr->base, tr->kind == TK_ITEM ? tr->item : (tr->kind == TK_SCIENTIST ? TK_SCI_ARRIVED : TK_ENG_ARRIVED), tr->qty);
		tr->base = NONE8;
	}
	// солдаты в пути
	{
		uint8_t n = ST->nsoldiers;
		soldier_t s;
		for (uint8_t i = 0; i < n; i++) {
			soldier_get(i, &s);
			if (s.base == NONE8 || !s.transit) continue;
			if (!--s.transit) event(GE_ARRIVED, s.base, TK_SOLDIER_ARRIVED, i);
			soldier_put(i, &s);
		}
	}
	prod_hour();
	sites_hour();                                // первое не обнаруженное место миссии — окно
}

// ---------------------------------------------------------------- исследования

static void t1day(void)
{
	// стройка
	for (uint8_t b = 0; b < MAX_BASES; b++) {
		if (!ST->base[b].name[0]) continue;
		for (uint8_t f = 0; f < MAX_FACILITIES; f++) {
			facility_t *fc = &ST->base[b].fac[f];
			if (fc->type == NONE8 || !fc->days) continue;
			if (!--fc->days) event(GE_BUILT, b, fc->type, 0);
		}
	}
	// исследования (ResearchProject::step): spent += assigned, готово при spent >= cost
	for (uint8_t i = 0; i < MAX_RESEARCH; i++) {
		research_t *r = &ST->research[i];
		if (r->base == NONE8) continue;
		r->spent += r->assigned;
	}
	for (uint8_t i = 0; i < MAX_RESEARCH; i++) {
		research_t *r = &ST->research[i];
		if (r->base != NONE8 && r->spent >= r->cost) { research_finish(i); ST->speed = 0; }   // timerReset
	}
	// лечение: солдаты баз (Base::getSoldiers — и на кораблях), в пути — нет
	uint8_t n = ST->nsoldiers;
	soldier_t s;
	for (uint8_t i = 0; i < n; i++) {
		soldier_get(i, &s);
		if (s.base == NONE8 || s.transit || !s.recovery) continue;
		s.recovery--;
		soldier_put(i, &s);
	}
	abases_day();                                // очки баз пришельцев, снабжение
	uint8_t d = ST->day;                         // автосохранение Ironman 10-го и 20-го
	if (d == 10 || d == 20) st_ironsave();
}

// ---------------------------------------------------------------- часы

// Сдвинуть часы на s секунд (s < 600, без перехода через границу 10 минут).
static void clock_add(uint16_t s)
{
	uint16_t sec = ST->second + s;
	while (sec >= 60) { sec -= 60; ST->minute++; }
	ST->second = (uint8_t)sec;
	if (ST->minute < 60) return;
	ST->minute -= 60;
	if (++ST->hour < 24) return;
	ST->hour = 0;
	ST->weekday = ST->weekday % 7 + 1;
	uint8_t md = mdays[ST->month - 1] + (ST->month == 2 && leap(ST->year));
	if (++ST->day <= md) return;
	ST->day = 1;
	if (++ST->month > 12) { ST->month = 1; ST->year++; }
}

// Ход времени на sec + min*60 секунд (кратно 5). Возвращает 1, если появились события
// (остаток хода отброшен — как timerReset при окне).
uint8_t game_advance(uint16_t sec, uint16_t min) __banked
{
	// шагов по 5 с: (sec + min · 60) / 5 = sec / 5 + min · 12 — без 32-битного деления (~2600 такт
	// на каждый ход времени) и счётчик 16-битный (сутки — 17 280 шагов)
	uint16_t left = sec / 5 + min * 12;
	if (ST->months < 0) return 0;              // до первой базы время стоит
	while (left) {
		// шагов до границы 10 минут (граница — на последнем шаге)
		uint16_t to10 = 120 - ((ST->minute % 10) * 12 + ST->second / 5);   // то же без деления на 5
		uint16_t k = left < to10 ? left : to10;
		k = ufo_limit(k);                        // прибытие / взлёт — не раньше k-го шага
		k = craft_limit(k);
		if (k > 1) { clock_add((k - 1) * 5); ufo_advance(k - 1); craft_advance(k - 1); }
		clock_add(5);
		left -= k;
		if (!ST->second && !(ST->minute % 10)) {
			// граница: TIME_1MONTH -> 1DAY -> 1HOUR -> 30MIN -> 10MIN (switch без break)
			uint8_t m30 = !(ST->minute % 30), h = m30 && !ST->minute, d = h && !ST->hour;
			if (d && ST->day == 1) {
				month_end();                     // month.c (миссии месяца — там же)
				event(GE_MONTH, 0, 0, 0);
				abases_month();
			}
			if (d) t1day();
			if (h) t1hour();
			if (m30) t30min();
			craft_10min();                       // time10Minutes: топливо, патруль
			bases_10min();
		}
		ufo_step();                              // time5Seconds: НЛО, затем корабли
		craft_step();
		if (gev_n) return 1;
	}
	return gev_n != 0;
}

// Первая база названа: начало кампании (GeoscapeState::init при months == -1:
// addMonth, determineAlienMissions). Аренда построек и кораблей — сразу, без зарплат.
void game_start(void) __banked
{
	costs_t c;
	ST->months = 0;
	alien_month();
	base_costs(0, &c);
	funds_add(-(int32_t)(c.facilities + c.crafts));
}
