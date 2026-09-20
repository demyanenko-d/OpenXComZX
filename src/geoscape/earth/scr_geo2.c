// Банк 4: сообщения геоскейпа (ResearchCompleteState, NewPossibleResearchState,
// NewPossibleManufactureState, ProductionCompleteState, ItemsArrivingState,
// AlienBaseState, BaseDefenseState, BaseDestroyedState, ConfirmLandingState,
// ConfirmCydoniaState, PsiTrainingState, AllocatePsiTrainingState,
// ResearchRequiredState). Бой — scr_dogf.c (банк 22).
// Сообщения хода времени — из ev_cur / arrivals (scr_geo.c: show_event), пси —
// солдаты баз (state.h). Высадка и оборона базы — пока заглушки.
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "tsconf.h"
#include "res.h"
#include "rules.h"
#include "text.h"
#include "gfx.h"
#include "pages.h"
#include "game.h"
#include "scrdef.h"

static char t1[80], t2[80];

static uint16_t rec_name(uint16_t table, uint16_t i)
{
	rtab_t t;
	if (!rtab_open(table, &t) || !t.n) return 0xFFFF;
	return rtab_word(&t, i % t.n, 0);
}

// ---------------------------------------------------------------- исследования, производство

static const wdef_t w_rescomplete[] = {
	WINP(45, 30, 230, 140, UI_EL_WINDOW, POPB),
	BTN(64, 146, 80, 16, UI_EL_BUTTON, STR_OK, A_POP, 0, ESC),
	BTN(176, 146, 80, 16, UI_EL_BUTTON, STR_VIEW_REPORTS, A_CUSTOM, 3, ENT),
	TXT(45, 70, 230, 17, UI_EL_TEXT1, STR_RESEARCH_COMPLETED, BIG | TC),
	TXT(45, 96, 230, 32, UI_EL_TEXT2, DYN(0), BIG | TC | TW),
};

static const wdef_t w_newresearch[] = {
	WIN(16, 10, 288, 180, UI_EL_WINDOW),
	BTN(80, 149, 160, 14, UI_EL_BUTTON, STR_OK, A_POP, 0, ESC),
	BTN(80, 165, 160, 14, UI_EL_BUTTON, STR_ALLOCATE_RESEARCH, A_CUSTOM, 1, ENT),
	TXT(16, 20, 288, 40, UI_EL_TEXT1, STR_WE_CAN_NOW_RESEARCH, BIG | TC),
	LST(35, 50, 250, 96, UI_EL_TEXT2, 0, BIG | TC),
};

static const wdef_t w_newmanuf[] = {
	WIN(16, 10, 288, 180, UI_EL_WINDOW),
	BTN(80, 149, 160, 14, UI_EL_BUTTON, STR_OK, A_POP, 0, ESC),
	BTN(80, 165, 160, 14, UI_EL_BUTTON, STR_ALLOCATE_MANUFACTURE, A_CUSTOM, 1, ENT),
	TXT(16, 20, 288, 40, UI_EL_TEXT1, STR_WE_CAN_NOW_PRODUCE, BIG | TC),
	LST(35, 50, 250, 80, UI_EL_TEXT2, 0, BIG | TC),
};

static const wdef_t w_proddone[] = {
	WINP(32, 20, 256, 160, UI_EL_WINDOW, POPB),
	BTN(40, 154, 118, 18, UI_EL_BUTTON, STR_OK, A_POP, 0, ESC),
	BTN(162, 154, 118, 18, UI_EL_BUTTON, DYN(1), A_CUSTOM, 1, 0),
	TXT(37, 35, 246, 110, UI_EL_TEXT1, DYN(0), BIG | TC | TM | TW),
};

static const wdef_t w_items[] = {
	WINP(0, 8, 320, 184, UI_EL_WINDOW, POPB),
	BTN(16, 166, 142, 16, UI_EL_BUTTON, STR_OK, A_POP, 0, ESC),
	BTN(162, 166, 142, 16, UI_EL_BUTTON, STR_GO_TO_BASE, A_CUSTOM, 1, ENT),
	TXT(5, 18, 310, 17, UI_EL_TEXT1, STR_ITEMS_ARRIVING, BIG | TC),
	TXT(16, 34, 114, 9, UI_EL_TEXT1, STR_ITEM, 0),
	TXT(152, 34, 54, 9, UI_EL_TEXT1, STR_QUANTITY_UC, 0),
	TXT(212, 34, 112, 9, UI_EL_TEXT1, STR_DESTINATION_UC, 0),
	LST(14, 50, 271, 112, UI_EL_TEXT2, 0, WF_SEL),
};

// ---------------------------------------------------------------- базы

// В AlienBaseState номера элементов у кнопки и заголовка перепутаны (как в OpenXcom).
static const wdef_t w_alienbase[] = {
	WIN(0, 0, 320, 200, UI_EL_WINDOW),
	BTN(135, 180, 50, 12, UI_EL_TEXT, STR_OK, A_CUSTOM, 1, ESC),   // центр на базу, 5 с
	TXT(6, 60, 308, 60, UI_EL_BUTTON, DYN(0), BIG | TC | TW),
	HOT(0, 0, 0, 0, A_CUSTOM, 1, ENT),
};

// BaseDefenseState: оборона по очереди (готовые постройки с defense), грависщит — ещё
// круг, НЛО уничтожено — три взрыва; шаг — таймер 250 мс (выстрел — 333 мс). OK и
// ENTER видны только в конце (geo2_get прячет их).
static const wdef_t w_basedef[] = {
	WIN(0, 0, 320, 200, UI_EL_WINDOW),
	BTN(100, 170, 120, 18, UI_EL_BUTTON, STR_OK, A_CUSTOM, 1, ESC),
	TXT(16, 6, 300, 17, UI_EL_TEXT, DYN(0), BIG),
	TXT(16, 24, 300, 10, UI_EL_TEXT, DYN(2), 0),
	LST(16, 40, 300, 128, UI_EL_TEXT, 1, 0),
	HOT(0, 0, 0, 0, A_CUSTOM, 1, ENT),
};
#define BD_NONE    0
#define BD_FIRE    1
#define BD_RESOLVE 2
#define BD_DESTROY 3
#define BD_END     4
#define BD_ROWS    15                          // видимых строк (дальше — сдвиг, scrollDown)
#define BDR_SHIELD 0xFE
#define BDR_DEAD   0xFD
static struct { uint8_t kind, fire, res; } bd_row[BD_ROWS];   // kind — тип постройки / BDR_*; res 1 мимо, 2 попал
static uint8_t bd_def[MAX_FACILITIES], bd_ndef, bd_grav, bd_passes, bd_attacks, bd_action, bd_expl, bd_nrows, bd_wait;
static int8_t bd_cycles;
static uint16_t bd_at;
static uint8_t bd_real;                        // штурм настоящий (отладочное меню — без последствий)
extern volatile uint16_t frames;               // crt0.s

static void bd_add(uint8_t kind)
{
	if (bd_nrows == BD_ROWS) {
		memmove(bd_row, bd_row + 1, sizeof bd_row[0] * (BD_ROWS - 1));
		bd_nrows--;
		ui_dirty(1);
	}
	uint8_t r = bd_nrows++;
	bd_row[r].kind = kind; bd_row[r].fire = bd_row[r].res = 0;
	ui_dirty_row(1, r);
}

// BaseDefenseState::nextStep
static void bd_step(void)
{
	rtab_t t;
	if (bd_cycles < 0) return;
	if (!bd_cycles) { bd_cycles = 1; ui_dirty(2); return; }   // «Base defenses initiated»
	switch (bd_action) {
	case BD_DESTROY:
		if (!bd_expl) bd_add(BDR_DEAD);
		if (++bd_expl == 3) bd_action = BD_END;
		return;
	case BD_END:                                 // OK видна
		bd_cycles = -1;
		UI_GO(A_REDRAW, 0);
		return;
	}
	uint8_t at = bd_attacks, nd = bd_ndef, ps = bd_passes, gs = bd_grav;   // SDCC 4.5: сравнения — копиями
	if (at == nd) {
		if (ps == gs) { bd_action = BD_END; return; }
		bd_add(BDR_SHIELD);                      // грависщит: ещё круг
		bd_passes++;
		bd_attacks = 0;
		return;
	}
	uint8_t type = ST->base[ctx.base].fac[bd_def[bd_attacks]].type, r = bd_nrows - 1;
	switch (bd_action) {
	case BD_NONE:
		bd_add(type);
		bd_action = BD_FIRE;
		return;
	case BD_FIRE:
		bd_row[r].fire = 1;
		ui_dirty_row(1, r);
		bd_wait = 17;
		bd_action = BD_RESOLVE;
		return;
	case BD_RESOLVE: {
		r_facilities_t f;
		rtab_open(RES_RULE_FACILITIES, &t);
		rtab_get(&t, type, &f);
		uint8_t hit = rng_percent(f.hit_ratio);
		bd_row[r].res = hit ? 2 : 1;
		ui_dirty_row(1, r);
		ufo_t *u = &ST->ufo[ctx.ufo];
		if (u->type != NONE8) {
			if (hit) {                           // Ufo::setDamage
				uint16_t dmg = f.defense / 2 + rng_range(0, f.defense);
				rtab_open(RES_RULE_UFOS, &t);
				uint16_t mx = rtab_word(&t, ST->ufo[ctx.ufo].type, offsetof(r_ufos_t, damage_max));
				u = &ST->ufo[ctx.ufo];
				u->damage += dmg;
				if (u->damage >= mx) u->status = US_DESTROYED;
				else if (u->damage > mx / 2) u->status = US_CRASHED;
			}
			if (ST->ufo[ctx.ufo].status == US_DESTROYED) bd_action = BD_DESTROY;
			else bd_action = BD_NONE;
		} else
			bd_action = BD_NONE;
		bd_attacks++;
		bd_wait = 13;
		return;
	}
	}
}

// GeoscapeState::handleBaseDefense: НЛО своё отработало; защитники — бой на базе
// (не перенесён), нет — база уничтожена. op — A_PUSH / A_POP_PUSH.
static void assault(uint8_t op)
{
	if (ST->ufo[ctx.ufo].type != NONE8) ST->ufo[ctx.ufo].status = US_DESTROYED;
	if (base_defenders(ctx.base)) {
		bd_real = 0;
		strcpy(ui_msg, "BASE DEFENSE\x02\nnot ported yet");
		UI_GO(op, SCR_ERROR);
		return;
	}
	bd_real = 1;
	UI_GO(op, SCR_BASE_DESTROYED);
}

// time5Seconds: штурм базы ctx.base НЛО ctx.ufo — оборона, если есть чем, иначе сразу
// handleBaseDefense (setupDefenses: готовые постройки с defense)
void geo_base_attack(void) __banked
{
	rtab_t t;
	rtab_open(RES_RULE_FACILITIES, &t);
	for (uint8_t i = 0; i < MAX_FACILITIES; i++) {
		facility_t *fc = &ST->base[ctx.base].fac[i];
		if (fc->type == NONE8 || fc->days) continue;
		if (rtab_word(&t, fc->type, offsetof(r_facilities_t, defense))) { bd_real = 1; UI_GO(A_PUSH, SCR_BASE_DEFENSE); return; }
	}
	assault(A_PUSH);
}

static const wdef_t w_basedestroyed[] = {
	WIN(32, 20, 256, 160, UI_EL_WINDOW),
	BTN(110, 142, 100, 20, UI_EL_BUTTON, STR_OK, A_CUSTOM, 1, ESC),
	TXT(48, 76, 224, 48, UI_EL_TEXT, DYN(0), BIG | TC | TW),
};

// ---------------------------------------------------------------- высадка

static const wdef_t w_landing[] = {
	WINP(20, 20, 216, 160, UI_EL_WINDOW, POPB),
	BTN(40, 150, 80, 20, UI_EL_BUTTON, STR_YES, A_CUSTOM, 2, ENT),
	BTN(136, 150, 80, 20, UI_EL_BUTTON, STR_NO, A_CUSTOM, 3, ESC),
	TXT(25, 40, 206, 80, UI_EL_TEXT, DYN(0), BIG | TC | TW),
	TXT(25, 130, 206, 17, UI_EL_TEXT, DYN(1), BIG | TC),
};

static const wdef_t w_cydonia[] = {
	WIN(32, 20, 256, 160, UI_EL_WINDOW),
	BTN(70, 142, 80, 20, UI_EL_BUTTON, STR_YES, A_CUSTOM, 2, ENT),
	BTN(170, 142, 80, 20, UI_EL_BUTTON, STR_NO, A_POP, 0, ESC),
	TXT(48, 76, 224, 48, UI_EL_TEXT, STR_ARE_YOU_SURE_CYDONIA, BIG | TC | TW),
};

// ---------------------------------------------------------------- пси-тренировка

// Кнопки баз с пси-лабораторией добавляет geo2_get (по одной, 80, 40 + 16 * i).
static const wdef_t w_psi[] = {
	WIN(0, 0, 320, 200, UI_EL_WINDOW),
	BTN(80, 174, 160, 14, UI_EL_BUTTON2, STR_OK, A_POP, 0, ESC),
	TXT(10, 16, 300, 17, UI_EL_TEXT, STR_PSIONIC_TRAINING, BIG | TC),
};

static const wdef_t w_allocpsi[] = {
	WIN(0, 0, 320, 200, UI_EL_WINDOW),
	BTN(80, 174, 160, 14, UI_EL_BUTTON, STR_OK, A_POP, 0, ESC),
	TXT(10, 40, 64, 10, UI_EL_TEXT, STR_NAME, 0),
	TXT(10, 8, 300, 17, UI_EL_TEXT, STR_PSIONIC_TRAINING, BIG | TC),
	TXT(10, 24, 300, 10, UI_EL_TEXT, DYN(0), 0),
	TXT(124, 32, 80, 20, UI_EL_TEXT, STR_PSIONIC__STRENGTH, 0),
	TXT(188, 32, 80, 20, UI_EL_TEXT, STR_PSIONIC_SKILL_IMPROVEMENT, 0),
	TXT(270, 32, 48, 20, UI_EL_TEXT, STR_IN_TRAINING, 0),
	LST(8, 52, 290, 112, UI_EL_LIST, 1, WF_SEL),
};

// AllocatePsiTrainingState: солдаты базы ctx.base; флаг SF_PSI, вместимость —
// пси-лаборатории базы (Base::getAvailablePsiLabs / getUsedPsiLabs).
static uint8_t psi_bases[MAX_BASES], psi_nb;

static uint16_t psi_free(void)
{
	caps_t a, u;
	base_caps(ctx.base, &a, &u);
	return a.psi > u.psi ? a.psi - u.psi : 0;
}

// ---------------------------------------------------------------- прочее

static const wdef_t w_resrequired[] = {
	WIN(16, 10, 288, 180, UI_EL_WINDOW),
	BTN(80, 150, 160, 18, UI_EL_BUTTON, STR_OK, A_POP, 0, ESC),
	TXT(16, 50, 288, 80, UI_EL_TEXT1, DYN(0), BIG | TC | TM),
};

// ---------------------------------------------------------------- таблица экранов

static const scr_t tab[] = {
	SCR(SCR_RESEARCH_COMPLETE, UI_SCR_GEORESEARCHCOMPLETE, NOUI, RES_BACK05_SCR, SF_POPUP, w_rescomplete),
	SCR(SCR_NEW_POSS_RESEARCH, UI_SCR_GEORESEARCH, NOUI, RES_BACK05_SCR, SF_POPUP, w_newresearch),
	SCR(SCR_NEW_POSS_MANUF, UI_SCR_GEOMANUFACTURE, NOUI, RES_BACK17_SCR, SF_POPUP, w_newmanuf),
	SCR(SCR_PRODUCTION_DONE, UI_SCR_GEOMANUFACTURECOMPLETE, NOUI, RES_BACK17_SCR, SF_POPUP, w_proddone),
	SCR(SCR_ITEMS_ARRIVING, UI_SCR_ITEMSARRIVING, NOUI, RES_BACK13_SCR, SF_POPUP, w_items),
	SCR(SCR_ALIEN_BASE, UI_SCR_ALIENBASE, NOUI, RES_BACK13_SCR, 0, w_alienbase),
	SCR(SCR_BASE_DEFENSE, UI_SCR_BASEDEFENSE, NOUI, RES_BACK04_SCR, 0, w_basedef),
	SCR(SCR_BASE_DESTROYED, UI_SCR_BASEDESTROYED, NOUI, RES_BACK15_SCR, SF_POPUP, w_basedestroyed),
	SCR(SCR_CONFIRM_LANDING, UI_SCR_CONFIRMLANDING, NOUI, RES_BACK15_SCR, SF_POPUP, w_landing),
	SCR(SCR_CONFIRM_CYDONIA, UI_SCR_CONFIRMCYDONIA, NOUI, RES_BACK12_SCR, SF_POPUP, w_cydonia),
	SCR(SCR_PSI_TRAINING, UI_SCR_PSITRAINING, NOUI, RES_BACK01_SCR, 0, w_psi),
	SCR(SCR_ALLOC_PSI, UI_SCR_ALLOCATEPSI, NOUI, RES_BACK01_SCR, 0, w_allocpsi),
	SCR(SCR_RESEARCH_REQUIRED, UI_SCR_GEORESEARCHREQUIRED, NOUI, RES_BACK05_SCR, SF_POPUP, w_resrequired),
};

uint8_t geo2_get(uint8_t id, sdef_t *s, wdef_t *w) __banked
{
	if (!scr_find(tab, sizeof tab / sizeof tab[0], id, s, w)) return 0;
	if (id == SCR_BASE_DEFENSE && bd_cycles >= 0) {   // OK — только в конце
		w[1].type = w[5].type = 0;
		w[1].key = w[5].key = 0;
	}
	if (id == SCR_PSI_TRAINING) {
		psi_nb = 0;
		for (uint8_t b = 0; b < MAX_BASES; b++) {
			if (!ST->base[b].name[0]) continue;
			caps_t a, u;
			base_caps(b, &a, &u);
			if (!a.psi) continue;
			wdef_t *d = &w[s->n++];
			d->type = W_BUTTON; d->x = 80; d->y = 40 + 16 * psi_nb; d->w = 160; d->h = 14;
			d->el = UI_EL_BUTTON1; d->str = DYN(psi_nb); d->flags = 0;
			d->act = A_CUSTOM; d->arg = psi_nb; d->key = 0;
			psi_bases[psi_nb++] = b;
		}
	}
	return 1;
}

void geo2_text(uint8_t id, uint8_t slot, uint8_t row, char *buf) __banked
{
	switch (id) {
	case SCR_RESEARCH_COMPLETE:
		str_copy(rec_name(RES_RULE_RESEARCH, ev_cur.what & ~GE_OLD), buf, 256);
		break;
	case SCR_NEW_POSS_RESEARCH:
		if (row != LIST_COLS && row < lab_nnewres) str_copy(rec_name(RES_RULE_RESEARCH, lab_newres[row]), buf, 256);
		break;
	case SCR_NEW_POSS_MANUF:
		if (row != LIST_COLS && row < lab_nnewman) str_copy(rec_name(RES_RULE_MANUFACTURE, lab_newman[row]), buf, 256);
		break;
	case SCR_PRODUCTION_DONE: {
		// ProductionCompleteState: постройка / производство / нет денег
		if (slot == 1) {
			str_copy(ev_cur.kind == GE_BUILT ? STR_GO_TO_BASE : STR_ALLOCATE_MANUFACTURE, buf, 64);
			break;
		}
		uint16_t fmt = STR_PRODUCTION_OF_ITEM_AT_BASE_IS_COMPLETE, tb = RES_RULE_MANUFACTURE;
		if (ev_cur.kind == GE_BUILT) { fmt = STR_CONSTRUCTION_OF_FACILITY_AT_BASE_IS_COMPLETE; tb = RES_RULE_FACILITIES; }
		else if (ev_cur.kind == GE_NO_MONEY) fmt = STR_NOT_ENOUGH_MONEY_TO_PRODUCE_ITEM_AT_BASE;
		else if (ev_cur.kind == GE_NO_MATERIALS) fmt = STR_NOT_ENOUGH_SPECIAL_MATERIALS_TO_PRODUCE_ITEM_AT_BASE;
		str_copy(rec_name(tb, ev_cur.what), t1, sizeof t1);
		strcpy(t2, ST->base[ev_cur.base].name);
		str_fmt(buf, str_get(fmt), t1, t2);
		break;
	}
	case SCR_ITEMS_ARRIVING:
		if (row == LIST_COLS) {
			static const uint8_t cw[] = { 155, 41, 98 };
			list_cols(buf, 3, cw, 0);
		} else {
			// ItemsArrivingState: предмет / персонал / корабль / солдат, количество, база
			const gevent_t *e = &arrivals[row];
			uint16_t q = e->qty;
			if (e->what == TK_CRAFT_ARRIVED) { craft_name((uint8_t)q, buf); q = 1; }
			else if (e->what == TK_SOLDIER_ARRIVED) {
				soldier_t sd;
				soldier_get((uint8_t)q, &sd);
				strcpy(buf, sd.name);
				q = 1;
			} else if (e->what == TK_SCI_ARRIVED) str_copy(STR_SCIENTISTS, buf, 64);
			else if (e->what == TK_ENG_ARRIVED) str_copy(STR_ENGINEERS, buf, 64);
			else str_copy(rec_name(RES_RULE_ITEMS, e->what), buf, 64);
			strcat(buf, "\t");
			fmt_num(buf + strlen(buf), q, 0);
			strcat(buf, "\t");
			strcat(buf, ST->base[e->base].name);
		}
		break;
	case SCR_ALIEN_BASE: {                        // AlienBaseState: «страна, регион» / регион / неизвестно
		geo_t p = ST->abase[ctx.tidx].pos;
		uint16_t lon = (uint16_t)((uint32_t)p.lon >> 16);
		int16_t lat = (int16_t)(p.lat >> 16);
		uint8_t c = country_at(lon, lat), r = region_at(lon, lat);
		char loc[80];
		if (c != NONE8) {
			str_copy(rec_name(RES_RULE_COUNTRIES, c), t1, sizeof t1);
			str_copy(rec_name(RES_RULE_REGIONS, r), loc, sizeof loc);
			str_fmt(buf, str_get(STR_COUNTRIES_COMMA), t1, loc);
			strcpy(loc, buf);
		} else
			str_copy(r != NONE8 ? rec_name(RES_RULE_REGIONS, r) : STR_UNKNOWN, loc, sizeof loc);
		str_fmt(buf, str_get(STR_XCOM_AGENTS_HAVE_LOCATED_AN_ALIEN_BASE_IN_REGION), loc, "");
		break;
	}
	case SCR_BASE_DEFENSE:
		if (slot == 0) {
			strcpy(t1, ST->base[ctx.base].name);
			str_fmt(buf, str_get(STR_BASE_UNDER_ATTACK), t1, "");
			break;
		}
		if (slot == 2) {                          // _txtInit: со второго шага
			if (bd_cycles) str_copy(STR_BASE_DEFENSES_INITIATED, buf, 64);
			break;
		}
		if (row == LIST_COLS) {
			static const uint8_t cw[] = { 134, 70, 50 };
			list_cols(buf, 3, cw, 0);
		} else if (row < bd_nrows) {
			uint8_t k = bd_row[row].kind;
			str_copy(k == BDR_SHIELD ? STR_GRAV_SHIELD_REPELS_UFO : k == BDR_DEAD ? STR_UFO_DESTROYED
				: rec_name(RES_RULE_FACILITIES, k), buf, 64);
			strcat(buf, "\t");
			if (bd_row[row].fire) str_copy(STR_FIRING, buf + strlen(buf), 40);
			strcat(buf, "\t");
			if (bd_row[row].res) str_copy(bd_row[row].res == 2 ? STR_HIT : STR_MISSED, buf + strlen(buf), 40);
		}
		break;
	case SCR_BASE_DESTROYED:
		strcpy(t1, ST->base[ctx.base].name);
		str_fmt(buf, str_get(STR_THE_ALIENS_HAVE_DESTROYED_THE_UNDEFENDED_BASE), t1, "");
		break;
	case SCR_CONFIRM_LANDING:                     // корабль и его цель (Target::getName)
		if (slot == 0) {
			craft_t *cr = &ST->craft[ctx.craft];
			uint8_t k = cr->dest_kind, d = cr->dest;
			craft_name(ctx.craft, t1);
			target_name(k, d, t2);
			str_fmt(buf, str_get(STR_CRAFT_READY_TO_LAND_NEAR_DESTINATION), t1, t2);
		} else {
			buf[0] = 1;
			str_copy(STR_BEGIN_MISSION, buf + 1, 64);
		}
		break;
	case SCR_PSI_TRAINING:
		if (slot < psi_nb) strcpy(buf, ST->base[psi_bases[slot]].name);
		break;
	case SCR_ALLOC_PSI:
		if (slot == 0) {
			fmt_num(t2, psi_free(), 0);
			str_fmt(buf, str_get(STR_REMAINING_PSI_LAB_CAPACITY), t2, "");
		} else if (row == LIST_COLS) {
			static const uint8_t cw[] = { 114, 80, 62, 30 }, ca[] = { 0, 0, 0, TX_RIGHT };
			list_cols(buf, 4, cw, ca);
		} else {
			// AllocatePsiTrainingState: сила видна, если навык уже есть
			soldier_t sd;
			soldier_get(soldier_nth(ctx.base, row), &sd);
			strcpy(buf, sd.name);
			strcat(buf, "\t");
			if (sd.cur.psi_skill) { strcat(buf, "   "); fmt_num(buf + strlen(buf), sd.cur.psi_strength, 0); }
			else str_copy(STR_UNKNOWN, buf + strlen(buf), 32);
			strcat(buf, "\t   ");
			fmt_num(buf + strlen(buf), sd.cur.psi_skill, 0);
			strcat(buf, "/+");
			fmt_num(buf + strlen(buf), sd.psi_improve, 0);
			strcat(buf, "\t");
			str_copy(sd.flags & SF_PSI ? STR_YES : STR_NO, buf + strlen(buf), 16);
		}
		break;
	case SCR_RESEARCH_REQUIRED:                // ResearchRequiredState: боеприпас, оружие
		str_copy(rec_name(RES_RULE_ITEMS, ev_cur.qty), t1, sizeof t1);
		str_copy(rec_name(RES_RULE_ITEMS, ev_cur.what), t2, sizeof t2);
		str_fmt(buf, str_get(STR_YOU_NEED_TO_RESEARCH_ITEM_TO_PRODUCE_ITEM), t1, t2);
		break;
	}
}

uint8_t geo2_rows(uint8_t id, uint8_t slot) __banked
{
	(void)slot;
	switch (id) {
	case SCR_NEW_POSS_RESEARCH: return lab_nnewres;
	case SCR_NEW_POSS_MANUF: return lab_nnewman;
	case SCR_ITEMS_ARRIVING: return narr;
	case SCR_BASE_DEFENSE: return bd_nrows;
	case SCR_ALLOC_PSI: return soldiers_count(ctx.base, 0xFE, 0);
	}
	return 0;
}

static void not_ported(const char *what)
{
	strcpy(ui_msg, what);
	strcat(ui_msg, "\x02\nnot ported yet");
	UI_GO(A_POP_PUSH, SCR_ERROR);
}

uint8_t geo2_event(uint8_t id, uint8_t ev, uint8_t arg) __banked
{
	switch (id) {
	case SCR_RESEARCH_COMPLETE:                // «Отчёты»: статья темы (если новая), под ней — бонуса getOneFree
		if (ev == EVT_BUTTON && arg == 3) {
			uint8_t topic = (ev_cur.what & GE_OLD) ? NONE8 : ufop_find_topic((uint8_t)ev_cur.what);
			uint8_t bonus = ev_cur.qty != NONE16 ? ufop_find_topic((uint8_t)ev_cur.qty) : NONE8;
			if (topic == NONE8) { topic = bonus; bonus = NONE8; }
			if (topic != NONE8) { ufop_next = bonus; ufop_open(topic); UI_GO(A_POP_PUSH, SCR_ARTICLE); }
			else UI_GO(A_POP, 0);
		}
		break;
	case SCR_NEW_POSS_RESEARCH:                // списки показаны — очистить; «Назначить» — окно исследований базы
		if (ev == EVT_CLOSE) lab_nnewres = 0;
		else if (ev == EVT_BUTTON) { ctx.base = ev_cur.base; UI_GO(A_POP_PUSH, SCR_RESEARCH); }
		break;
	case SCR_NEW_POSS_MANUF:
		if (ev == EVT_CLOSE) lab_nnewman = 0;
		else if (ev == EVT_BUTTON) { ctx.base = ev_cur.base; UI_GO(A_POP_PUSH, SCR_MANUFACTURE); }
		break;
	case SCR_PRODUCTION_DONE:
		if (ev != EVT_BUTTON) break;
		ctx.base = ev_cur.base;
		ST->speed = 0;                             // timerReset
		UI_GO(A_POP_PUSH, ev_cur.kind == GE_BUILT ? SCR_BASESCAPE : SCR_MANUFACTURE);
		break;
	case SCR_ITEMS_ARRIVING:
		if (ev == EVT_BUTTON && narr) {
			ctx.base = arrivals[0].base;
			ST->speed = 0;                         // timerReset
			UI_GO(A_POP_PUSH, SCR_BASESCAPE);
		}
		break;
	case SCR_ALIEN_BASE:                          // btnOkClick: timerReset, центр на базу (сетка видов, z2+)
		if (ev == EVT_BUTTON) {
			geo_t p = ST->abase[ctx.tidx].pos;
			geo_center(&p, 2);
			UI_GO(A_POP, 0);
		}
		break;
	case SCR_PSI_TRAINING:
		if (ev == EVT_BUTTON && arg < psi_nb) {
			ctx.base = psi_bases[arg];
			UI_GO(A_PUSH, SCR_ALLOC_PSI);
		}
		break;
	case SCR_CONFIRM_LANDING:                     // «Нет» — корабль домой, «Да» — высадка в бой
		if (ev == EVT_BUTTON) {
			if (arg == 3) {
				if (ST->craft[ctx.craft].type != NONE8 && ST->craft[ctx.craft].status == CS_OUT) craft_return(ctx.craft);
				UI_GO(A_POP, 0);
			} else {
				// Бой: карту собирает генератор при открытии экрана (16 §3). Выбор террейна
				// и размера по развёртыванию миссии — следующий шаг, корабль пока остаётся
				// на месте, разбора после боя ещё нет.
				UI_GO(A_POP_PUSH, SCR_BATTLE);
			}
		}
		break;
	case SCR_CONFIRM_CYDONIA:
		if (ev == EVT_BUTTON) not_ported("BATTLESCAPE");
		break;
	case SCR_BASE_DEFENSE:
		if (ev == EVT_OPEN) {                      // setupDefenses, getGravShields
			rtab_t t;
			r_facilities_t f;
			bd_ndef = bd_grav = bd_passes = bd_attacks = bd_expl = bd_nrows = 0;
			bd_action = BD_NONE;
			bd_cycles = 0;
			bd_wait = 13;
			bd_at = frames;
			rtab_open(RES_RULE_FACILITIES, &t);
			for (uint8_t i = 0; i < MAX_FACILITIES; i++) {
				facility_t *fc = &ST->base[ctx.base].fac[i];
				if (fc->type == NONE8 || fc->days) continue;
				rtab_get(&t, fc->type, &f);
				if (f.defense) bd_def[bd_ndef++] = i;
				if (f.flags & FACILITIES_F_GRAV) bd_grav++;
			}
		} else if (ev == EVT_TICK) {
			uint16_t now = frames, at = bd_at;
			if ((uint16_t)(now - at) >= bd_wait) { bd_at = now; bd_step(); }
		} else if (ev == EVT_BUTTON) {             // btnOkClick: НЛО цело — handleBaseDefense
			if (bd_real && ST->ufo[ctx.ufo].type != NONE8 && ST->ufo[ctx.ufo].status != US_DESTROYED) assault(A_POP_PUSH);
			else { bd_real = 0; UI_GO(A_POP, 0); }
		}
		break;
	case SCR_BASE_DESTROYED:
		if (ev == EVT_OPEN && bd_real) {           // возмездие региона базы снимается
			geo_t p = ST->base[ctx.base].pos;
			uint8_t r = region_at((uint16_t)((uint32_t)p.lon >> 16), (int16_t)(p.lat >> 16));
			if (r != NONE8) retaliation_cancel(r);
		} else if (ev == EVT_BUTTON) {             // btnOkClick: базы нет; баз не осталось — поражение
			uint8_t real = bd_real;
			bd_real = 0;
			if (real) {
				base_remove(ctx.base);
				if (!bases_count()) { ST->ending = END_LOSE; st_ironsave(); UI_GO(A_POP_PUSH, SCR_REPORT_FAILED); break; }
			}
			UI_GO(A_POP, 0);
		}
		break;
	case SCR_ALLOC_PSI:
		if (ev == EVT_LIST) {
			uint8_t i = soldier_nth(ctx.base, arg);
			if (i == NONE8) break;
			soldier_t sd;
			soldier_get(i, &sd);
			if (!(sd.flags & SF_PSI) && !psi_free()) break;
			sd.flags ^= SF_PSI;
			soldier_put(i, &sd);
			ui_dirty(0);                       // остаток мест
			ui_dirty_row(1, arg);
		}
		break;
	}
	return 0;
}
