// Банк 4: графики (GraphsState OpenXcom, REF/OpenXcom/src/Geoscape/GraphsState.cpp).
// Шесть режимов: активность НЛО по регионам/странам, X-COM по регионам/странам, доход
// стран, финансы. Кнопки-переключатели рядов (до 16) + «Итого»; нажатые хранятся в
// ST->graph_tgl (как graphRegionToggles/… в сохранении OpenXcom). 12 точек: последний
// месяц в x = 312, шаг 17 влево; месяцы без истории — на нуле.
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "res.h"
#include "rules.h"
#include "text.h"
#include "gfx.h"
#include "state.h"
#include "game.h"
#include "scrdef.h"

#define GRAPH_MAX_BUTTONS 16
#define TOTAL_BIT 31
#define M_FINANCE 5

static const wdef_t w_graphs[] = {
	CUS(0, 0, 320, 200, NOSTR, A_NONE, 0),   // фон, сетка, линии
	HOT(96, 0, 32, 24, A_CUSTOM, 100, 0),   // режимы: arg 100 + режим
	HOT(128, 0, 32, 24, A_CUSTOM, 101, 0),
	HOT(160, 0, 32, 24, A_CUSTOM, 102, 0),
	HOT(192, 0, 32, 24, A_CUSTOM, 103, 0),
	HOT(224, 0, 32, 24, A_CUSTOM, 104, 0),
	HOT(256, 0, 32, 24, A_CUSTOM, 105, 0),
	HOT(288, 0, 32, 24, A_POP, 0, ESC),
	HOT(0, 0, 0, 0, A_POP, 0, 'g'),         // keyGeoGraphs
	TXT(90, 28, 230, 16, UI_EL_TEXT, DYN(0), BIG | TC),
	TXT(96, 28, 38, 11, UI_EL_TEXT, DYN(3), 0),     // STR_FINANCE_THOUSANDS (доход, финансы)
	LST(115, 183, 205, 8, UI_EL_SCALE, 1, 0),
	LST(121, 191, 200, 8, UI_EL_SCALE, 2, 0),
};

static const scr_t tab[] = {
	SCR(SCR_GRAPHS, UI_SCR_GRAPHS, NOUI, 0, 0, w_graphs),
};

static const uint16_t month_str[12] = {
	STR_JAN, STR_FEB, STR_MAR, STR_APR, STR_MAY, STR_JUN, STR_JUL, STR_AUG, STR_SEP, STR_OCT, STR_NOV, STR_DEC,
};
static const uint16_t graph_title[6] = {
	STR_UFO_ACTIVITY_IN_AREAS, STR_UFO_ACTIVITY_IN_COUNTRIES, STR_XCOM_ACTIVITY_IN_AREAS,
	STR_XCOM_ACTIVITY_IN_COUNTRIES, STR_INCOME, STR_FINANCE,
};
static const uint16_t finance_str[5] = { STR_INCOME, STR_EXPENDITURE, STR_MAINTENANCE, STR_BALANCE, STR_SCORE };

static uint8_t mode;                     // 0..5 (порядок кнопок режимов)
static uint8_t nitems;                   // рядов (регионов/стран/5), кнопок — не больше 16
static uint8_t reg[MAX_REGIONS];         // i-й регион с зонами -> запись таблицы (как region_rec)
static uint8_t nreg, nctry;
static int32_t lo, check;                // шкала: низ и цена деления (9 делений)

static uint8_t is_region(void) { return mode == 0 || mode == 2; }
static uint8_t tgl_set(void) { return mode == M_FINANCE ? 2 : is_region() ? 0 : 1; }
static uint8_t pressed(uint8_t bit) { return (uint8_t)((ST->graph_tgl[tgl_set()] >> bit) & 1); }

// Значение ряда i (nitems — «Итого») за месяц iter назад (0 — текущий).
static int32_t value(uint8_t i, uint8_t iter)
{
	if (iter >= ST->hist_len) return 0;
	uint8_t m = ST->hist_len - 1 - iter;
	if (mode == M_FINANCE) {
		switch (i) {
		case 0: return ST->fin.income[m] / 1000;
		case 1: return ST->fin.expenditure[m] / 1000;
		case 2: return ST->fin.maintenance[m] / 1000 + (iter ? 0 : total_maintenance() / 1000);
		case 3: return (iter ? ST->fin.balance[m] : ST->funds) / 1000;   // _funds.back() — текущие деньги
		default: {                                                        // исследования + Σ(X-COM − пришельцы)
			int32_t s = ST->fin.research[m];
			for (uint8_t r = 0; r < MAX_REGIONS; r++) s += ST->region[r].act_xcom[m] - ST->region[r].act_alien[m];
			return s;
		}
		}
	}
	if (i >= nitems) {                    // «Итого» — по всем записям
		int32_t s = 0;
		if (is_region()) for (uint8_t r = 0; r < nreg; r++) s += mode ? ST->region[reg[r]].act_xcom[m] : ST->region[reg[r]].act_alien[m];
		else for (uint8_t c = 0; c < nctry; c++) s += mode == 1 ? ST->country[c].act_alien[m] : mode == 3 ? ST->country[c].act_xcom[m] : ST->country[c].funding[m];
		return s;
	}
	switch (mode) {
	case 0: return ST->region[reg[i]].act_alien[m];
	case 2: return ST->region[reg[i]].act_xcom[m];
	case 1: return ST->country[i].act_alien[m];
	case 3: return ST->country[i].act_xcom[m];
	default: return ST->country[i].funding[m];                           // тыс. $
	}
}

// Шкала (drawRegionLines/drawCountryLines/drawFinanceLines): по нажатым рядам за все
// месяцы истории; деление 10 (доход 50, финансы 250), удваивается, пока не влезет в 9.
static void scale(void)
{
	int32_t up = 0, low = 0;
	uint8_t n = mode == M_FINANCE ? 5 : nitems;
	for (uint8_t i = 0; i <= n; i++) {
		if (i == n && mode == M_FINANCE) break;
		if (!pressed(i == n ? TOTAL_BIT : i)) continue;
		for (uint8_t e = 0; e < ST->hist_len; e++) {
			int32_t v = value(i, e);
			if (v > up) up = v;
			if (v < low) low = v;
		}
	}
	check = mode == M_FINANCE ? 250 : mode == 4 ? 50 : 10;
	while (up - low > check * 9) check *= 2;
	lo = 0;
	while (low < lo) lo -= check;
}

static int16_t ypos(int32_t v)
{
	int32_t range = check * 9;
	return (int16_t)(175 - (-lo * 126 / range) - (v * 126 / range));
}

static void line(uint8_t i, uint8_t c, uint8_t clamp)
{
	int16_t px = 0, py = 0;
	for (uint8_t iter = 0; iter < 12; iter++) {
		int16_t x = 312 - iter * 17, y = ypos(value(i, iter));
		if (clamp && y > 175) y = 175;
		if (iter) gfx_line(x, y, px, py, c);
		px = x; py = y;
	}
}

static void draw(void)
{
	uint8_t gc = ui_color(UI_EL_GRAPH, 0);
	if (!gfx_bg(RES_GRAPH_BDY, 0, 0, 320, 200)) gfx_bg(RES_GRAPHS_SPK, 0, 0, 320, 200);
	gfx_fill(125, 49, 188, 127, gc);
	for (uint8_t g = 0; g <= 4; g++)
		for (int16_t y = 50 + g; y <= 163 + g; y += 14)
			for (int16_t x = 126 + g; x <= 297 + g; x += 17)
				gfx_fill(x, y, 16 - 2 * g, 13 - 2 * g, g == 4 ? 0 : gc + g + 1);
	if (mode == M_FINANCE) {
		for (uint8_t b = 0; b < 5; b++)
			if (pressed(b)) line(b, (uint8_t)(16 * (b / 2 + 1) + (b & 1 ? 8 : 0)), 0);
		return;
	}
	for (uint8_t i = 0; i < nitems; i++)
		if (pressed(i)) line(i, (uint8_t)(13 + 8 * (i % GRAPH_MAX_BUTTONS) + 4), 1);
	if (pressed(TOTAL_BIT)) line(nitems, ui_color(is_region() ? UI_EL_REGIONTOTAL : UI_EL_COUNTRYTOTAL, 1), 0);
}

static void set_mode(uint8_t m)
{
	mode = m;
	if (mode == M_FINANCE) nitems = 5;
	else if (is_region()) nitems = nreg;
	else nitems = nctry;
	if (mode != M_FINANCE && nitems > GRAPH_MAX_BUTTONS) nitems = GRAPH_MAX_BUTTONS;
	scale();
}

uint8_t graph_get(uint8_t id, sdef_t *s, wdef_t *w) __banked
{
	if (!scr_find(tab, sizeof tab / sizeof tab[0], id, s, w)) return 0;
	for (uint8_t i = 0; i < 10 && s->n < SDEF_MAXW; i++) {
		wdef_t *b = &w[s->n++];
		b->type = W_TEXT; b->x = 80; b->y = 171 - 14 * i; b->w = 42; b->h = 16;
		b->el = UI_EL_SCALE; b->str = DYN(10 + i); b->flags = TX_RIGHT; b->act = b->arg = b->key = 0;
	}
	uint8_t nb = mode == M_FINANCE ? 5 : nitems + 1;
	for (uint8_t i = 0; i < nb && s->n < SDEF_MAXW; i++) {
		wdef_t *b = &w[s->n++];
		uint8_t total = mode != M_FINANCE && i == nitems;
		b->type = W_TOGGLE; b->x = 0; b->y = 11 * i; b->w = 88; b->h = 11;
		b->el = UI_EL_BUTTON; b->str = DYN(30 + i); b->flags = 0; b->act = 0;
		b->arg = total ? TOTAL_BIT : i; b->key = 0;
	}
	return 1;
}

void graph_text(uint8_t id, uint8_t slot, uint8_t row, char *buf) __banked
{
	(void)id;
	if (slot == 0) { str_copy(graph_title[mode], buf, 256); return; }
	if (slot == 3) { if (mode >= 4) str_copy(STR_FINANCE_THOUSANDS, buf, 64); return; }
	if (slot == 1) {                       // месяцы: последний справа
		if (row == LIST_COLS) {
			static const uint8_t cw[12] = { 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17 };
			list_cols(buf, 12, cw, 0);
			return;
		}
		char t[8];
		for (uint8_t m = 0; m < 12; m++) {
			str_copy(month_str[(ST->month + m) % 12], t, sizeof t);
			t[3] = 0;
			strcat(buf, t);
			if (m < 11) strcat(buf, "\t");
		}
		return;
	}
	if (slot == 2) {                       // годы: под январём, прошлый — в первой ячейке
		if (row == LIST_COLS) {
			static const uint8_t cw[6] = { 34, 34, 34, 34, 34, 34 };
			list_cols(buf, 6, cw, 0);
			return;
		}
		uint8_t wrap = 12 - ST->month, k = wrap / 2;
		for (uint8_t c = 0; c < 6; c++) {
			if (c == k) fmt_num(buf + strlen(buf), ST->year, 0);
			else if (c == 0 && wrap > 2) fmt_num(buf + strlen(buf), ST->year - 1, 0);
			if (c < 5) strcat(buf, "\t");
		}
		return;
	}
	if (slot >= 10 && slot < 20) { fmt_num(buf, lo + check * (slot - 10), ','); return; }
	if (slot >= 30) {
		uint8_t i = slot - 30;
		if (mode == M_FINANCE) str_copy(finance_str[i], buf, 64);
		else if (i >= nitems) str_copy(STR_TOTAL_UC, buf, 64);
		else {
			rtab_t t;
			rtab_open(is_region() ? RES_RULE_REGIONS : RES_RULE_COUNTRIES, &t);
			str_copy(rtab_word(&t, is_region() ? reg[i] : i, 0), buf, 64);
		}
	}
}

uint8_t graph_rows(uint8_t id, uint8_t slot) __banked
{
	(void)id;
	return slot == 0 ? 0 : 1;
}

uint8_t graph_event(uint8_t id, uint8_t ev, uint8_t arg) __banked
{
	(void)id;
	switch (ev) {
	case EVT_OPEN: {                       // конструктор: btnUfoRegionClick
		rtab_t t;
		r_regions_t r;
		rtab_open(RES_RULE_REGIONS, &t);
		nreg = 0;
		for (uint16_t k = 0; k < t.n && nreg < MAX_REGIONS; k++) {
			rtab_get(&t, k, &r);
			if (r.areas.n) reg[nreg++] = (uint8_t)k;
		}
		rtab_open(RES_RULE_COUNTRIES, &t);
		nctry = t.n > MAX_COUNTRIES ? MAX_COUNTRIES : (uint8_t)t.n;
		set_mode(0);
		break;
	}
	case EVT_DRAW: draw(); break;
	case EVT_QUERY:                        // ToggleTextButton::setInvertColor: нажатая — инверсия своим цветом
		if (!pressed(arg)) return 0;
		if (arg == TOTAL_BIT) return ui_color(is_region() ? UI_EL_REGIONTOTAL : UI_EL_COUNTRYTOTAL, 0);
		return (uint8_t)(13 + 8 * (arg % GRAPH_MAX_BUTTONS));
	case EVT_BUTTON:
		if (arg >= 100) { if (mode != arg - 100) set_mode(arg - 100); }
		else { ST->graph_tgl[tgl_set()] ^= 1ul << arg; scale(); }
		UI_GO(A_REDRAW, 0);                // линии и шкала (фон рисует сам W_CUSTOM)
		break;
	}
	return 0;
}
