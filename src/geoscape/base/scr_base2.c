// Банк 6: покупка, продажа, переводы между базами (PurchaseState, SellState,
// TransferBaseState, TransferItemsState, TransferConfirmState, TransfersState;
// tmp/screens_basescape.json, tmp/state_model.md §3.2-3.3). Количества — стрелки у
// строк (EVT_ARROW): левая +1, правая -1, правая кнопка мыши — до предела,
// удержание — повтор. Категории — ComboBox (getCategory OpenXcom).
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "res.h"
#include "rules.h"
#include "text.h"
#include "state.h"
#include "game.h"
#include "scrdef.h"

#define CMB(x, y, w, h, el, str, arg) { W_COMBO, x, y, w, h, el, str, 0, A_CUSTOM, arg, 0 }

#define MAX_ROWS 200
static uint8_t row_kind[MAX_ROWS], row_what[MAX_ROWS], row_cat[MAX_ROWS];
static uint16_t row_amt[MAX_ROWS];
static uint8_t nrows;
static uint8_t vis[MAX_ROWS], nvis;     // строки выбранной категории
static int32_t total;                   // сумма покупок / продаж / переводов
static uint16_t pq, cq;                 // персонал и корабли в заказе
static int32_t iq;                      // место на складе в заказе (сотые; при продаже — освобождается)
static caps_t avail, used;              // база ctx.base (перевод — база назначения)
static uint8_t tr_to;                   // перевод: база назначения
static uint16_t tr_d16;                 // перевод: расстояние * 16
static char t1[48];

// категории строк (PurchaseState::getCategory)
#define C_ALL        0
#define C_PERSONNEL  1
#define C_CRAFT      2
#define C_ALIENS     3
#define C_EQUIPMENT  4
#define C_COMPONENTS 5
static const uint16_t cat_str[6] = { STR_ALL_ITEMS, STR_PERSONNEL, STR_CRAFT_ARMAMENT, STR_ALIENS, STR_EQUIPMENT, STR_COMPONENTS };
static uint8_t cats[6], ncats;          // категории экрана в порядке появления
static uint8_t cw_items[(MAX_ITEMS + 7) / 8], ar_items[(MAX_ITEMS + 7) / 8];

#define BT_NONE   0
#define BT_AMMO   2
#define BT_CORPSE 11
#define IBIT(a, i) (((a)[(i) >> 3] >> ((i) & 7)) & 1)

static uint8_t is_done(uint16_t topic) { return topic < RES_BITS * 8 && ((ST->discovered[topic >> 3] >> (topic & 7)) & 1); }

static uint8_t reqs_done(rtab_t *t, uint16_t rec, uint8_t off)
{
	uint8_t l[3];
	uint16_t r[8];
	far_read(t->base + 8 + (uint32_t)rec * t->size + off, l, 3);
	uint8_t n = l[2] > 8 ? 8 : l[2];
	if (!n) return 1;
	rtab_tail(t, l[0] | (l[1] << 8), r, n * 2);
	for (uint8_t i = 0; i < n; i++) if (!is_done(r[i])) return 0;
	return 1;
}

// Предметы-пусковые/обоймы кораблей и броня со склада (для категорий)
static void cat_sets(void)
{
	rtab_t t;
	memset(cw_items, 0, sizeof cw_items);
	memset(ar_items, 0, sizeof ar_items);
	rtab_open(RES_RULE_CRAFTWEAPONS, &t);
	for (uint8_t i = 0; i < t.n; i++) {
		uint16_t l = rtab_word(&t, i, offsetof(r_craftWeapons_t, launcher)), c = rtab_word(&t, i, offsetof(r_craftWeapons_t, clip));
		if (l < MAX_ITEMS) cw_items[l >> 3] |= (uint8_t)(1 << (l & 7));
		if (c < MAX_ITEMS) cw_items[c >> 3] |= (uint8_t)(1 << (c & 7));
	}
	rtab_open(RES_RULE_ARMORS, &t);
	for (uint8_t i = 0; i < t.n; i++) {
		uint16_t s = rtab_word(&t, i, offsetof(r_armors_t, store_item));
		if (s < MAX_ITEMS) ar_items[s >> 3] |= (uint8_t)(1 << (s & 7));
	}
}

static uint16_t item_flags(uint8_t it)
{
	rtab_t t;
	rtab_open(RES_RULE_ITEMS, &t);
	return rtab_word(&t, it, offsetof(r_items_t, flags));
}

static uint8_t item_cat(uint8_t it)
{
	rtab_t t;
	rtab_open(RES_RULE_ITEMS, &t);
	uint8_t bt = (uint8_t)rtab_word(&t, it, offsetof(r_items_t, battle_type));
	uint16_t fl = rtab_word(&t, it, offsetof(r_items_t, flags));
	if (bt == BT_CORPSE || (fl & ITEMS_F_LIVE_ALIEN)) return C_ALIENS;
	if (bt == BT_NONE) {
		if (IBIT(cw_items, it)) return C_CRAFT;
		if (IBIT(ar_items, it)) return C_EQUIPMENT;
		return C_COMPONENTS;
	}
	return C_EQUIPMENT;
}

// Боеприпас (строка сдвинута на 2 пробела, цвет ammoColor)
static uint8_t is_ammo(uint8_t it)
{
	rtab_t t;
	rtab_open(RES_RULE_ITEMS, &t);
	uint8_t bt = (uint8_t)rtab_word(&t, it, offsetof(r_items_t, battle_type));
	return bt == BT_AMMO || (bt == BT_NONE && (int16_t)rtab_word(&t, it, offsetof(r_items_t, clip_size)) > 0);
}

static void add_row(uint8_t kind, uint8_t what)
{
	if (nrows >= MAX_ROWS) return;
	uint8_t c = kind == BUY_ITEM ? item_cat(what) : (kind == BUY_CRAFT || kind == SELL_CRAFT) ? C_CRAFT : C_PERSONNEL;
	row_kind[nrows] = kind;
	row_what[nrows] = what;
	row_amt[nrows] = 0;
	row_cat[nrows] = c;
	nrows++;
	uint8_t k = 0;
	while (k < ncats && cats[k] != c) k++;
	if (k == ncats) cats[ncats++] = c;
}

// Начало заполнения; конец — rows_done (категории -> combo, видимые строки)
static void rows_begin(void)
{
	nrows = 0; total = 0; pq = cq = 0; iq = 0;
	ncats = 1; cats[0] = C_ALL;
	cat_sets();
}

static void filter(void)
{
	uint8_t c = cats[combo.sel < ncats ? combo.sel : 0];
	nvis = 0;
	for (uint8_t r = 0; r < nrows; r++)
		if (c == C_ALL || row_cat[r] == c) vis[nvis++] = r;
}

static void rows_done(void)
{
	for (uint8_t k = 0; k < ncats; k++) combo.item[k] = cat_str[cats[k]];
	combo.n = ncats;
	combo.sel = 0;
	combo.changed = 0;
	filter();
}

static int16_t item_size(uint8_t it)
{
	rtab_t t;
	rtab_open(RES_RULE_ITEMS, &t);
	return (int16_t)rtab_word(&t, it, offsetof(r_items_t, size));
}

static void error(uint16_t str)
{
	str_copy(str, ui_msg, sizeof ui_msg);
	UI_GO(A_PUSH, SCR_ERROR);
}

static void open_combo(int16_t y, uint16_t bg, uint8_t ui)
{
	combo.x = 10; combo.y = y; combo.w = 120; combo.h = 16;
	combo.bg = bg; combo.ui = ui; combo.el = UI_EL_TEXT;
	UI_GO(A_PUSH, SCR_COMBO);
}

// ---------------------------------------------------------------- покупка

static const wdef_t w_buy[] = {
	WIN(0, 0, 320, 200, UI_EL_WINDOW),
	BTN(8, 176, 148, 16, UI_EL_BUTTON, STR_OK, A_CUSTOM, 1, ENT),
	BTN(164, 176, 148, 16, UI_EL_BUTTON, STR_CANCEL, A_POP, 0, ESC),
	TXT(5, 8, 310, 17, UI_EL_TEXT, STR_PURCHASE_HIRE_PERSONNEL, BIG | TC),
	TXT(10, 24, 150, 9, UI_EL_TEXT, DYN(1), 0),
	TXT(160, 24, 150, 9, UI_EL_TEXT, DYN(2), 0),
	TXT(160, 34, 150, 9, UI_EL_TEXT, DYN(3), 0),
	TXT(152, 44, 102, 9, UI_EL_TEXT, STR_COST_PER_UNIT_UC, 0),
	TXT(256, 44, 60, 9, UI_EL_TEXT, STR_QUANTITY_UC, 0),
	LST(10, 54, 285, 120, UI_EL_LIST, 0, 0),
	CMB(10, 36, 120, 16, UI_EL_TEXT, DYN(4), 2),
};

static void buy_open(void)
{
	rtab_t t;
	uint8_t b = ctx.base;
	rows_begin();
	base_caps(b, &avail, &used);
	if (price(BUY_SOLDIER, 0, 0)) add_row(BUY_SOLDIER, 0);
	add_row(BUY_SCIENTIST, 0);
	add_row(BUY_ENGINEER, 0);
	rtab_open(RES_RULE_CRAFTS, &t);
	for (uint8_t i = 0; i < t.n; i++)
		if (price(BUY_CRAFT, i, 0) && reqs_done(&t, i, offsetof(r_crafts_t, requires))) add_row(BUY_CRAFT, i);
	rtab_open(RES_RULE_ITEMS, &t);
	for (uint8_t i = 0; i < t.n && i < MAX_ITEMS; i++)
		if (price(BUY_ITEM, i, 0) && reqs_done(&t, i, offsetof(r_items_t, requires))) add_row(BUY_ITEM, i);
	rows_done();
}

// Сколько такого уже на базе (колонка «в наличии»)
static uint16_t at_base(uint8_t b, uint8_t kind, uint8_t what)
{
	uint8_t n = 0;
	switch (kind) {
	case BUY_SOLDIER: return soldiers_count(b, 0xFE, 0);
	case BUY_SCIENTIST: return ST->base[b].scientists;
	case BUY_ENGINEER: return ST->base[b].engineers;
	case BUY_CRAFT:
		for (uint8_t c = 0; c < MAX_CRAFTS; c++) if (ST->craft[c].type == what && ST->craft[c].base == b) n++;
		return n;
	case BUY_ITEM: return ST->base[b].items[what];
	}
	return 0;
}

static void row_name(uint8_t kind, uint8_t what, char *buf)
{
	rtab_t t;
	switch (kind) {
	case BUY_SOLDIER: str_copy(STR_SOLDIER, buf, 40); break;
	case BUY_SCIENTIST: str_copy(STR_SCIENTIST, buf, 40); break;
	case BUY_ENGINEER: str_copy(STR_ENGINEER, buf, 40); break;
	case BUY_CRAFT: rtab_open(RES_RULE_CRAFTS, &t); str_copy(rtab_word(&t, what, 0), buf, 40); break;
	case BUY_ITEM:
		if (is_ammo(what)) { strcpy(buf, "  "); buf += 2; }
		rtab_open(RES_RULE_ITEMS, &t); str_copy(rtab_word(&t, what, 0), buf, 40); break;
	case SELL_SOLDIER: { soldier_t so; soldier_get(what, &so); strcpy(buf, so.name); break; }
	case SELL_CRAFT: craft_name(what, buf); break;
	}
}

// PurchaseState::increaseByValue
static void buy_more(uint8_t r, uint16_t change)
{
	uint8_t kind = row_kind[r], what = row_what[r];
	int32_t cost = price(kind, what, 0);
	if (total + cost > ST->funds) { error(STR_NOT_ENOUGH_MONEY); return; }
	uint8_t person = kind == BUY_SOLDIER || kind == BUY_SCIENTIST || kind == BUY_ENGINEER;
	int16_t size = kind == BUY_ITEM ? item_size(what) : 0;
	if (person && pq + 1 > (int16_t)(avail.quarters - used.quarters)) { error(STR_NOT_ENOUGH_LIVING_SPACE); return; }
	if (kind == BUY_CRAFT && cq + 1 > (int16_t)(avail.hangars - used.hangars)) { error(STR_NO_FREE_HANGARS_FOR_PURCHASE); return; }
	if (kind == BUY_ITEM && (int32_t)used.stores + iq + size > (int32_t)avail.stores) { error(STR_NOT_ENOUGH_STORE_SPACE); return; }
	int32_t by_money = cost ? (ST->funds - total) / cost : 0x7FFF;
	if (change > by_money) change = (uint16_t)by_money;
	if (person) {
		int16_t room = (int16_t)(avail.quarters - used.quarters) - pq;
		if (change > room) change = room;
		pq += change;
	} else if (kind == BUY_CRAFT) {
		int16_t room = (int16_t)(avail.hangars - used.hangars) - cq;
		if (change > room) change = room;
		cq += change;
	} else if (size > 0) {
		int32_t room = ((int32_t)avail.stores - (int32_t)used.stores - iq + 5) / size;
		if (change > room) change = (uint16_t)room;
		iq += (int32_t)change * size;
	}
	row_amt[r] += change;
	total += cost * change;
}

static void buy_less(uint8_t r, uint16_t change)
{
	uint8_t kind = row_kind[r], what = row_what[r];
	if (!row_amt[r]) return;
	if (change > row_amt[r]) change = row_amt[r];
	if (kind == BUY_SOLDIER || kind == BUY_SCIENTIST || kind == BUY_ENGINEER) pq -= change;
	else if (kind == BUY_CRAFT) cq -= change;
	else iq -= (int32_t)change * item_size(what);
	row_amt[r] -= change;
	total -= price(kind, what, 0) * change;
}

// ---------------------------------------------------------------- продажа

static const wdef_t w_sell[] = {
	WIN(0, 0, 320, 200, UI_EL_WINDOW),
	BTN(8, 176, 148, 16, UI_EL_BUTTON, STR_SELL_SACK, A_CUSTOM, 1, ENT),
	BTN(164, 176, 148, 16, UI_EL_BUTTON, STR_CANCEL, A_POP, 0, ESC),
	TXT(5, 8, 310, 17, UI_EL_TEXT, STR_SELL_ITEMS_SACK_PERSONNEL, BIG | TC),
	TXT(10, 24, 150, 9, UI_EL_TEXT, DYN(2), 0),
	TXT(160, 24, 150, 9, UI_EL_TEXT, DYN(1), 0),
	TXT(160, 34, 150, 9, UI_EL_TEXT, DYN(3), 0),
	TXT(136, 44, 54, 9, UI_EL_TEXT, STR_QUANTITY_UC, 0),
	TXT(190, 44, 96, 9, UI_EL_TEXT, STR_SELL_SACK, 0),
	TXT(270, 44, 40, 9, UI_EL_TEXT, STR_VALUE, 0),
	LST(10, 54, 285, 120, UI_EL_LIST, 0, 0),
	CMB(10, 36, 120, 16, UI_EL_TEXT, DYN(4), 2),
};

// Строки продажи и перевода: солдаты без корабля, корабли не в полёте, свободный
// персонал, склад (SellState / TransferItemsState).
static void stock_rows(uint8_t b)
{
	uint8_t n = soldiers_count(b, 0xFE, 0);
	for (uint8_t k = 0; k < n; k++) {
		uint8_t s = soldier_nth(b, k);
		soldier_t so;
		soldier_get(s, &so);
		if (so.craft == NONE8 && !so.transit) add_row(SELL_SOLDIER, s);
	}
	for (uint8_t c = 0; c < MAX_CRAFTS; c++) {
		craft_t *cr = &ST->craft[c];
		if (cr->type != NONE8 && cr->base == b && !cr->transit && cr->status != CS_OUT) add_row(SELL_CRAFT, c);
	}
	if (ST->base[b].scientists) add_row(BUY_SCIENTIST, 0);
	if (ST->base[b].engineers) add_row(BUY_ENGINEER, 0);
	for (uint8_t i = 0; i < MAX_ITEMS; i++) if (ST->base[b].items[i]) add_row(BUY_ITEM, i);
}

static void sell_open(void)
{
	rows_begin();
	base_caps(ctx.base, &avail, &used);
	stock_rows(ctx.base);
	rows_done();
}

static uint16_t sell_src(uint8_t r)
{
	uint8_t b = ctx.base;
	switch (row_kind[r]) {
	case SELL_SOLDIER: case SELL_CRAFT: return 1;
	case BUY_SCIENTIST: return ST->base[b].scientists;
	case BUY_ENGINEER: return ST->base[b].engineers;
	case BUY_ITEM: return ST->base[b].items[row_what[r]];
	}
	return 0;
}

static int32_t sell_price(uint8_t r)
{
	switch (row_kind[r]) {
	case SELL_CRAFT: return price(BUY_CRAFT, ST->craft[row_what[r]].type, 1);
	case BUY_ITEM: return price(BUY_ITEM, row_what[r], 1);
	}
	return 0;
}

// SellState::changeByValue
static void sell_change(uint8_t r, int8_t dir, uint16_t change)
{
	uint16_t src = sell_src(r);
	if (dir > 0) { if (src <= row_amt[r]) return; if (change > src - row_amt[r]) change = src - row_amt[r]; }
	else { if (!row_amt[r]) return; if (change > row_amt[r]) change = row_amt[r]; }
	row_amt[r] += dir > 0 ? change : -change;
	total += (dir > 0 ? 1 : -1) * sell_price(r) * change;
	if (row_kind[r] == BUY_ITEM) iq -= (dir > 0 ? 1 : -1) * (int32_t)change * item_size(row_what[r]);
}

// ---------------------------------------------------------------- переводы

static const wdef_t w_trbase[] = {
	WIN(20, 30, 280, 140, UI_EL_WINDOW),
	BTN(28, 146, 264, 16, UI_EL_BUTTON, STR_CANCEL, A_POP, 0, ESC),
	TXT(25, 38, 270, 17, UI_EL_TEXT, STR_SELECT_DESTINATION_BASE, BIG | TC),
	TXT(30, 54, 250, 9, UI_EL_TEXT, DYN(1), 0),
	TXT(28, 64, 130, 17, UI_EL_TEXT, STR_NAME, BIG),
	TXT(160, 64, 130, 17, UI_EL_TEXT, STR_AREA, BIG),
	LST(30, 80, 246, 64, UI_EL_LIST, 0, WF_SEL),
};

static const wdef_t w_tritems[] = {
	WIN(0, 0, 320, 200, UI_EL_WINDOW),
	BTN(8, 176, 148, 16, UI_EL_BUTTON, STR_TRANSFER, A_PUSH, SCR_TRANSFER_CONFIRM, ENT),
	BTN(164, 176, 148, 16, UI_EL_BUTTON, STR_CANCEL, A_POP2, 0, ESC),
	TXT(5, 8, 310, 17, UI_EL_TEXT, STR_TRANSFER, BIG | TC),
	TXT(150, 24, 50, 9, UI_EL_TEXT, STR_QUANTITY_UC, 0),
	TXT(200, 24, 60, 17, UI_EL_TEXT, STR_AMOUNT_TO_TRANSFER, TW),
	TXT(260, 24, 60, 17, UI_EL_TEXT, STR_AMOUNT_AT_DESTINATION, TW),
	LST(10, 44, 285, 128, UI_EL_LIST, 0, 0),
	CMB(10, 24, 120, 16, UI_EL_TEXT, DYN(4), 2),
};

static const wdef_t w_trconfirm[] = {
	WIN(0, 60, 320, 80, UI_EL_WINDOW),
	BTN(176, 115, 128, 16, UI_EL_BUTTON, STR_CANCEL_UC, A_POP, 0, ESC),
	BTN(16, 115, 128, 16, UI_EL_BUTTON, STR_OK, A_CUSTOM, 1, ENT),
	TXT(5, 75, 310, 17, UI_EL_TEXT, DYN(0), BIG | TC),
	TXT(110, 95, 60, 17, UI_EL_TEXT, STR_COST, BIG),
	TXT(170, 95, 100, 17, UI_EL_TEXT, DYN(1), BIG),
};

static const wdef_t w_transfers[] = {
	WINP(0, 8, 320, 184, UI_EL_WINDOW, POPB),
	BTN(16, 166, 288, 16, UI_EL_BUTTON, STR_OK, A_POP, 0, ESC),
	HOT(0, 0, 0, 0, A_POP, 0, ENT),
	TXT(21, 18, 278, 17, UI_EL_TEXT, STR_TRANSFERS, BIG | TC),
	TXT(16, 34, 114, 9, UI_EL_TEXT, STR_ITEM, 0),
	TXT(152, 34, 54, 9, UI_EL_TEXT, STR_QUANTITY_UC, 0),
	TXT(212, 34, 108, 9, UI_EL_TEXT, STR_ARRIVAL_TIME_HOURS, 0),
	LST(16, 50, 271, 112, UI_EL_LIST, 0, WF_SEL),
};

// k-я база кроме текущей
static uint8_t other_base(uint8_t k)
{
	for (uint8_t b = 0; b < MAX_BASES; b++)
		if (ST->base[b].name[0] && b != ctx.base) { if (!k) return b; k--; }
	return NONE8;
}

static void tr_open(void)
{
	rows_begin();
	base_caps(tr_to, &avail, &used);           // проверки — по базе назначения
	tr_d16 = base_dist16(ctx.base, tr_to);
	stock_rows(ctx.base);
	rows_done();
}

// Стоимость строки перевода: солдат, учёный, инженер — 5 * d, корабль — 25 * d, предмет — d
static int32_t tr_cost(uint8_t r)
{
	uint8_t k = row_kind[r];
	uint16_t m = k == SELL_CRAFT ? 25 : k == BUY_ITEM ? 1 : 5;
	return (int32_t)m * tr_d16 / 16;
}

static uint8_t crew(uint8_t c) { return soldiers_count(ST->craft[c].base, c, 0); }

static int32_t craft_items_size(uint8_t c)
{
	int32_t s = 0;
	uint8_t cg = ST->craft[c].cargo;
	if (cg == NONE8) return 0;
	for (uint8_t k = 0; k < CARGO_ITEMS; k++)
		if (ST->cargo[cg].it[k].item != NONE8) s += (int32_t)ST->cargo[cg].it[k].qty * item_size(ST->cargo[cg].it[k].item);
	return s;
}

// TransferItemsState::increaseByValue (проверки — у базы назначения)
static void tr_more(uint8_t r, uint16_t change)
{
	uint8_t kind = row_kind[r], what = row_what[r];
	uint16_t src = sell_src(r);
	if (src <= row_amt[r]) return;
	int16_t fq = (int16_t)avail.quarters - (int16_t)used.quarters;
	uint8_t person = kind == SELL_SOLDIER || kind == BUY_SCIENTIST || kind == BUY_ENGINEER;
	uint8_t alien = 0;
	int16_t size = 0;
	if (kind == BUY_ITEM) {
		rtab_t t;
		rtab_open(RES_RULE_ITEMS, &t);
		alien = (rtab_word(&t, what, offsetof(r_items_t, flags)) & ITEMS_F_LIVE_ALIEN) != 0;
		size = (int16_t)rtab_word(&t, what, offsetof(r_items_t, size));
	}
	if (person && pq + 1 > fq) { error(STR_NO_FREE_ACCOMODATION); return; }
	if (kind == SELL_CRAFT) {
		if (cq + 1 > (int16_t)avail.hangars - (int16_t)used.hangars) { error(STR_NO_FREE_HANGARS_FOR_TRANSFER); return; }
		if (pq + crew(what) > fq) { error(STR_NO_FREE_ACCOMODATION_CREW); return; }
	}
	if (kind == BUY_ITEM && !alien && (int32_t)used.stores + iq + size > (int32_t)avail.stores) { error(STR_NOT_ENOUGH_STORE_SPACE); return; }
	if (alien && !avail.aliens) { error(STR_NO_ALIEN_CONTAINMENT_FOR_TRANSFER); return; }
	uint16_t rem = src - row_amt[r];
	if (person) {
		change = change < (uint16_t)(fq - pq) ? change : (uint16_t)(fq - pq);
		pq += change;
	} else if (kind == SELL_CRAFT) {
		change = 1;
		cq++;
		pq += crew(what);
		iq += craft_items_size(what);
	} else if (!alien && size > 0) {
		int32_t room = ((int32_t)avail.stores - (int32_t)used.stores - iq + 5) / size;
		if (change > room) change = (uint16_t)room;
	}
	if (change > rem) change = rem;
	if (kind == BUY_ITEM && !alien) iq += (int32_t)change * size;
	row_amt[r] += change;
	total += tr_cost(r) * change;
}

static void tr_less(uint8_t r, uint16_t change)
{
	uint8_t kind = row_kind[r], what = row_what[r];
	if (!row_amt[r]) return;
	if (change > row_amt[r]) change = row_amt[r];
	if (kind == SELL_SOLDIER || kind == BUY_SCIENTIST || kind == BUY_ENGINEER) pq -= change;
	else if (kind == SELL_CRAFT) { cq--; pq -= crew(what); iq -= craft_items_size(what); }
	else if (!(item_flags(what) & ITEMS_F_LIVE_ALIEN)) iq -= (int32_t)change * item_size(what);
	row_amt[r] -= change;
	total -= tr_cost(r) * change;
}

// TransferItemsState::completeTransfer: время 6 + d/10 часов, деньги, перевод
static void tr_complete(void)
{
	uint8_t from = ctx.base, to = tr_to;
	uint8_t hours = (uint8_t)(6 + tr_d16 / 160);
	funds_add(-total);
	for (uint8_t r = 0; r < nrows; r++) {
		uint16_t q = row_amt[r];
		if (!q) continue;
		uint8_t w = row_what[r];
		switch (row_kind[r]) {
		case SELL_SOLDIER: {
			soldier_t s;
			soldier_get(w, &s);
			s.base = to; s.transit = hours; s.flags &= ~SF_PSI;
			soldier_put(w, &s);
			break;
		}
		case SELL_CRAFT: {                          // корабль везёт экипаж и груз
			uint8_t n = ST->nsoldiers;
			for (uint8_t i = 0; i < n; i++) {
				soldier_t s;
				soldier_get(i, &s);
				if (s.base != from || s.craft != w) continue;
				s.base = to; s.transit = hours; s.flags &= ~SF_PSI;
				soldier_put(i, &s);
			}
			ST->craft[w].base = to;
			ST->craft[w].transit = hours;
			break;
		}
		case BUY_SCIENTIST: ST->base[from].scientists -= q; transfer_add(to, TK_SCIENTIST, 0, q, hours); break;
		case BUY_ENGINEER: ST->base[from].engineers -= q; transfer_add(to, TK_ENGINEER, 0, q, hours); break;
		case BUY_ITEM: ST->base[from].items[w] -= q; transfer_add(to, TK_ITEM, w, q, hours); break;
		}
	}
}

// TransfersState: поставки на базу (доставки, солдаты и корабли в пути)
static uint8_t tfs_row(uint8_t k, char *buf)
{
	uint8_t b = ctx.base, n = 0;
	for (uint8_t i = 0; i < MAX_TRANSFERS; i++) {
		transfer_t *t = &ST->transfer[i];
		if (t->base != b) continue;
		if (n++ != k) continue;
		if (buf) {
			if (t->kind == TK_ITEM) { rtab_t ti; rtab_open(RES_RULE_ITEMS, &ti); str_copy(rtab_word(&ti, t->item, 0), buf, 64); }
			else str_copy(t->kind == TK_SCIENTIST ? STR_SCIENTISTS : STR_ENGINEERS, buf, 64);
			strcat(buf, "\t"); fmt_num(buf + strlen(buf), t->qty, 0);
			strcat(buf, "\t"); fmt_num(buf + strlen(buf), t->hours, 0);
		}
		return 1;
	}
	uint8_t ns = ST->nsoldiers;
	for (uint8_t i = 0; i < ns; i++) {
		soldier_t s;
		soldier_get(i, &s);
		if (s.base != b || !s.transit) continue;
		if (n++ != k) continue;
		if (buf) { strcpy(buf, s.name); strcat(buf, "\t1\t"); fmt_num(buf + strlen(buf), s.transit, 0); }
		return 1;
	}
	for (uint8_t c = 0; c < MAX_CRAFTS; c++) {
		craft_t *cr = &ST->craft[c];
		if (cr->type == NONE8 || cr->base != b || !cr->transit) continue;
		if (n++ != k) continue;
		if (buf) { craft_name(c, buf); strcat(buf, "\t1\t"); fmt_num(buf + strlen(buf), cr->transit, 0); }
		return 1;
	}
	return 0;
}

// ---------------------------------------------------------------- таблица, тексты, события

static const scr_t tab[] = {
	SCR(SCR_PURCHASE, UI_SCR_BUYMENU, NOUI, RES_BACK13_SCR, 0, w_buy),
	SCR(SCR_SELL, UI_SCR_SELLMENU, NOUI, RES_BACK13_SCR, 0, w_sell),
	SCR(SCR_TRANSFER_BASE, UI_SCR_TRANSFERBASESELECT, NOUI, RES_BACK13_SCR, 0, w_trbase),
	SCR(SCR_TRANSFER_ITEMS, UI_SCR_TRANSFERMENU, NOUI, RES_BACK13_SCR, 0, w_tritems),
	SCR(SCR_TRANSFER_CONFIRM, UI_SCR_TRANSFERCONFIRM, NOUI, RES_BACK13_SCR, SF_POPUP, w_trconfirm),
	SCR(SCR_TRANSFERS, UI_SCR_TRANSFERINFO, NOUI, RES_BACK13_SCR, SF_POPUP, w_transfers),
};

uint8_t base2_get(uint8_t id, sdef_t *s, wdef_t *w) __banked
{
	if (combo.changed && (id == SCR_PURCHASE || id == SCR_SELL || id == SCR_TRANSFER_ITEMS)) { combo.changed = 0; filter(); }
	return scr_find(tab, sizeof tab / sizeof tab[0], id, s, w);
}

static void space_text(char *buf, int32_t change)
{
	int32_t u = ((int32_t)used.stores + change + 50) / 100;
	fmt_num(t1, u, 0);
	strcat(t1, ":");
	fmt_num(t1 + strlen(t1), (int32_t)avail.stores / 100, 0);
	str_fmt(buf, str_get(STR_SPACE_USED), t1, "");
}

// Ячейка строки: при ненулевом количестве вся строка — второй цвет ({ALT} в каждой ячейке)
static void cell(char *buf, uint8_t hi)
{
	strcat(buf, hi ? "\t\x01" : "\t");
}

// Начало строки: цвет ammoColor для боеприпасов без количества
static char *row_start(char *buf, uint8_t r, uint8_t hi)
{
	if (hi) { strcpy(buf, "\x01"); return buf + 1; }
	if (row_kind[r] == BUY_ITEM && is_ammo(row_what[r])) {
		buf[0] = 3; buf[1] = (char)ui_color(UI_EL_AMMOCOLOR, 0); buf[2] = 0;
		return buf + 2;
	}
	return buf;
}

void base2_text(uint8_t id, uint8_t slot, uint8_t row, char *buf) __banked
{
	if (slot == 4) { str_copy(combo.item[combo.sel], buf, 64); return; }
	switch (id) {
	case SCR_TRANSFER_BASE:
		if (slot == 1) { fmt_funds(t1, ST->funds); str_fmt(buf, str_get(STR_CURRENT_FUNDS), t1, ""); return; }
		if (row == LIST_COLS) {
			static const uint8_t cw[] = { 130, 116 };
			list_cols(buf, 2, cw, 0);
		} else {
			uint8_t b = other_base(row);
			if (b == NONE8) return;
			rtab_t t;
			strcpy(buf, ST->base[b].name);
			strcat(buf, "\t\x01");
			uint8_t rg = region_at((uint16_t)((uint32_t)ST->base[b].pos.lon >> 16), (int16_t)(ST->base[b].pos.lat >> 16));
			rtab_open(RES_RULE_REGIONS, &t);
			if (rg != NONE8) str_copy(rtab_word(&t, rg, 0), buf + strlen(buf), 64);
		}
		return;
	case SCR_TRANSFER_CONFIRM:
		if (slot == 0) { str_fmt(buf, str_get(STR_TRANSFER_ITEMS_TO), ST->base[tr_to].name, ""); return; }
		strcpy(buf, "\x01");
		fmt_funds(buf + 1, total);
		return;
	case SCR_TRANSFERS:
		if (row == LIST_COLS) {
			static const uint8_t cw[] = { 155, 75, 46 };
			list_cols(buf, 3, cw, 0);
		} else tfs_row(row, buf);
		return;
	}
	if (slot == 1) { fmt_funds(t1, ST->funds); str_fmt(buf, str_get(id == SCR_PURCHASE ? STR_CURRENT_FUNDS : STR_FUNDS), t1, ""); return; }
	if (slot == 2) { fmt_funds(t1, total); str_fmt(buf, str_get(id == SCR_PURCHASE ? STR_COST_OF_PURCHASES : STR_VALUE_OF_SALES), t1, ""); return; }
	if (slot == 3) { space_text(buf, iq); return; }
	if (row == LIST_COLS) {
		static const uint8_t cb[] = { 150, 55, 50, 32 }, cs[] = { 156, 54, 24, 53 }, ct[] = { 162, 58, 40, 27 };
		if (id == SCR_PURCHASE) list_cols_arrows(buf, 4, cb, 0, 225);
		else if (id == SCR_SELL) list_cols_arrows(buf, 4, cs, 0, 180);
		else list_cols_arrows(buf, 4, ct, 0, 191);
		return;
	}
	if (row >= nvis) return;
	uint8_t r = vis[row];
	uint8_t hi = row_amt[r] != 0;
	char *p = row_start(buf, r, hi);
	uint8_t kind = row_kind[r], what = row_what[r];
	row_name(kind, what, p);
	if (id == SCR_PURCHASE) {
		cell(buf, hi); fmt_funds(buf + strlen(buf), price(kind, what, 0));
		cell(buf, hi); fmt_num(buf + strlen(buf), at_base(ctx.base, kind, what), 0);
		cell(buf, hi); fmt_num(buf + strlen(buf), row_amt[r], 0);
	} else if (id == SCR_SELL) {
		cell(buf, hi); fmt_num(buf + strlen(buf), sell_src(r) - row_amt[r], 0);
		cell(buf, hi); fmt_num(buf + strlen(buf), row_amt[r], 0);
		cell(buf, hi); fmt_funds(buf + strlen(buf), sell_price(r));
	} else {                                      // перевод: осталось | переводится | у назначения
		cell(buf, hi); fmt_num(buf + strlen(buf), sell_src(r) - row_amt[r], 0);
		cell(buf, hi); fmt_num(buf + strlen(buf), row_amt[r], 0);
		cell(buf, hi);
		uint16_t dst = kind == SELL_SOLDIER || kind == SELL_CRAFT ? 0 : at_base(tr_to, kind, what);
		fmt_num(buf + strlen(buf), dst, 0);
	}
}

uint8_t base2_rows(uint8_t id, uint8_t slot) __banked
{
	(void)slot;
	uint8_t n = 0;
	switch (id) {
	case SCR_TRANSFER_BASE: while (other_base(n) != NONE8) n++; return n;
	case SCR_TRANSFER_CONFIRM: return 0;
	case SCR_TRANSFERS: while (tfs_row(n, 0)) n++; return n;
	}
	return nvis;
}

uint8_t base2_event(uint8_t id, uint8_t ev, uint8_t arg) __banked
{
	uint8_t b = ctx.base;
	if (ev == EVT_OPEN) {
		if (id == SCR_PURCHASE) buy_open();
		else if (id == SCR_SELL) sell_open();
		else if (id == SCR_TRANSFER_ITEMS) tr_open();
		return 0;
	}
	if (id == SCR_TRANSFER_BASE) {
		if (ev == EVT_LIST) { uint8_t t = other_base(arg); if (t != NONE8) { tr_to = t; UI_GO(A_PUSH, SCR_TRANSFER_ITEMS); } }
		return 0;
	}
	if (id == SCR_TRANSFER_CONFIRM) {
		if (ev == EVT_BUTTON && arg == 1) { tr_complete(); UI_GO(A_POPN, 3); }
		return 0;
	}
	if (id == SCR_TRANSFERS) return 0;
	if (ev == EVT_BUTTON && arg == 2) {           // ComboBox категорий
		open_combo(id == SCR_TRANSFER_ITEMS ? 24 : 36, RES_BACK13_SCR,
			id == SCR_PURCHASE ? UI_SCR_BUYMENU : id == SCR_SELL ? UI_SCR_SELLMENU : UI_SCR_TRANSFERMENU);
		return 0;
	}
	if (ev == EVT_ARROW && arg < nvis) {
		uint8_t r = vis[arg];
		uint16_t ch = ui_arrow_max ? 0x7FFF : 1;
		if (id == SCR_PURCHASE) { if (ui_arrow_dir > 0) buy_more(r, ch); else buy_less(r, ch); }
		else if (id == SCR_SELL) sell_change(r, ui_arrow_dir, ch);
		else { if (ui_arrow_dir > 0) tr_more(r, ch); else tr_less(r, ch); }
		ui_dirty_row(0, arg);                     // только эта строка и итоги, не весь список
		if (id != SCR_TRANSFER_ITEMS) { ui_dirty(2); ui_dirty(3); }
		return 0;
	}
	if (ev == EVT_BUTTON && arg == 1) {
		for (uint8_t r = 0; r < nrows; r++) {
			if (!row_amt[r]) continue;
			if (id == SCR_PURCHASE) econ_buy(b, row_kind[r], row_what[r], row_amt[r]);
			else econ_sell(b, row_kind[r], row_what[r], row_amt[r]);
		}
		UI_GO(A_POP, 0);
	}
	return 0;
}
