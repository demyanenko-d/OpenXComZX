// Банк 9: исследования и производство (ResearchState, NewResearchListState,
// ResearchInfoState, ManufactureState, NewManufactureListState,
// ManufactureStartState, ManufactureInfoState). Раскладки — tmp/screens_basescape.json,
// логика — src/game/lab.c. База — ctx.base.
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "res.h"
#include "rules.h"
#include "text.h"
#include "state.h"
#include "game.h"
#include "scrdef.h"

static char t1[48], t2[48];

#define ARW(x, y, el, down, arg) { W_ARROW, x, y, 13, 14, el, NOSTR, down, A_CUSTOM, arg, 0 }
#define CMB(x, y, w, h, el, str, arg) { W_COMBO, x, y, w, h, el, str, 0, A_CUSTOM, arg, 0 }
#define TB   TX_BOTTOM

static uint8_t lst[140], nlst;          // строки открытого списка (темы / производства)
static uint8_t dirty;                   // списки пересчитать (окно сверху закрылось)
static uint8_t res_slot, res_new;       // ResearchInfo: проект, только что создан
static uint8_t man_rec;                 // ManufactureStart: запись manufacture
static uint8_t prod_slot, prod_fresh;   // ManufactureInfo: производство, новое
static uint8_t cat_n;                   // категории NewManufactureList (combo.item)

static uint16_t rec_name(uint16_t table, uint16_t i)
{
	rtab_t t;
	if (!rtab_open(table, &t) || i >= t.n) return 0xFFFF;
	return rtab_word(&t, i, 0);
}

static void fmt1(char *buf, uint16_t pat, int32_t v)
{
	fmt_num(t2, v, 0);
	str_fmt(buf, str_get(pat), t2, "");
}

static void fmt1f(char *buf, uint16_t pat, int32_t v)
{
	fmt_funds(t2, v);
	str_fmt(buf, str_get(pat), t2, "");
}

static uint16_t free_labs(void)
{
	caps_t a, u;
	base_caps(ctx.base, &a, &u);
	return a.labs > u.labs ? a.labs - u.labs : 0;
}

static uint16_t free_shops(void)
{
	caps_t a, u;
	base_caps(ctx.base, &a, &u);
	return a.workshops > u.workshops ? a.workshops - u.workshops : 0;
}

static int16_t free_hangars(void)
{
	caps_t a, u;
	base_caps(ctx.base, &a, &u);
	return (int16_t)a.hangars - (int16_t)u.hangars;
}

// k-й проект / производство базы (порядок пула)
static uint8_t res_nth(uint8_t k)
{
	for (uint8_t i = 0; i < MAX_RESEARCH; i++)
		if (ST->research[i].base == ctx.base) { if (!k) return i; k--; }
	return NONE8;
}

static uint8_t prod_nth(uint8_t k)
{
	for (uint8_t i = 0; i < MAX_PRODS; i++)
		if (ST->prod[i].base == ctx.base) { if (!k) return i; k--; }
	return NONE8;
}

static uint16_t sum_scientists(void)
{
	uint16_t n = 0;
	for (uint8_t i = 0; i < MAX_RESEARCH; i++) if (ST->research[i].base == ctx.base) n += ST->research[i].assigned;
	return n;
}

static uint16_t sum_engineers(void)
{
	uint16_t n = 0;
	for (uint8_t i = 0; i < MAX_PRODS; i++) if (ST->prod[i].base == ctx.base) n += ST->prod[i].engineers;
	return n;
}

// ---------------------------------------------------------------- исследования

static const wdef_t w_research[] = {
	WIN(0, 0, 320, 200, UI_EL_WINDOW),
	BTN(8, 176, 148, 16, UI_EL_BUTTON, STR_NEW_PROJECT, A_PUSH, SCR_NEW_RESEARCH, 0),
	BTN(164, 176, 148, 16, UI_EL_BUTTON, STR_OK, A_POP, 0, ESC),
	TXT(5, 8, 310, 17, UI_EL_TEXT, STR_CURRENT_RESEARCH, BIG | TC),
	TXT(10, 24, 150, 9, UI_EL_TEXT, DYN(1), 0),
	TXT(160, 24, 150, 9, UI_EL_TEXT, DYN(2), 0),
	TXT(10, 34, 300, 9, UI_EL_TEXT, DYN(3), 0),
	TXT(10, 44, 110, 17, UI_EL_TEXT, STR_RESEARCH_PROJECT, TW),
	TXT(120, 44, 106, 17, UI_EL_TEXT, STR_SCIENTISTS_ALLOCATED_UC, TW),
	TXT(226, 44, 84, 9, UI_EL_TEXT, STR_PROGRESS, 0),
	LST(10, 62, 286, 112, UI_EL_LIST, 0, WF_SEL),
};

static const wdef_t w_newres[] = {
	WIN(45, 30, 230, 140, UI_EL_WINDOW),
	BTN(53, 146, 214, 16, UI_EL_BUTTON, STR_OK, A_POP, 0, ESC),
	TXT(53, 38, 214, 16, UI_EL_TEXT, STR_NEW_RESEARCH_PROJECTS, TC),
	LST(61, 54, 190, 88, UI_EL_LIST, 0, WF_SEL | TC),
};

static const wdef_t w_resinfo[] = {
	WIN(45, 30, 230, 140, UI_EL_WINDOW),
	BTN(169, 145, 90, 16, UI_EL_BUTTON2, DYN(10), A_CUSTOM, 1, ENT),
	BTN(61, 145, 90, 16, UI_EL_BUTTON2, DYN(11), A_CUSTOM, 2, 0),
	HOT(0, 0, 0, 0, A_CUSTOM, 5, ESC),          // ESC: новый — отмена, старый — OK
	TXT(61, 40, 210, 17, UI_EL_TEXT, DYN(0), BIG),
	TXT(61, 60, 210, 9, UI_EL_TEXT, DYN(1), 0),
	TXT(61, 70, 210, 9, UI_EL_TEXT, DYN(2), 0),
	TXT(61, 80, 210, 17, UI_EL_TEXT, DYN(3), BIG),
	TXT(85, 100, 110, 17, UI_EL_TEXT, STR_INCREASE, BIG),
	TXT(85, 120, 110, 17, UI_EL_TEXT, STR_DECREASE, BIG),
	ARW(195, 100, UI_EL_BUTTON1, 0, 3),
	ARW(195, 120, UI_EL_BUTTON1, 1, 4),
};

static const uint16_t progress_str[6] = { STR_NONE, STR_UNKNOWN, STR_POOR, STR_AVERAGE, STR_GOOD, STR_EXCELLENT };

// ---------------------------------------------------------------- производство

static const wdef_t w_manuf[] = {
	WIN(0, 0, 320, 200, UI_EL_WINDOW),
	BTN(8, 176, 148, 16, UI_EL_BUTTON, STR_NEW_PRODUCTION, A_PUSH, SCR_NEW_MANUF, 0),
	BTN(164, 176, 148, 16, UI_EL_BUTTON, STR_OK, A_POP, 0, ESC),
	TXT(5, 8, 310, 17, UI_EL_TEXT1, STR_CURRENT_PRODUCTION, BIG | TC),
	TXT(8, 24, 150, 9, UI_EL_TEXT1, DYN(1), 0),
	TXT(160, 24, 150, 9, UI_EL_TEXT1, DYN(2), 0),
	TXT(8, 34, 150, 9, UI_EL_TEXT1, DYN(3), 0),
	TXT(160, 34, 150, 9, UI_EL_TEXT1, DYN(4), 0),
	TXT(10, 52, 80, 9, UI_EL_TEXT2, STR_ITEM, 0),
	TXT(112, 44, 56, 18, UI_EL_TEXT2, STR_ENGINEERS__ALLOCATED, TW),
	TXT(168, 44, 56, 18, UI_EL_TEXT2, STR_UNITS_PRODUCED, TW),
	TXT(222, 44, 44, 27, UI_EL_TEXT2, STR_COST__PER__UNIT, TW),
	TXT(260, 44, 60, 27, UI_EL_TEXT2, STR_DAYS_HOURS_LEFT, TW),
	LST(10, 80, 286, 88, UI_EL_LIST, 0, WF_SEL),
};

static const wdef_t w_newman[] = {
	WIN(0, 22, 320, 156, UI_EL_WINDOW),
	BTN(8, 154, 304, 16, UI_EL_BUTTON, STR_OK, A_POP, 0, ESC),
	TXT(0, 30, 320, 17, UI_EL_TEXT, STR_PRODUCTION_ITEMS, BIG | TC),
	TXT(10, 62, 156, 9, UI_EL_TEXT, STR_ITEM, 0),
	TXT(166, 62, 130, 9, UI_EL_TEXT, STR_CATEGORY, 0),
	LST(10, 70, 286, 80, UI_EL_LIST, 0, WF_SEL),
	CMB(166, 46, 146, 16, UI_EL_CATBOX, DYN(1), 1),
};

static const wdef_t w_mstart[] = {
	WIN(0, 20, 320, 160, UI_EL_WINDOW),
	TXT(0, 30, 320, 17, UI_EL_TEXT, DYN(0), BIG | TC),
	TXT(16, 50, 290, 9, UI_EL_TEXT, DYN(1), 0),
	TXT(16, 60, 290, 9, UI_EL_TEXT, DYN(2), 0),
	TXT(16, 70, 290, 9, UI_EL_TEXT, DYN(3), 0),
	BTN(16, 155, 136, 16, UI_EL_BUTTON, STR_CANCEL_UC, A_POP, 0, ESC),
};
static const wdef_t w_mstart_req[] = {           // только если нужны материалы
	TXT(16, 84, 290, 9, UI_EL_TEXT, STR_SPECIAL_MATERIALS_REQUIRED, TC),
	TXT(30, 92, 60, 16, UI_EL_TEXT, STR_ITEM_REQUIRED, TW),
	TXT(155, 92, 60, 16, UI_EL_TEXT, STR_UNITS_REQUIRED, TW),
	TXT(230, 92, 60, 16, UI_EL_TEXT, STR_UNITS_AVAILABLE, TW),
	LST(30, 108, 270, 40, UI_EL_LIST, 0, 0),
};
static const wdef_t w_mstart_go[] = {            // только если можно начать
	BTN(168, 155, 136, 16, UI_EL_BUTTON, STR_START_PRODUCTION, A_CUSTOM, 1, ENT),
};

static const wdef_t w_minfo[] = {
	WIN(0, 20, 320, 160, UI_EL_WINDOW),
	TXT(0, 30, 320, 17, UI_EL_TEXT, DYN(0), BIG | TC),
	TXT(16, 50, 160, 9, UI_EL_TEXT, DYN(1), 0),
	TXT(16, 60, 160, 9, UI_EL_TEXT, DYN(2), 0),
	TXT(168, 50, 152, 9, UI_EL_TEXT, DYN(3), 0),   // в OpenXcom 160 (обрезается экраном)
	TXT(16, 80, 112, 32, UI_EL_TEXT, STR_ENGINEERS__ALLOCATED, BIG | TW | TB),
	TXT(128, 88, 40, 16, UI_EL_TEXT, DYN(4), BIG),
	TXT(168, 64, 112, 48, UI_EL_TEXT, STR_UNITS_TO_PRODUCE, BIG | TW | TB),
	TXT(280, 88, 40, 16, UI_EL_TEXT, DYN(5), BIG),
	TXT(40, 118, 90, 9, UI_EL_TEXT, STR_INCREASE_UC, 0),
	TXT(40, 138, 90, 9, UI_EL_TEXT, STR_DECREASE_UC, 0),
	ARW(132, 114, UI_EL_BUTTON1, 0, 3),
	ARW(132, 136, UI_EL_BUTTON1, 1, 4),
	TXT(192, 118, 90, 9, UI_EL_TEXT, STR_INCREASE_UC, 0),
	TXT(192, 138, 90, 9, UI_EL_TEXT, STR_DECREASE_UC, 0),
	ARW(284, 114, UI_EL_BUTTON1, 0, 5),
	ARW(284, 136, UI_EL_BUTTON1, 1, 6),
	BTN(168, 155, 136, 16, UI_EL_BUTTON2, STR_OK, A_CUSTOM, 1, ENT),
	BTN(16, 155, 136, 16, UI_EL_BUTTON2, STR_STOP_PRODUCTION, A_CUSTOM, 2, 0),
	TGL(244, 61, 60, 16, UI_EL_BUTTON1, STR_SELL_PRODUCTION, 0, 0, 7, 0),
	HOT(0, 0, 0, 0, A_CUSTOM, 1, ESC),          // ESC тоже подтверждает (keyCancel)
};

// ---------------------------------------------------------------- таблица

static const scr_t tab[] = {
	SCR(SCR_RESEARCH, UI_SCR_RESEARCHMENU, NOUI, RES_BACK05_SCR, 0, w_research),
	SCR(SCR_NEW_RESEARCH, UI_SCR_SELECTNEWRESEARCH, NOUI, RES_BACK05_SCR, SF_POPUP, w_newres),
	SCR(SCR_RESEARCH_INFO, UI_SCR_ALLOCATERESEARCH, NOUI, RES_BACK05_SCR, SF_POPUP, w_resinfo),
	SCR(SCR_MANUFACTURE, UI_SCR_MANUFACTUREMENU, NOUI, RES_BACK17_SCR, 0, w_manuf),
	SCR(SCR_NEW_MANUF, UI_SCR_SELECTNEWMANUFACTURE, NOUI, RES_BACK17_SCR, SF_POPUP, w_newman),
	SCR(SCR_MANUF_START, UI_SCR_ALLOCATEMANUFACTURE, NOUI, RES_BACK17_SCR, SF_POPUP, w_mstart),
	SCR(SCR_MANUF_INFO, UI_SCR_MANUFACTUREINFO, NOUI, RES_BACK17_SCR, SF_POPUP, w_minfo),
};

// Категории производства (в порядке первого появления среди доступных) и список
static void newman_fill(void)
{
	uint8_t all[140], n = manuf_avail(ctx.base, all, sizeof all);
	rtab_t t;
	rtab_open(RES_RULE_MANUFACTURE, &t);
	if (!cat_n) {
		combo.item[0] = STR_ALL_ITEMS;
		cat_n = 1;
		for (uint8_t i = 0; i < n; i++) {
			uint16_t c = rtab_word(&t, all[i], offsetof(r_manufacture_t, category));
			uint8_t k = 1;
			while (k < cat_n && combo.item[k] != c) k++;
			if (k == cat_n && cat_n < COMBO_MAX) combo.item[cat_n++] = c;
		}
		combo.n = cat_n;
		combo.sel = 0;
	}
	nlst = 0;
	for (uint8_t i = 0; i < n; i++)
		if (!combo.sel || rtab_word(&t, all[i], offsetof(r_manufacture_t, category)) == combo.item[combo.sel]) lst[nlst++] = all[i];
}

static uint8_t mreq(uint16_t *v)                // материалы записи man_rec: пары (ref, qty)
{
	rtab_t t;
	r_manufacture_t m;
	rtab_open(RES_RULE_MANUFACTURE, &t);
	rtab_get(&t, man_rec, &m);
	uint8_t n = m.required_items.n > 8 ? 8 : m.required_items.n;
	if (n) rtab_tail(&t, m.required_items.off, v, n * 4);
	return n;
}

uint8_t lab_get(uint8_t id, sdef_t *s, wdef_t *w) __banked
{
	if (!scr_find(tab, sizeof tab / sizeof tab[0], id, s, w)) return 0;
	if (dirty || combo.changed) {
		if (id == SCR_NEW_RESEARCH) { nlst = research_avail(ctx.base, lst, sizeof lst); dirty = 0; }
		if (id == SCR_NEW_MANUF) { combo.changed = 0; newman_fill(); dirty = 0; }
	}
	if (id == SCR_MANUF_START) {
		uint16_t v[16];
		if (mreq(v)) { memcpy(&w[s->n], w_mstart_req, sizeof w_mstart_req); s->n += sizeof w_mstart_req / sizeof(wdef_t); }
		if (manuf_can_start(ctx.base, man_rec)) { memcpy(&w[s->n], w_mstart_go, sizeof w_mstart_go); s->n++; }
	}
	return 1;
}

void lab_text(uint8_t id, uint8_t slot, uint8_t row, char *buf) __banked
{
	rtab_t t;
	base_t *bs = &ST->base[ctx.base];
	switch (id) {
	case SCR_RESEARCH:
		if (slot == 1) { fmt1(buf, STR_SCIENTISTS_AVAILABLE, bs->scientists); break; }
		if (slot == 2) { fmt1(buf, STR_SCIENTISTS_ALLOCATED, sum_scientists()); break; }
		if (slot == 3) { fmt1(buf, STR_LABORATORY_SPACE_AVAILABLE, free_labs()); break; }
		if (row == LIST_COLS) {
			static const uint8_t cw[] = { 158, 58, 70 };
			list_cols(buf, 3, cw, 0);
		} else {
			uint8_t r = res_nth(row);
			if (r == NONE8) break;
			str_copy(rec_name(RES_RULE_RESEARCH, ST->research[r].topic), buf, 64);
			strcat(buf, "\t");
			fmt_num(buf + strlen(buf), ST->research[r].assigned, 0);
			strcat(buf, "\t");
			str_copy(progress_str[research_progress(r)], buf + strlen(buf), 40);
		}
		break;
	case SCR_NEW_RESEARCH:
		if (row != LIST_COLS && row < nlst) str_copy(rec_name(RES_RULE_RESEARCH, lst[row]), buf, 128);
		break;
	case SCR_RESEARCH_INFO: {
		research_t *p = &ST->research[res_slot];
		switch (slot) {
		case 0: str_copy(rec_name(RES_RULE_RESEARCH, p->topic), buf, 64); break;
		case 1: fmt1(buf, STR_SCIENTISTS_AVAILABLE_UC, bs->scientists); break;
		case 2: fmt1(buf, STR_LABORATORY_SPACE_AVAILABLE_UC, free_labs()); break;
		case 3: fmt1(buf, STR_SCIENTISTS_ALLOCATED, p->assigned); break;
		case 10: str_copy(res_new ? STR_START_PROJECT : STR_OK, buf, 40); break;
		case 11: str_copy(res_new ? STR_CANCEL_UC : STR_CANCEL_PROJECT, buf, 40); break;
		}
		break;
	}
	case SCR_MANUFACTURE:
		if (slot == 1) { fmt1(buf, STR_ENGINEERS_AVAILABLE, bs->engineers); break; }
		if (slot == 2) { fmt1(buf, STR_ENGINEERS_ALLOCATED, sum_engineers()); break; }
		if (slot == 3) { fmt1(buf, STR_WORKSHOP_SPACE_AVAILABLE, free_shops()); break; }
		if (slot == 4) { fmt1f(buf, STR_CURRENT_FUNDS, ST->funds); break; }
		if (row == LIST_COLS) {
			static const uint8_t cw[] = { 115, 15, 52, 56, 48 }, ca[] = { 0, TR, TR, TR, TR };
			list_cols(buf, 5, cw, ca);
		} else {
			uint8_t r = prod_nth(row);
			if (r == NONE8) break;
			prod_t *p = &ST->prod[r];
			r_manufacture_t m;
			rtab_open(RES_RULE_MANUFACTURE, &t);
			rtab_get(&t, p->manuf, &m);
			uint16_t done = prod_done_units(r);
			str_copy(m.name, buf, 64);
			strcat(buf, "\t");
			fmt_num(buf + strlen(buf), p->engineers, 0);
			strcat(buf, "\t");
			fmt_num(buf + strlen(buf), done, 0);
			strcat(buf, "/");
			if (p->flags & PF_INFINITE) strcat(buf, "INF"); else fmt_num(buf + strlen(buf), p->amount, 0);
			if (p->flags & PF_SELL) strcat(buf, " $");
			strcat(buf, "\t");
			fmt_funds(buf + strlen(buf), (int32_t)m.cost);
			strcat(buf, "\t");
			if (p->flags & PF_INFINITE) strcat(buf, "INF");
			else if (p->engineers) {                // ManufactureState: ceil(осталось часов / инженеры)
				int32_t left = (int32_t)p->amount * m.time - (int32_t)p->spent;
				int32_t h = (left + p->engineers - 1) / p->engineers;
				fmt_num(buf + strlen(buf), h / 24, 0);
				strcat(buf, "/");
				fmt_num(buf + strlen(buf), h % 24, 0);
			} else strcat(buf, "-");
		}
		break;
	case SCR_NEW_MANUF:
		if (slot == 1) { str_copy(combo.item[combo.sel], buf, 64); break; }
		if (row == LIST_COLS) {
			static const uint8_t cw[] = { 156, 130 };
			list_cols(buf, 2, cw, 0);
		} else if (row < nlst) {
			rtab_open(RES_RULE_MANUFACTURE, &t);
			str_copy(rtab_word(&t, lst[row], 0), buf, 64);
			strcat(buf, "\t");
			str_copy(rtab_word(&t, lst[row], offsetof(r_manufacture_t, category)), buf + strlen(buf), 64);
		}
		break;
	case SCR_MANUF_START: {
		r_manufacture_t m;
		rtab_open(RES_RULE_MANUFACTURE, &t);
		rtab_get(&t, man_rec, &m);
		switch (slot) {
		case 0: str_copy(m.name, buf, 64); return;
		case 1: fmt1(buf, STR_ENGINEER_HOURS_TO_PRODUCE_ONE_UNIT, m.time); return;
		case 2: fmt1f(buf, STR_COST_PER_UNIT_, (int32_t)m.cost); return;
		case 3: fmt1(buf, STR_WORK_SPACE_REQUIRED, m.space); return;
		}
		if (row == LIST_COLS) {
			static const uint8_t cw[] = { 140, 75, 55 };
			list_cols(buf, 3, cw, 0);
		} else {
			uint16_t v[16];
			uint8_t n = mreq(v);
			if (row >= n) break;
			uint16_t ref = v[row * 2];
			str_copy(rec_name(ref & RREF_ALT ? RES_RULE_CRAFTS : RES_RULE_ITEMS, ref & ~RREF_ALT), buf, 64);
			strcat(buf, "\t\x01");
			fmt_num(buf + strlen(buf), v[row * 2 + 1], 0);
			strcat(buf, "\x01\t\x01");
			fmt_num(buf + strlen(buf), manuf_have(ctx.base, ref), 0);
		}
		break;
	}
	case SCR_MANUF_INFO: {
		prod_t *p = &ST->prod[prod_slot];
		r_manufacture_t m;
		rtab_open(RES_RULE_MANUFACTURE, &t);
		rtab_get(&t, p->manuf, &m);
		switch (slot) {
		case 0: str_copy(m.name, buf, 64); break;
		case 1: fmt1(buf, STR_ENGINEERS_AVAILABLE_UC, bs->engineers); break;
		case 2: fmt1(buf, STR_WORKSHOP_SPACE_AVAILABLE_UC, free_shops()); break;
		case 3: {                                // getMonthlyNetFunds: 730 часов в месяц
			int32_t per = -(int32_t)m.cost;
			if (p->flags & PF_SELL) {
				uint16_t v[16];
				uint8_t n = m.produced_items.n > 8 ? 8 : m.produced_items.n;
				if (n) rtab_tail(&t, m.produced_items.off, v, n * 4);
				for (uint8_t i = 0; i < n; i++)
					if (!(v[i * 2] & RREF_ALT)) per += price(BUY_ITEM, (uint8_t)v[i * 2], 1) * (int16_t)v[i * 2 + 1];
			}
			// единиц в месяц * 1000 = часы * 1000 / время (без плавающей точки)
			int32_t hours = 730l * p->engineers, k = 0;
			if (!(p->flags & PF_INFINITE)) {
				int32_t cap = (int32_t)m.time * (int32_t)(p->amount - prod_done_units(prod_slot));
				if (hours > cap) hours = cap;
			}
			if (m.time) k = hours * 1000 / m.time;
			fmt1f(buf, STR_MONTHLY_PROFIT, (per / 1000) * k + (per % 1000) * k / 1000);
			break;
		}
		case 4: strcpy(buf, ">\x01"); fmt_num(buf + 2, p->engineers, 0); break;
		case 5:
			strcpy(buf, ">\x01");
			if (p->flags & PF_INFINITE) strcat(buf, "INF"); else fmt_num(buf + 2, p->amount, 0);
			break;
		}
		break;
	}
	}
}

uint8_t lab_rows(uint8_t id, uint8_t slot) __banked
{
	uint8_t n = 0;
	(void)slot;
	switch (id) {
	case SCR_RESEARCH:
		for (uint8_t i = 0; i < MAX_RESEARCH; i++) if (ST->research[i].base == ctx.base) n++;
		return n;
	case SCR_MANUFACTURE:
		for (uint8_t i = 0; i < MAX_PRODS; i++) if (ST->prod[i].base == ctx.base) n++;
		return n;
	case SCR_NEW_RESEARCH: case SCR_NEW_MANUF: return nlst;
	case SCR_MANUF_START: { uint16_t v[16]; return mreq(v); }
	}
	return 0;
}

static void error(uint16_t str)
{
	str_copy(str, ui_msg, sizeof ui_msg);
	UI_GO(A_PUSH, SCR_ERROR);
}

// Изменение количества (ResearchInfoState / ManufactureInfoState more/lessByValue)
static uint16_t lim(uint16_t change, uint16_t a) { return change < a ? change : a; }

uint8_t lab_event(uint8_t id, uint8_t ev, uint8_t arg) __banked
{
	base_t *bs = &ST->base[ctx.base];
	uint16_t ch = ui_arrow_max ? 0xFFFF : 1;
	switch (id) {
	case SCR_RESEARCH:
		if (ev == EVT_LIST) {
			uint8_t r = res_nth(arg);
			if (r != NONE8) { res_slot = r; res_new = 0; UI_GO(A_PUSH, SCR_RESEARCH_INFO); }
		}
		break;
	case SCR_NEW_RESEARCH:
		if (ev == EVT_OPEN) { nlst = research_avail(ctx.base, lst, sizeof lst); dirty = 0; }
		else if (ev == EVT_LIST && arg < nlst) {
			uint8_t r = research_start(ctx.base, lst[arg]);
			if (r != NONE8) { res_slot = r; res_new = 1; UI_GO(A_PUSH, SCR_RESEARCH_INFO); }
		}
		break;
	case SCR_RESEARCH_INFO: {
		research_t *p = &ST->research[res_slot];
		if (ev == EVT_CLOSE) { dirty = 1; break; }
		if (ev != EVT_BUTTON) break;
		if (arg == 5) arg = res_new ? 2 : 1;
		if (arg == 1) UI_GO(A_POP, 0);
		else if (arg == 2) { research_cancel(res_slot); UI_GO(A_POP, 0); }
		else if (arg == 3) {
			uint16_t fl = free_labs();
			if (bs->scientists && fl) {
				ch = lim(lim(ch, bs->scientists), fl);
				p->assigned += ch;
				bs->scientists -= ch;
				ui_dirty(1); ui_dirty(2); ui_dirty(3);   // свободные, место, назначено
			}
		} else if (arg == 4 && p->assigned) {
			ch = lim(ch, p->assigned);
			p->assigned -= ch;
			bs->scientists += ch;
			ui_dirty(1); ui_dirty(2); ui_dirty(3);
		}
		break;
	}
	case SCR_MANUFACTURE:
		if (ev == EVT_LIST) {
			uint8_t r = prod_nth(arg);
			if (r != NONE8) { prod_slot = r; prod_fresh = 0; UI_GO(A_PUSH, SCR_MANUF_INFO); }
		}
		break;
	case SCR_NEW_MANUF:
		if (ev == EVT_OPEN) { cat_n = 0; combo.changed = 0; newman_fill(); dirty = 0; }
		else if (ev == EVT_LIST && arg < nlst) { man_rec = lst[arg]; UI_GO(A_PUSH, SCR_MANUF_START); }
		else if (ev == EVT_BUTTON && arg == 1) {    // ComboBox категорий
			combo.x = 166; combo.y = 46; combo.w = 146; combo.h = 16;
			combo.bg = RES_BACK17_SCR; combo.ui = UI_SCR_SELECTNEWMANUFACTURE; combo.el = UI_EL_CATBOX;
			UI_GO(A_PUSH, SCR_COMBO);
		}
		break;
	case SCR_MANUF_START:
		if (ev == EVT_BUTTON && arg == 1) {          // ManufactureStartState::btnStartClick
			rtab_t t;
			rtab_open(RES_RULE_MANUFACTURE, &t);
			uint16_t cat = rtab_word(&t, man_rec, offsetof(r_manufacture_t, category));
			uint16_t space = rtab_word(&t, man_rec, offsetof(r_manufacture_t, space));
			if (cat == STR_CRAFT && free_hangars() <= 0) error(STR_NO_FREE_HANGARS_FOR_CRAFT_PRODUCTION);
			else if (space > free_shops()) error(STR_NOT_ENOUGH_WORK_SPACE);
			else {
				uint8_t r = prod_new(ctx.base, man_rec);
				if (r != NONE8) { prod_slot = r; prod_fresh = 1; UI_GO(A_PUSH, SCR_MANUF_INFO); }
			}
		}
		break;
	case SCR_MANUF_INFO: {
		prod_t *p = &ST->prod[prod_slot];
		if (ev == EVT_CLOSE) { dirty = 1; break; }
		if (ev == EVT_QUERY) return (p->flags & PF_SELL) != 0;
		if (ev != EVT_BUTTON) break;
		rtab_t t;
		rtab_open(RES_RULE_MANUFACTURE, &t);
		uint8_t craft = rtab_word(&t, p->manuf, offsetof(r_manufacture_t, category)) == STR_CRAFT;
		switch (arg) {
		case 1:                                      // OK: новое — первая единица
			if (prod_fresh) { prod_confirm(prod_slot); UI_GO(A_POP2, 0); }
			else UI_GO(A_POP, 0);
			break;
		case 2:                                      // стоп
			prod_stop(prod_slot);
			UI_GO(prod_fresh ? A_POP2 : A_POP, 0);
			break;
		case 3: {
			uint16_t fs = free_shops();
			if (bs->engineers && fs) {
				ch = lim(lim(ch, bs->engineers), fs);
				p->engineers += ch;
				bs->engineers -= ch;
			}
			break;
		}
		case 4:
			if (p->engineers) { ch = lim(ch, p->engineers); p->engineers -= ch; bs->engineers += ch; }
			break;
		case 5:                                      // единиц больше (правая — бесконечно / до предела ангаров)
			if (p->flags & PF_INFINITE) break;
			if (ui_arrow_max && !craft) { p->flags |= PF_INFINITE; break; }
			if (craft) {
				int16_t fh = free_hangars();
				if (fh <= 0) { error(STR_NO_FREE_HANGARS_FOR_CRAFT_PRODUCTION); return 0; }
				ch = lim(ch, (uint16_t)fh);
			}
			ch = lim(ch, 65000u - p->amount);
			p->amount += ch;
			break;
		case 6: {                                    // единиц меньше (не ниже выпущенных + 1)
			uint16_t done = prod_done_units(prod_slot);
			p->flags &= ~PF_INFINITE;
			if (ui_arrow_max || p->amount <= done) p->amount = done + 1;
			else p->amount -= lim(1, p->amount - (done + 1));
			break;
		}
		case 7: p->flags ^= PF_SELL; break;
		}
		// только изменившиеся тексты: техники/место/назначено (3, 4), единицы (5, 6); прибыль — всегда
		if (arg == 3 || arg == 4) { ui_dirty(1); ui_dirty(2); ui_dirty(4); }
		if (arg == 5 || arg == 6) ui_dirty(5);
		if (arg >= 3) ui_dirty(3);
		break;
	}
	}
	return 0;
}
