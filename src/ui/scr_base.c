// Банк 5: база (BasescapeState, BaseInfoState, StoresState, MonthlyCostsState,
// BuildFacilitiesState, PlaceFacilityState, DismantleFacilityState, CraftsState,
// SoldiersState, SoldierInfoState, SackSoldierState) + своё окно имени сохранения.
// Раскладки — tmp/screens_basescape.json; логика — src/game/econ.c.
// Состояние (ST) подключено в Win3 на время вызовов (диспетчер screens.c).
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "tsconf.h"
#include "res.h"
#include "rules.h"
#include "text.h"
#include "gfx.h"
#include "pages.h"
#include "state.h"
#include "game.h"
#include "scrdef.h"

static char t1[80], t2[80];

static uint8_t is_done(uint8_t topic) { return (ST->discovered[topic >> 3] >> (topic & 7)) & 1; }

// Все requires записи (rlist) исследованы.
static uint8_t reqs_done(rtab_t *t, uint16_t rec, uint8_t off)
{
	uint8_t l[3];
	uint16_t r[8];
	far_read(t->base + 8 + (uint32_t)rec * t->size + off, l, 3);
	uint8_t n = l[2] > 8 ? 8 : l[2];
	if (!n) return 1;
	rtab_tail(t, l[0] | (l[1] << 8), r, n * 2);
	for (uint8_t i = 0; i < n; i++) if (r[i] < RES_BITS * 8 && !is_done((uint8_t)r[i])) return 0;
	return 1;
}

// MonthlyCostsState: строка аренды — корабль с арендой и исследованными requires.
static uint32_t rent_of(rtab_t *t, uint8_t ty)
{
	uint32_t rent;
	far_read(t->base + 8 + (uint32_t)ty * t->size + offsetof(r_crafts_t, cost_rent), &rent, 4);
	return rent && reqs_done(t, ty, offsetof(r_crafts_t, requires)) ? rent : 0;
}

static void msg(uint16_t str)
{
	str_copy(str, ui_msg, sizeof ui_msg);
	UI_GO(A_PUSH, SCR_ERROR);
}

// ---------------------------------------------------------------- вид базы (BaseView)

static uint8_t facgrid[BASE_SIZE * BASE_SIZE];

static void fill_grid(uint8_t b)
{
	rtab_t t;
	r_facilities_t f;
	rtab_open(RES_RULE_FACILITIES, &t);
	memset(facgrid, NONE8, sizeof facgrid);
	for (uint8_t i = 0; i < MAX_FACILITIES; i++) {
		uint8_t type = ST->base[b].fac[i].type;
		if (type == NONE8) continue;
		rtab_get(&t, type, &f);
		uint8_t fx = ST->base[b].fac[i].xy & 15, fy = ST->base[b].fac[i].xy >> 4;
		for (uint8_t y = fy; y < fy + f.size && y < BASE_SIZE; y++)
			for (uint8_t x = fx; x < fx + f.size && x < BASE_SIZE; x++) facgrid[y * BASE_SIZE + x] = i;
	}
}

static uint8_t cell_done(uint8_t b, uint8_t x, uint8_t y)
{
	uint8_t g = facgrid[y * BASE_SIZE + x];
	return g != NONE8 && !ST->base[b].fac[g].days;
}

static int8_t sel_gx = -1;                // клетка, где сейчас нарисована рамка (-1 — нет)

// BaseView::draw (tmp/screens_basescape.json base_view.draw_algorithm)
static void draw_base(uint8_t b, int16_t ox, int16_t oy)
{
	rtab_t t, tc;
	r_facilities_t f;
	sel_gx = -1;                             // вид перерисован — рамки на экране нет
	rtab_open(RES_RULE_FACILITIES, &t);
	rtab_open(RES_RULE_CRAFTS, &tc);
	fill_grid(b);
	gfx_fill(ox, oy, 192, 192, 0);
	for (uint8_t y = 0; y < BASE_SIZE; y++)
		for (uint8_t x = 0; x < BASE_SIZE; x++) gfx_sprite(RES_BASEBITS_PCK, 0, ox + x * 32, oy + y * 32);
	// формы (готовые / стройка)
	for (uint8_t i = 0; i < MAX_FACILITIES; i++) {
		facility_t *fc = &ST->base[b].fac[i];
		if (fc->type == NONE8) continue;
		rtab_get(&t, fc->type, &f);
		uint8_t fx = fc->xy & 15, fy = fc->xy >> 4, num = 0, out = f.size * f.size > 3 ? f.size * f.size : 3;
		for (uint8_t y = fy; y < fy + f.size; y++)
			for (uint8_t x = fx; x < fx + f.size; x++, num++)
				gfx_sprite(RES_BASEBITS_PCK, f.sprite_shape + num + (fc->days ? out : 0), ox + x * 32, oy + y * 32);
	}
	// коридоры между готовыми постройками: 7 — справа, 8 — снизу
	for (uint8_t i = 0; i < MAX_FACILITIES; i++) {
		facility_t *fc = &ST->base[b].fac[i];
		if (fc->type == NONE8 || fc->days) continue;
		rtab_get(&t, fc->type, &f);
		uint8_t fx = fc->xy & 15, fy = fc->xy >> 4, xr = fx + f.size, yb = fy + f.size;
		if (xr < BASE_SIZE)
			for (uint8_t y = fy; y < yb; y++)
				if (cell_done(b, xr, y)) gfx_sprite(RES_BASEBITS_PCK, 7, ox + xr * 32 - 16, oy + y * 32);
		if (yb < BASE_SIZE)
			for (uint8_t x = fx; x < xr; x++)
				if (cell_done(b, x, yb)) gfx_sprite(RES_BASEBITS_PCK, 8, ox + x * 32, oy + yb * 32 - 16);
	}
	// картинки, корабли в ангарах, дни стройки
	uint8_t ci = 0;                           // общий перебор кораблей базы по ангарам
	uint8_t cvcol = ui_color_in(UI_SCR_BASESCAPE, UI_EL_BASEVIEW, 0);   // вид базы — цвета basescape и в окне установки
	for (uint8_t i = 0; i < MAX_FACILITIES; i++) {
		facility_t *fc = &ST->base[b].fac[i];
		if (fc->type == NONE8) continue;
		rtab_get(&t, fc->type, &f);
		uint8_t fx = fc->xy & 15, fy = fc->xy >> 4;
		if (f.size == 1) gfx_sprite(RES_BASEBITS_PCK, f.sprite_facility, ox + fx * 32, oy + fy * 32);
		if (!fc->days && f.crafts) {
			while (ci < MAX_CRAFTS && (ST->craft[ci].type == NONE8 || ST->craft[ci].base != b || ST->craft[ci].transit)) ci++;
			if (ci < MAX_CRAFTS) {
				if (ST->craft[ci].status != CS_OUT) {
					int16_t spr = (int16_t)rtab_word(&tc, ST->craft[ci].type, offsetof(r_crafts_t, sprite));
					gfx_sprite(RES_BASEBITS_PCK, spr + 33, ox + fx * 32 + (f.size - 1) * 16 + 2, oy + fy * 32 + (f.size - 1) * 16 - 4);
				}
				ci++;
			}
		}
		if (fc->days) {
			tbox_t tb;
			fmt_num(t1, fc->days, 0);
			tb.x = ox + fx * 32; tb.y = oy + fy * 32 + (32 * f.size - 16) / 2; tb.w = 32 * f.size; tb.h = 16;
			tb.font = FNT_BIG; tb.color = tb.color2 = cvcol; tb.flags = TX_CENTER;
			text_draw(&tb, t1);
		}
	}
}

// MiniBaseView: 8 ячеек 16x16, выбранная — цвет 1, постройки 2x2 точки на клетку.
static void draw_mini(int16_t ox, int16_t oy)
{
	rtab_t t;
	r_facilities_t f;
	uint8_t green = ui_color(UI_EL_MINIBASE, 0), red = ui_color(UI_EL_MINIBASE, 1);
	rtab_open(RES_RULE_FACILITIES, &t);
	gfx_fill(ox, oy, 128, 16, 0);
	for (uint8_t i = 0; i < MAX_BASES; i++) {
		int16_t x0 = ox + i * 16;
		if (i == ctx.base) gfx_fill(x0, oy, 16, 16, 1);
		gfx_sprite(RES_BASEBITS_PCK, 41, x0, oy);
		if (!ST->base[i].name[0]) continue;
		for (uint8_t k = 0; k < MAX_FACILITIES; k++) {
			facility_t *fc = &ST->base[i].fac[k];
			if (fc->type == NONE8) continue;
			rtab_get(&t, fc->type, &f);
			uint8_t c = fc->days ? red : green, s = f.size * 2;
			int16_t rx = x0 + 2 + (fc->xy & 15) * 2, ry = oy + 2 + (fc->xy >> 4) * 2;
			gfx_fill(rx, ry, s, s, c + 3);
			gfx_fill(rx + 1, ry + 1, s - 1, s - 1, c + 5);
			gfx_fill(rx, ry, s - 1, s - 1, c + 2);
			if (s > 2) gfx_fill(rx + 1, ry + 1, s - 2, s - 2, c + 3);
			gfx_pset(rx, ry, c + 1);
		}
	}
	gfx_fill(ox, oy + 16, 136, 24, 0);        // низ кадров 32x40 рамки ячейки
}

static void mini_click(int16_t ox)
{
	uint8_t i = (uint8_t)((ui_click_x - ox) / 16);
	if (i < MAX_BASES && ST->base[i].name[0] && i != ctx.base) {
		ctx.base = i;
		ST->sel_base = i;
		UI_GO(A_REDRAW, 0);
	}
}

// ---------------------------------------------------------------- рамка установки

// BaseView::setSelectable / mouseOver / blink: рамка size*32 в 1 точку цвета
// basescape.baseView.color2 в клетке под курсором (если постройка влезает в сетку),
// мигает раз в 100 мс (5 кадров). Событий движения мыши нет — курсор опрашивается
// в EVT_TICK; точки под рамкой сохраняются и возвращаются (рамка — прямо на экране).
#define SEL_MAX 2                          // постройки UFO/TFTD — до 2x2 (Win1 почти полон)
static int8_t sel_gy;
static uint8_t sel_sz, sel_blink, sel_div;
static uint8_t sel_buf[SEL_MAX * 32 * 4];

// 0 — сохранить точки под рамкой, 1 — вернуть, 2 — нарисовать цветом c
static void sel_io(uint8_t mode, uint8_t c)
{
	int16_t x0 = sel_gx * 32, y0 = 8 + sel_gy * 32, w = sel_sz * 32;
	uint8_t old = pg_win3(), *s = sel_buf;
	for (uint8_t k = 0; k < 2; k++, s += w) {           // верх и низ
		uint8_t *p = gfx_map(x0, k ? y0 + w - 1 : y0);
		if (mode == 0) memcpy(s, p, w); else if (mode == 1) memcpy(p, s, w); else memset(p, c, w);
	}
	for (int16_t y = y0 + 1; y < y0 + w - 1; y++, s += 2) {   // левый и правый край
		uint8_t *p = gfx_map(x0, y);
		if (mode == 0) { s[0] = p[0]; s[1] = p[w - 1]; }
		else if (mode == 1) { p[0] = s[0]; p[w - 1] = s[1]; }
		else p[0] = p[w - 1] = c;
	}
	pg_map3(old);
}

static void sel_hide(void)
{
	if (sel_gx >= 0) { sel_io(1, 0); sel_gx = -1; }
}

static void sel_tick(uint8_t size)
{
	int16_t cx = cursor_x, cy = cursor_y - 8;
	int8_t gx = -1, gy = 0;
	if (size > SEL_MAX) size = SEL_MAX;
	if (cx >= 0 && cy >= 0 && cx < 32 * BASE_SIZE && cy < 32 * BASE_SIZE) {
		gx = (int8_t)(cx / 32); gy = (int8_t)(cy / 32);
		if (gx + size > BASE_SIZE || gy + size > BASE_SIZE) gx = -1;
	}
	if (++sel_div >= 5) { sel_div = 0; sel_blink ^= 1; }
	uint8_t show = gx >= 0 && sel_blink;
	if (sel_gx >= 0 && (!show || gx != sel_gx || gy != sel_gy)) sel_hide();
	if (show && sel_gx < 0) {
		sel_gx = gx; sel_gy = gy; sel_sz = size;
		sel_io(0, 0);
		sel_io(2, ui_color_in(UI_SCR_BASESCAPE, UI_EL_BASEVIEW, 1));
	}
}

// ---------------------------------------------------------------- Basescape

// BasescapeState::viewMouseOver: название постройки под курсором (у готового ангара —
// и корабль в нём) в строке над видом; курсор опрашивается в EVT_TICK.
static uint8_t hover_fi = NONE8, hover_cell = NONE8;

// Корабль ангара fi: ангары по порядку записей получают корабли базы по порядку (как draw_base)
static uint8_t hangar_craft(uint8_t b, uint8_t fi)
{
	rtab_t t;
	uint8_t k = 0;
	rtab_open(RES_RULE_FACILITIES, &t);
	for (uint8_t i = 0; i < fi; i++) {
		facility_t *fc = &ST->base[b].fac[i];
		if (fc->type != NONE8 && !fc->days && rtab_word(&t, fc->type, offsetof(r_facilities_t, crafts)) & 0xFF) k++;
	}
	for (uint8_t c = 0; c < MAX_CRAFTS; c++) {
		if (ST->craft[c].type == NONE8 || ST->craft[c].base != b || ST->craft[c].transit) continue;
		if (!k--) return c;
	}
	return NONE8;
}

static void hover_text(uint8_t b, char *buf)
{
	rtab_t t;
	r_facilities_t f;
	if (hover_fi == NONE8 || ST->base[b].fac[hover_fi].type == NONE8) return;
	rtab_open(RES_RULE_FACILITIES, &t);
	rtab_get(&t, ST->base[b].fac[hover_fi].type, &f);
	str_copy(f.name, buf, 64);
	if (!f.crafts || ST->base[b].fac[hover_fi].days) return;
	uint8_t c = hangar_craft(b, hover_fi);
	if (c == NONE8) return;
	craft_name(c, t1);
	strcat(buf, " ");
	str_fmt(buf + strlen(buf), str_get(STR_CRAFT_), t1, "");
}

// Подсказка сменилась — перерисовать только её (ui_dirty)
static void hover_tick(uint8_t b)
{
	int16_t cx = cursor_x, cy = cursor_y - 8;
	uint8_t cell = cx >= 0 && cy >= 0 && cx < 32 * BASE_SIZE && cy < 32 * BASE_SIZE ? (uint8_t)(cy / 32 * BASE_SIZE + cx / 32) : NONE8;
	uint8_t hc = hover_cell, hf = hover_fi;     // сравнение с копиями (ошибка SDCC 4.5, CLAUDE.md)
	if (cell == hc) return;
	hover_cell = cell;
	uint8_t fi = cell == NONE8 ? NONE8 : fac_at(b, cell % BASE_SIZE, cell / BASE_SIZE);
	if (fi == hf) return;
	hover_fi = fi;
	ui_dirty(1);
}

static const wdef_t w_base[] = {
	CUS(0, 8, 192, 192, NOSTR, A_CUSTOM, 1),   // не DYN: подсказка перерисовывает только тексты
	CUS(192, 41, 128, 16, NOSTR, A_CUSTOM, 2),
	TXT(0, 0, 192, 9, UI_EL_TEXTTOOLTIP, DYN(1), 0),
	TXT(193, 0, 127, 17, UI_EL_TEXT1, DYN(2), BIG),
	TXT(194, 16, 126, 9, UI_EL_TEXT2, DYN(3), 0),
	TXT(194, 24, 126, 9, UI_EL_TEXT3, DYN(4), 0),
	BTN(192, 58, 128, 12, UI_EL_BUTTON, STR_BUILD_NEW_BASE_UC, A_CUSTOM, 10, 0),
	BTN(192, 71, 128, 12, UI_EL_BUTTON, STR_BASE_INFORMATION, A_PUSH, SCR_BASE_INFO, 'i'),
	BTN(192, 84, 128, 12, UI_EL_BUTTON, STR_SOLDIERS_UC, A_PUSH, SCR_SOLDIERS, 's'),
	BTN(192, 97, 128, 12, UI_EL_BUTTON, STR_EQUIP_CRAFT, A_PUSH, SCR_CRAFTS, 'c'),
	BTN(192, 110, 128, 12, UI_EL_BUTTON, STR_BUILD_FACILITIES, A_PUSH, SCR_BUILD_FACILITIES, 'f'),
	BTN(192, 123, 128, 12, UI_EL_BUTTON, STR_RESEARCH, A_CUSTOM, 11, 'r'),
	BTN(192, 136, 128, 12, UI_EL_BUTTON, STR_MANUFACTURE, A_CUSTOM, 12, 'm'),
	BTN(192, 149, 128, 12, UI_EL_BUTTON, STR_TRANSFER_UC, A_CUSTOM, 13, 't'),
	BTN(192, 162, 128, 12, UI_EL_BUTTON, STR_PURCHASE_RECRUIT, A_PUSH, SCR_PURCHASE, 'p'),
	BTN(192, 175, 128, 12, UI_EL_BUTTON, STR_SELL_SACK_UC, A_PUSH, SCR_SELL, 'e'),
	BTN(192, 188, 128, 12, UI_EL_BUTTON, STR_GEOSCAPE_UC, A_POP, 0, ESC),
	HOT(0, 0, 0, 0, A_CUSTOM, 20, '1'), HOT(0, 0, 0, 0, A_CUSTOM, 21, '2'),
	HOT(0, 0, 0, 0, A_CUSTOM, 22, '3'), HOT(0, 0, 0, 0, A_CUSTOM, 23, '4'),
	HOT(0, 0, 0, 0, A_CUSTOM, 24, '5'), HOT(0, 0, 0, 0, A_CUSTOM, 25, '6'),
	HOT(0, 0, 0, 0, A_CUSTOM, 26, '7'), HOT(0, 0, 0, 0, A_CUSTOM, 27, '8'),
};

// ---------------------------------------------------------------- BaseInfo

static const wdef_t w_info[] = {
	IMG(0, 0, 320, 200, RES_BACK07_SCR),
	CUS(182, 8, 128, 16, NOSTR, A_CUSTOM, 2),
	BTN(10, 180, 30, 14, UI_EL_BUTTON, STR_OK, A_POP, 0, ESC),
	BTN(46, 180, 80, 14, UI_EL_BUTTON, STR_TRANSFERS_UC, A_CUSTOM, 13, 0),
	BTN(132, 180, 80, 14, UI_EL_BUTTON, STR_STORES_UC, A_PUSH, SCR_STORES, 0),
	BTN(218, 180, 92, 14, UI_EL_BUTTON, STR_MONTHLY_COSTS, A_PUSH, SCR_MONTHLY_COSTS, 0),
	TXT(8, 8, 127, 16, UI_EL_TEXT1, DYN(0), BIG),
	TXT(8, 30, 300, 9, UI_EL_TEXT1, STR_PERSONNEL_AVAILABLE_PERSONNEL_TOTAL, 0),
	TXT(8, 41, 114, 9, UI_EL_TEXT2, STR_SOLDIERS, 0),
	TXT(126, 41, 40, 9, UI_EL_NUMBERS, DYN(1), 0),
	{ W_BAR, 166, 43, 150, 5, UI_EL_PERSONNELBARS, DYN(21), 0, 0, 0, 0 },
	TXT(8, 51, 114, 9, UI_EL_TEXT2, STR_ENGINEERS, 0),
	TXT(126, 51, 40, 9, UI_EL_NUMBERS, DYN(2), 0),
	{ W_BAR, 166, 53, 150, 5, UI_EL_PERSONNELBARS, DYN(22), 0, 0, 0, 0 },
	TXT(8, 61, 114, 9, UI_EL_TEXT2, STR_SCIENTISTS, 0),
	TXT(126, 61, 40, 9, UI_EL_NUMBERS, DYN(3), 0),
	{ W_BAR, 166, 63, 150, 5, UI_EL_PERSONNELBARS, DYN(23), 0, 0, 0, 0 },
	TXT(8, 72, 300, 9, UI_EL_TEXT1, STR_SPACE_USED_SPACE_AVAILABLE, 0),
	TXT(8, 83, 114, 9, UI_EL_TEXT2, STR_LIVING_QUARTERS_PLURAL, 0),
	TXT(126, 83, 40, 9, UI_EL_NUMBERS, DYN(4), 0),
	{ W_BAR, 166, 85, 150, 5, UI_EL_FACILITYBARS, DYN(24), 0, 0, 0, 0 },
	TXT(8, 93, 114, 9, UI_EL_TEXT2, STR_STORES, 0),
	TXT(126, 93, 40, 9, UI_EL_NUMBERS, DYN(5), 0),
	{ W_BAR, 166, 95, 150, 5, UI_EL_FACILITYBARS, DYN(25), 0, 0, 0, 0 },
	TXT(8, 103, 114, 9, UI_EL_TEXT2, STR_LABORATORIES, 0),
	TXT(126, 103, 40, 9, UI_EL_NUMBERS, DYN(6), 0),
	{ W_BAR, 166, 105, 150, 5, UI_EL_FACILITYBARS, DYN(26), 0, 0, 0, 0 },
	TXT(8, 113, 114, 9, UI_EL_TEXT2, STR_WORK_SHOPS, 0),
	TXT(126, 113, 40, 9, UI_EL_NUMBERS, DYN(7), 0),
	{ W_BAR, 166, 115, 150, 5, UI_EL_FACILITYBARS, DYN(27), 0, 0, 0, 0 },
	TXT(8, 123, 114, 9, UI_EL_TEXT2, STR_HANGARS, 0),
	TXT(126, 123, 40, 9, UI_EL_NUMBERS, DYN(8), 0),
	{ W_BAR, 166, 125, 150, 5, UI_EL_FACILITYBARS, DYN(28), 0, 0, 0, 0 },
	TXT(8, 138, 114, 9, UI_EL_TEXT2, STR_DEFENSE_STRENGTH, 0),
	TXT(126, 138, 40, 9, UI_EL_NUMBERS, DYN(9), 0),
	{ W_BAR, 166, 140, 150, 5, UI_EL_DEFENCEBAR, DYN(29), 0, 0, 0, 0 },
	TXT(8, 153, 114, 9, UI_EL_TEXT2, STR_SHORT_RANGE_DETECTION, 0),
	TXT(126, 153, 40, 9, UI_EL_NUMBERS, DYN(10), 0),
	{ W_BAR, 166, 155, 150, 5, UI_EL_DETECTIONBARS, DYN(30), 0, 0, 0, 0 },
	TXT(8, 163, 114, 9, UI_EL_TEXT2, STR_LONG_RANGE_DETECTION, 0),
	TXT(126, 163, 40, 9, UI_EL_NUMBERS, DYN(11), 0),
	{ W_BAR, 166, 165, 150, 5, UI_EL_DETECTIONBARS, DYN(31), 0, 0, 0, 0 },
};

// Значения окна (пересчёт в EVT_OPEN): [i] = {available/used, total/available, bar value, bar max}
static uint16_t info_a[12], info_b[12];
static const uint8_t info_scale8[12] = { 0, 8, 8, 8, 4, 4, 4, 4, 144, 1, 200, 200 };   // масштаб * 8

static void info_calc(void)
{
	caps_t a, u;
	rtab_t t;
	r_facilities_t f;
	uint8_t b = ctx.base;
	uint16_t sci, eng;
	base_caps(b, &a, &u);
	base_personnel(b, &sci, &eng);
	uint16_t sold = soldiers_count(b, 0xFE, 0);
	info_a[1] = soldiers_count(b, NONE8, 0);  info_b[1] = sold;     // без корабля / всего
	info_a[2] = ST->base[b].engineers;       info_b[2] = eng;
	info_a[3] = ST->base[b].scientists;      info_b[3] = sci;
	info_a[4] = u.quarters;                  info_b[4] = a.quarters;
	info_a[5] = (uint16_t)((u.stores + 5) / 100); info_b[5] = (uint16_t)(a.stores / 100);
	info_a[6] = u.labs;                      info_b[6] = a.labs;
	info_a[7] = u.workshops;                 info_b[7] = a.workshops;
	info_a[8] = u.hangars;                   info_b[8] = a.hangars;
	// оборона и обнаружение (Base::getDefenseValue / get*RangeDetection)
	uint16_t def = 0, minr = 0xFFFF;
	uint8_t sr = 0, lr = 0;
	rtab_open(RES_RULE_FACILITIES, &t);
	for (uint8_t i = 0; i < t.n; i++) {
		uint16_t rr = rtab_word(&t, i, offsetof(r_facilities_t, radar_range));
		if (rr && rr < minr) minr = rr;
	}
	for (uint8_t i = 0; i < MAX_FACILITIES; i++) {
		facility_t *fc = &ST->base[b].fac[i];
		if (fc->type == NONE8 || fc->days) continue;
		rtab_get(&t, fc->type, &f);
		def += f.defense;
		if (f.radar_range && f.radar_range == minr) sr++;
		else if (f.radar_range > minr) lr++;
	}
	info_a[9] = def; info_b[9] = def;
	info_a[10] = sr; info_b[10] = sr;
	info_a[11] = lr; info_b[11] = lr;
}

// ---------------------------------------------------------------- склад, расходы

static const wdef_t w_stores[] = {
	WIN(0, 0, 320, 200, UI_EL_WINDOW),
	BTN(10, 176, 300, 16, UI_EL_BUTTON, STR_OK, A_POP, 0, ESC),
	TXT(5, 8, 310, 17, UI_EL_TEXT, STR_STORES, BIG | TC),
	TXT(10, 32, 142, 9, UI_EL_TEXT, STR_ITEM, 0),
	TXT(152, 32, 88, 9, UI_EL_TEXT, STR_QUANTITY_UC, 0),
	TXT(240, 32, 74, 9, UI_EL_TEXT, STR_SPACE_USED_UC, 0),
	LST(10, 40, 286, 128, UI_EL_LIST, 0, 0),
};

static const wdef_t w_costs[] = {
	WIN(0, 0, 320, 200, UI_EL_WINDOW),
	BTN(10, 170, 300, 20, UI_EL_BUTTON, STR_OK, A_POP, 0, ESC),
	TXT(5, 12, 310, 17, UI_EL_TEXT1, STR_MONTHLY_COSTS, BIG | TC),
	TXT(115, 32, 80, 9, UI_EL_TEXT1, STR_COST_PER_UNIT, 0),
	TXT(195, 32, 55, 9, UI_EL_TEXT1, STR_QUANTITY, 0),
	TXT(249, 32, 60, 9, UI_EL_TEXT1, STR_TOTAL, 0),
	TXT(10, 40, 150, 9, UI_EL_TEXT1, STR_CRAFT_RENTAL, 0),
	LST(10, 48, 288, 32, UI_EL_LIST, 0, 0),
	TXT(10, 80, 150, 9, UI_EL_TEXT1, STR_SALARIES, 0),
	LST(10, 88, 288, 40, UI_EL_LIST, 1, 0),
	LST(10, 128, 300, 9, UI_EL_TEXT1, 2, 0),
	TXT(10, 146, 150, 9, UI_EL_LIST, DYN(3), 0),
	TXT(10, 154, 150, 9, UI_EL_LIST, DYN(4), 0),
	LST(205, 150, 100, 9, UI_EL_TEXT2, 5, 0),
};

// ---------------------------------------------------------------- постройки

static const wdef_t w_build[] = {
	WINP(192, 40, 128, 160, UI_EL_WINDOW, POPV),
	BTN(200, 176, 112, 16, UI_EL_BUTTON, STR_OK, A_POP, 0, ESC),
	TXT(197, 48, 118, 17, UI_EL_TEXT, STR_INSTALLATION, BIG | TC),
	LST(202, 64, 102, 104, UI_EL_LIST, 0, WF_SEL | TW),
};
static uint8_t build_list[32], build_n, place_size;

static const wdef_t w_place[] = {
	WIN(192, 40, 128, 160, UI_EL_WINDOW),
	CUS(0, 8, 192, 192, NOSTR, A_CUSTOM, 1),
	BTN(200, 176, 112, 16, UI_EL_BUTTON, STR_CANCEL, A_POP, 0, ESC),
	TXT(202, 50, 110, 9, UI_EL_TEXT, DYN(0), 0),
	TXT(202, 62, 110, 9, UI_EL_TEXT, STR_COST_UC, 0),
	TXT(202, 70, 110, 17, UI_EL_NUMBERS, DYN(1), BIG),
	TXT(202, 90, 110, 9, UI_EL_TEXT, STR_CONSTRUCTION_TIME_UC, 0),
	TXT(202, 98, 110, 17, UI_EL_NUMBERS, DYN(2), BIG),
	TXT(202, 118, 110, 9, UI_EL_TEXT, STR_MAINTENANCE_UC, 0),
	TXT(202, 126, 110, 17, UI_EL_NUMBERS, DYN(3), BIG),
};

static const wdef_t w_dismantle[] = {
	WIN(20, 60, 152, 80, UI_EL_WINDOW),
	BTN(36, 115, 44, 16, UI_EL_BUTTON, STR_OK, A_CUSTOM, 1, ENT),
	BTN(112, 115, 44, 16, UI_EL_BUTTON, STR_CANCEL_UC, A_POP, 0, ESC),
	TXT(25, 75, 142, 9, UI_EL_TEXT, STR_DISMANTLE, TC),
	TXT(25, 85, 142, 9, UI_EL_TEXT, DYN(0), TC),
};

// ---------------------------------------------------------------- корабли, солдаты

static const wdef_t w_crafts[] = {
	WIN(0, 0, 320, 200, UI_EL_WINDOW),
	BTN(16, 176, 288, 16, UI_EL_BUTTON, STR_OK, A_POP, 0, ESC),
	TXT(16, 8, 298, 17, UI_EL_TEXT, STR_INTERCEPTION_CRAFT, BIG),
	TXT(16, 24, 298, 17, UI_EL_TEXT, DYN(1), BIG),
	TXT(16, 40, 94, 9, UI_EL_TEXT, STR_NAME_UC, 0),
	TXT(110, 40, 50, 9, UI_EL_TEXT, STR_STATUS, 0),
	TXT(160, 40, 50, 17, UI_EL_TEXT, STR_WEAPON_SYSTEMS, TW),
	TXT(210, 40, 58, 9, UI_EL_TEXT, STR_CREW, 0),
	TXT(268, 40, 46, 9, UI_EL_TEXT, STR_HWPS, 0),
	LST(16, 58, 280, 118, UI_EL_LIST, 0, WF_SEL),
};

static const wdef_t w_soldiers[] = {
	WIN(0, 0, 320, 200, UI_EL_WINDOW),
	BTN(164, 176, 148, 16, UI_EL_BUTTON, STR_OK, A_POP, 0, ESC),
	TXT(5, 8, 310, 17, UI_EL_TEXT1, STR_SOLDIER_LIST, BIG | TC),
	TXT(16, 32, 114, 9, UI_EL_TEXT2, STR_NAME_UC, 0),
	TXT(130, 32, 102, 9, UI_EL_TEXT2, STR_RANK, 0),
	TXT(222, 32, 82, 9, UI_EL_TEXT2, STR_CRAFT, 0),
	LST(16, 40, 280, 128, UI_EL_LIST, 0, WF_SEL),
};

// SoldierInfo: имя — поле ввода (переименование), 11 строк характеристик.
#define SI_ROWS 11
static const wdef_t w_sinfo[] = {
	IMG(0, 0, 320, 200, RES_BACK06_SCR),
	BTN(30, 33, 48, 14, UI_EL_BUTTON, STR_OK, A_CUSTOM, 1, ENT),
	BTN(0, 33, 28, 14, UI_EL_BUTTON, DYN(40), A_CUSTOM, 2, 0),
	BTN(80, 33, 28, 14, UI_EL_BUTTON, DYN(41), A_CUSTOM, 3, 0),
	BTN(260, 33, 60, 14, UI_EL_BUTTON, STR_SACK, A_PUSH, SCR_SACK_SOLDIER, 0),
	{ W_EDIT, 40, 9, 210, 16, UI_EL_TEXT1, NOSTR, BIG, 0, SOLDIER_NAME - 1, 0 },
	TXT(0, 48, 130, 9, UI_EL_TEXT1, DYN(1), 0),
	TXT(130, 48, 100, 9, UI_EL_TEXT1, DYN(2), 0),
	TXT(200, 48, 100, 9, UI_EL_TEXT1, DYN(3), 0),
	TXT(0, 56, 130, 9, UI_EL_TEXT1, DYN(4), 0),
	TXT(130, 56, 180, 9, UI_EL_TEXT1, DYN(5), 0),
	TXT(0, 66, 150, 9, UI_EL_TEXT2, DYN(6), 0),
	HOT(0, 0, 0, 0, A_CUSTOM, 1, ESC),
};
static const uint16_t stat_str[SI_ROWS] = {
	STR_TIME_UNITS, STR_STAMINA, STR_HEALTH, STR_BRAVERY, STR_REACTIONS, STR_FIRING_ACCURACY,
	STR_THROWING_ACCURACY, STR_STRENGTH, STR_PSIONIC_STRENGTH, STR_PSIONIC_SKILL, STR_MELEE_ACCURACY,
};
static const uint8_t stat_el[SI_ROWS] = {
	UI_EL_BARTUS, UI_EL_BARENERGY, UI_EL_BARHEALTH, UI_EL_BARBRAVERY, UI_EL_BARREACTIONS, UI_EL_BARFIRING,
	UI_EL_BARTHROWING, UI_EL_BARSTRENGTH, UI_EL_BARPSISTRENGTH, UI_EL_BARPSISKILL, UI_EL_BARMELEE,
};
static soldier_t sol;                    // открытый солдат (копия)
static uint8_t sol_i;

static const wdef_t w_sack[] = {
	WIN(84, 60, 152, 80, UI_EL_WINDOW),
	BTN(100, 115, 44, 16, UI_EL_BUTTON, STR_OK, A_CUSTOM, 1, ENT),
	BTN(176, 115, 44, 16, UI_EL_BUTTON, STR_CANCEL_UC, A_POP, 0, ESC),
	TXT(89, 75, 142, 9, UI_EL_TEXT, STR_SACK, TC),
	TXT(89, 85, 142, 9, UI_EL_TEXT, DYN(0), TC),
};

// PlaceLiftState: вид новой базы без окна, клик по клетке — лифт (сразу готов),
// затем экран этой базы. Рамка под курсором — sel_tick, как у PlaceFacility.
static const wdef_t w_placelift[] = {
	CUS(0, 8, 192, 192, NOSTR, A_CUSTOM, 1),
	TXT(0, 0, 320, 9, UI_EL_TEXT, STR_SELECT_POSITION_FOR_ACCESS_LIFT, 0),
};

// Имя сохранения (своё окно): поле ввода + OK/отмена; слот — ctx.item.
static const wdef_t w_savename[] = {
	WIN(32, 60, 256, 80, UI_EL_WINDOW),
	TXT(37, 70, 246, 17, UI_EL_TEXT, STR_SAVE_GAME, BIG | TC),
	{ W_EDIT, 48, 92, 224, 16, UI_EL_TEXT, NOSTR, BIG, 0, SAVE_NAME - 1, 0 },
	BTN(48, 116, 108, 16, UI_EL_BUTTON, STR_OK, A_CUSTOM, 1, ENT),
	BTN(164, 116, 108, 16, UI_EL_BUTTON, STR_CANCEL, A_POP, 0, ESC),
};

// ---------------------------------------------------------------- таблица экранов

static const scr_t tab[] = {
	SCR(SCR_BASESCAPE, UI_SCR_BASESCAPE, NOUI, 0, 0, w_base),
	SCR(SCR_BASE_INFO, UI_SCR_BASEINFO, NOUI, 0, 0, w_info),
	SCR(SCR_STORES, UI_SCR_STORESINFO, NOUI, RES_BACK13_SCR, 0, w_stores),
	SCR(SCR_MONTHLY_COSTS, UI_SCR_COSTSINFO, NOUI, RES_BACK13_SCR, 0, w_costs),
	SCR(SCR_BUILD_FACILITIES, UI_SCR_SELECTFACILITY, NOUI, RES_BACK05_SCR, SF_POPUP, w_build),
	SCR(SCR_PLACE_FACILITY, UI_SCR_PLACEFACILITY, NOUI, RES_BACK01_SCR, SF_POPUP, w_place),
	SCR(SCR_DISMANTLE, UI_SCR_DISMANTLEFACILITY, NOUI, RES_BACK13_SCR, SF_POPUP, w_dismantle),
	SCR(SCR_CRAFTS, UI_SCR_CRAFTSELECT, NOUI, RES_BACK14_SCR, 0, w_crafts),
	SCR(SCR_SOLDIERS, UI_SCR_SOLDIERLIST, NOUI, RES_BACK02_SCR, 0, w_soldiers),
	SCR(SCR_SOLDIER_INFO, UI_SCR_SOLDIERINFO, NOUI, 0, 0, w_sinfo),
	SCR(SCR_SACK_SOLDIER, UI_SCR_SACKSOLDIER, NOUI, RES_BACK13_SCR, SF_POPUP, w_sack),
	SCR(SCR_SAVE_NAME, UI_SCR_SAVEMENUS, UI_SCR_GEOSCAPE, RES_BACK01_SCR, SF_POPUP | SF_ALTPAL, w_savename),
	SCR(SCR_PLACE_LIFT, UI_SCR_PLACEFACILITY, NOUI, 0, 0, w_placelift),
};

static void add_w(sdef_t *s, wdef_t *w, uint8_t type, int16_t x, int16_t y, int16_t ww, int16_t hh, uint8_t el, uint16_t str, uint8_t fl)
{
	if (s->n >= SDEF_MAXW) return;
	wdef_t *b = &w[s->n++];
	b->type = type; b->x = x; b->y = y; b->w = ww; b->h = hh;
	b->el = el; b->str = str; b->flags = fl; b->act = 0; b->arg = 0; b->key = 0;
}

uint8_t base_get(uint8_t id, sdef_t *s, wdef_t *w) __banked
{
	if (!scr_find(tab, sizeof tab / sizeof tab[0], id, s, w)) return 0;
	if (id == SCR_SOLDIER_INFO)                  // 11 строк: подпись, число (DYN 10..20), полоса (21..31)
		for (uint8_t i = 0; i < SI_ROWS; i++) {
			int16_t y = 80 + 11 * i;
			add_w(s, w, W_TEXT, 6, y, 120, 9, UI_EL_TEXT2, stat_str[i], 0);
			add_w(s, w, W_TEXT, 131, y, 18, 9, UI_EL_NUMBERS, DYN(10 + i), 0);
			add_w(s, w, W_BAR, 170, y + 1, 150, 7, stat_el[i], DYN(21 + i), 0);
		}
	return 1;
}

// ---------------------------------------------------------------- тексты

static void bar3(char *buf, uint16_t v, uint16_t v2, uint16_t mx)
{
	buf[0] = (char)v; buf[1] = (char)(v >> 8);
	buf[2] = (char)v2; buf[3] = (char)(v2 >> 8);
	buf[4] = (char)mx; buf[5] = (char)(mx >> 8);
}

static void pair(char *buf, uint16_t a, uint16_t b)
{
	fmt_num(buf, a, 0);
	strcat(buf, ":");
	fmt_num(buf + strlen(buf), b, 0);
}

static const uint16_t cstatus[5] = { STR_READY, STR_OUT, STR_REPAIRS, STR_REFUELLING, STR_REARMING };
static const uint16_t rank_str[6] = { STR_ROOKIE, STR_SQUADDIE, STR_SERGEANT, STR_CAPTAIN, STR_COLONEL, STR_COMMANDER };

// k-й корабль базы (не в пути)
static uint8_t craft_nth(uint8_t b, uint8_t k)
{
	for (uint8_t c = 0; c < MAX_CRAFTS; c++) {
		craft_t *cr = &ST->craft[c];
		if (cr->type == NONE8 || cr->base != b || cr->transit) continue;
		if (!k) return c;
		k--;
	}
	return NONE8;
}

// Строки склада: k-й предмет с количеством > 0
static uint8_t store_nth(uint8_t b, uint8_t k)
{
	for (uint8_t i = 0; i < MAX_ITEMS; i++)
		if (ST->base[b].items[i]) { if (!k) return i; k--; }
	return NONE8;
}

void base_text(uint8_t id, uint8_t slot, uint8_t row, char *buf) __banked
{
	rtab_t t;
	uint8_t b = ctx.base;
	switch (id) {
	case SCR_BASESCAPE:
		switch (slot) {
		case 1: hover_text(b, buf); break;
		case 2: strcpy(buf, ST->base[b].name); break;
		case 3: {
			uint8_t r = region_at((uint16_t)(ST->base[b].pos.lon >> 16), (int16_t)(ST->base[b].pos.lat >> 16));
			rtab_open(RES_RULE_REGIONS, &t);
			if (r != NONE8) str_copy(rtab_word(&t, r, 0), buf, 64);
			break;
		}
		case 4: fmt_funds(t1, ST->funds); str_fmt(buf, str_get(STR_FUNDS), t1, ""); break;
		}
		break;
	case SCR_BASE_INFO:
		if (slot == 0) { strcpy(buf, ST->base[b].name); break; }
		if (slot >= 9 && slot < 12) { fmt_num(buf, info_a[slot], 0); break; }
		if (slot < 12) { pair(buf, info_a[slot], info_b[slot]); break; }
		if (slot >= 21 && slot <= 31) {
			uint8_t k = slot - 20;
			uint16_t s8 = info_scale8[k];
			// Bar: длина = scale * max; значение — personnel: доступно из всего, остальное: занято из доступного
			uint16_t mx = k <= 3 ? info_b[k] : (k >= 9 ? info_a[k] : info_b[k]);
			uint16_t v = k <= 3 ? info_a[k] : info_a[k];
			bar3(buf, (uint16_t)((uint32_t)v * s8 / 8), 0, (uint16_t)((uint32_t)mx * s8 / 8));
		}
		break;
	case SCR_STORES:
		if (row == LIST_COLS) {
			static const uint8_t cw[] = { 162, 92, 32 };
			list_cols(buf, 3, cw, 0);
		} else {
			uint8_t it = store_nth(b, row);
			if (it == NONE8) break;
			rtab_open(RES_RULE_ITEMS, &t);
			str_copy(rtab_word(&t, it, 0), buf, 64);
			strcat(buf, "\t");
			fmt_num(buf + strlen(buf), ST->base[b].items[it], 0);
			strcat(buf, "\t");
			uint32_t sp = (uint32_t)ST->base[b].items[it] * (uint16_t)rtab_word(&t, it, offsetof(r_items_t, size));
			fmt_num(buf + strlen(buf), (int32_t)((sp + 50) / 100), 0);
		}
		break;
	case SCR_MONTHLY_COSTS: {
		static const uint8_t cw[] = { 125, 70, 44, 50 }, dots[] = { LC_DOT, LC_DOT, LC_DOT, 0 };   // setDot
		if (row == LIST_COLS) {
			if (slot == 2) { static const uint8_t c2[] = { 239, 60 }; list_cols(buf, 2, c2, dots); }
			else if (slot == 5) { static const uint8_t c5[] = { 44, 55 }; list_cols(buf, 2, c5, dots); }
			else list_cols(buf, 4, cw, dots);
			break;
		}
		costs_t c;
		base_costs(b, &c);
		if (slot == 0) {                          // аренда кораблей: по типам, что есть на базе
			rtab_open(RES_RULE_CRAFTS, &t);
			uint8_t k = row;
			for (uint8_t ty = 0; ty < t.n; ty++) {
				uint8_t n = 0;
				for (uint8_t cr = 0; cr < MAX_CRAFTS; cr++) if (ST->craft[cr].type == ty && ST->craft[cr].base == b) n++;
				uint32_t rent = rent_of(&t, ty);
				if (!rent) continue;
				if (k--) continue;
				str_copy(rtab_word(&t, ty, 0), buf, 64);
				strcat(buf, "\t"); fmt_funds(buf + strlen(buf), (int32_t)rent);
				strcat(buf, "\t"); fmt_num(buf + strlen(buf), n, 0);
				strcat(buf, "\t"); fmt_funds(buf + strlen(buf), (int32_t)rent * n);
				break;
			}
		} else if (slot == 1) {                   // зарплаты: солдаты, инженеры, учёные
			uint16_t sci, eng, n;
			uint32_t unit;
			base_personnel(b, &sci, &eng);
			if (row == 0) {
				rtab_open(RES_RULE_SOLDIERS, &t);
				far_read(t.base + 8 + offsetof(r_soldiers_t, cost_salary), &unit, 4);
				str_copy(STR_SOLDIERS, buf, 40);
				n = soldiers_count(b, 0xFE, 0);
			} else if (row == 1) { str_copy(STR_ENGINEERS, buf, 40); unit = gv.cost_engineer; n = eng; }
			else { str_copy(STR_SCIENTISTS, buf, 40); unit = gv.cost_scientist; n = sci; }
			strcat(buf, "\t"); fmt_funds(buf + strlen(buf), (int32_t)unit);
			strcat(buf, "\t"); fmt_num(buf + strlen(buf), n, 0);
			strcat(buf, "\t"); fmt_funds(buf + strlen(buf), (int32_t)unit * n);
		} else if (slot == 2) {
			str_copy(STR_BASE_MAINTENANCE, buf, 64);
			strcat(buf, "\t\x01"); fmt_funds(buf + strlen(buf), (int32_t)c.facilities);
		} else if (slot == 3) {
			int32_t inc = 0;
			for (uint8_t i = 0; i < MAX_COUNTRIES; i++) inc += (int32_t)ST->country[i].funding[ST->hist_len - 1] * 1000;
			str_copy(STR_INCOME, buf, 40); strcat(buf, "="); fmt_funds(buf + strlen(buf), inc);
		} else if (slot == 4) {
			str_copy(STR_MAINTENANCE, buf, 40); strcat(buf, "="); fmt_funds(buf + strlen(buf), total_maintenance());
		} else if (slot == 5) {
			str_copy(STR_TOTAL, buf, 40); strcat(buf, "\t"); fmt_funds(buf + strlen(buf), (int32_t)c.total);
		}
		break;
	}
	case SCR_BUILD_FACILITIES:
		if (row != LIST_COLS && row < build_n) {
			rtab_open(RES_RULE_FACILITIES, &t);
			str_copy(rtab_word(&t, build_list[row], 0), buf, 64);
		}
		break;
	case SCR_PLACE_FACILITY: {
		r_facilities_t f;
		rtab_open(RES_RULE_FACILITIES, &t);
		rtab_get(&t, ctx.facility, &f);
		if (slot == 0) str_copy(f.name, buf, 64);
		else if (slot == 1) fmt_funds(buf, (int32_t)f.build_cost);
		else if (slot == 2) str_plural(buf, STR_DAY_one, f.build_time);
		else fmt_funds(buf, (int32_t)f.monthly_cost);
		break;
	}
	case SCR_DISMANTLE:
		rtab_open(RES_RULE_FACILITIES, &t);
		str_copy(rtab_word(&t, ST->base[b].fac[ctx.facility].type, 0), buf, 64);
		break;
	case SCR_CRAFTS:
		if (slot == 1) { str_fmt(buf, str_get(STR_BASE_), ST->base[b].name, ""); break; }
		if (row == LIST_COLS) {
			static const uint8_t cw[] = { 94, 68, 44, 46, 28 };
			list_cols(buf, 5, cw, 0);
		} else {
			uint8_t c = craft_nth(b, row);
			if (c == NONE8) break;
			craft_t *cr = &ST->craft[c];
			craft_name(c, buf);
			strcat(buf, "\t");
			str_copy(cstatus[cr->status], buf + strlen(buf), 30);
			strcat(buf, "\t");
			fmt_num(buf + strlen(buf), (cr->weap[0].type != NONE8) + (cr->weap[1].type != NONE8), 0);
			rtab_open(RES_RULE_CRAFTS, &t);
			strcat(buf, "/");
			fmt_num(buf + strlen(buf), rtab_word(&t, cr->type, offsetof(r_crafts_t, weapons)) & 0xFF, 0);
			strcat(buf, "\t");
			fmt_num(buf + strlen(buf), soldiers_count(b, c, 0), 0);
			strcat(buf, "\t");
			uint8_t nv = 0;
			if (cr->cargo != NONE8)
				for (uint8_t k = 0; k < CARGO_VEH; k++) if (ST->cargo[cr->cargo].veh[k].type != NONE8) nv++;
			fmt_num(buf + strlen(buf), nv, 0);
		}
		break;
	case SCR_SOLDIERS:
		if (row == LIST_COLS) {
			static const uint8_t cw[] = { 114, 92, 74 };
			list_cols(buf, 3, cw, 0);
		} else {
			uint8_t s = soldier_nth(b, row);
			if (s == NONE8) break;
			soldier_t so;
			soldier_get(s, &so);
			strcpy(buf, so.name);
			strcat(buf, "\t");
			str_copy(rank_str[so.rank < 6 ? so.rank : 0], buf + strlen(buf), 30);
			strcat(buf, "\t");
			if (so.transit) str_copy(STR_TRANSFERS, buf + strlen(buf), 30);
			else if (so.craft != NONE8) craft_name(so.craft, buf + strlen(buf));
			else str_copy(STR_NONE_UC, buf + strlen(buf), 30);
		}
		break;
	case SCR_SOLDIER_INFO: {
		switch (slot) {
		case 1: str_copy(rank_str[sol.rank < 6 ? sol.rank : 0], t1, 40); str_fmt(buf, str_get(STR_RANK_), t1, ""); return;
		case 2: fmt_num(t1, sol.missions, 0); str_fmt(buf, str_get(STR_MISSIONS), t1, ""); return;
		case 3: fmt_num(t1, sol.kills, 0); str_fmt(buf, str_get(STR_KILLS), t1, ""); return;
		case 4:
			if (sol.craft != NONE8) craft_name(sol.craft, t1); else str_copy(STR_NONE_UC, t1, 40);
			str_fmt(buf, str_get(STR_CRAFT_), t1, "");
			return;
		case 5:
			if (sol.recovery) { str_plural(t1, STR_DAY_one, sol.recovery); str_fmt(buf, str_get(STR_WOUND_RECOVERY), t1, ""); }
			return;
		case 6: if (sol.flags & SF_PSI) str_copy(STR_IN_PSIONIC_TRAINING, buf, 64); return;
		case 40: strcpy(buf, "<<"); return;
		case 41: strcpy(buf, ">>"); return;
		}
		uint8_t *cur = (uint8_t *)&sol.cur, *ini = (uint8_t *)&sol.init;
		if (slot >= 10 && slot < 10 + SI_ROWS) fmt_num(buf, cur[slot - 10], 0);
		else if (slot >= 21 && slot < 21 + SI_ROWS) {
			uint8_t k = slot - 21;
			bar3(buf, cur[k], ini[k], cur[k]);    // текущее и при найме (второе поверх)
		}
		break;
	}
	case SCR_SACK_SOLDIER:
		strcpy(buf, sol.name);
		strcat(buf, "?");
		break;
	}
}

uint8_t base_rows(uint8_t id, uint8_t slot) __banked
{
	uint8_t n = 0, b = ctx.base;
	rtab_t t;
	switch (id) {
	case SCR_STORES:
		for (uint8_t i = 0; i < MAX_ITEMS; i++) if (ST->base[b].items[i]) n++;
		return n;
	case SCR_MONTHLY_COSTS:
		if (slot == 0) {
			rtab_open(RES_RULE_CRAFTS, &t);
			for (uint8_t ty = 0; ty < t.n; ty++) if (rent_of(&t, ty)) n++;
			return n;
		}
		return slot == 1 ? 3 : 1;
	case SCR_BUILD_FACILITIES: return build_n;
	case SCR_CRAFTS:
		for (uint8_t c = 0; c < MAX_CRAFTS; c++)
			if (ST->craft[c].type != NONE8 && ST->craft[c].base == b && !ST->craft[c].transit) n++;
		return n;
	case SCR_SOLDIERS: return soldiers_count(b, 0xFE, 0);
	}
	return 0;
}

// ---------------------------------------------------------------- события

static void sol_load(uint8_t i)
{
	sol_i = i;
	soldier_get(i, &sol);
	strcpy(ui_edit, sol.name);
}

static void sol_store(void)
{
	if (ui_edit[0]) { strncpy(sol.name, ui_edit, SOLDIER_NAME - 1); sol.name[SOLDIER_NAME - 1] = 0; }
	soldier_put(sol_i, &sol);
}

uint8_t base_event(uint8_t id, uint8_t ev, uint8_t arg) __banked
{
	rtab_t t;
	uint8_t b = ctx.base;
	switch (id) {
	case SCR_BASESCAPE:
		if (ev == EVT_OPEN) {
			if (!ST->base[b].name[0]) ctx.base = b = 0;
			hover_fi = hover_cell = NONE8;
		}
		if (ev == EVT_TICK) { hover_tick(b); break; }
		if (ev == EVT_DRAW) {
			if (arg == 0) { draw_base(b, 0, 8); hover_cell = NONE8; }   // база сменилась — пересчитать
			else draw_mini(192, 41);
			break;
		}
		if (ev != EVT_BUTTON) break;
		if (arg == 1) {                           // клик по постройке: снос
			uint8_t cx = (uint8_t)(ui_click_x / 32), cy = (uint8_t)((ui_click_y - 8) / 32);
			uint8_t fi = fac_at(b, cx, cy);
			if (fi == NONE8) break;
			uint8_t r = fac_can_dismantle(b, fi);
			if (r == FAC_IN_USE) msg(STR_FACILITY_IN_USE);
			else if (r == FAC_CUTS) msg(STR_CANNOT_DISMANTLE_FACILITY);
			else { ctx.facility = fi; UI_GO(A_PUSH, SCR_DISMANTLE); }
		} else if (arg == 2) mini_click(192);
		else if (arg == 10) {                     // новая база: выбор места на глобусе
			if (bases_count() >= MAX_BASES) break;
			ctx.new_base = 1;
			UI_GO(A_POP_PUSH, SCR_BUILD_NEW_BASE);
		} else if (arg >= 20 && arg < 28) {
			if (ST->base[arg - 20].name[0]) { ctx.base = arg - 20; ST->sel_base = ctx.base; UI_GO(A_REDRAW, 0); }
		} else if (arg >= 11 && arg <= 13)
			UI_GO(A_PUSH, arg == 11 ? SCR_RESEARCH : arg == 12 ? SCR_MANUFACTURE : SCR_TRANSFER_BASE);
		break;
	case SCR_BASE_INFO:
		if (ev == EVT_OPEN) info_calc();
		else if (ev == EVT_DRAW) draw_mini(182, 8);
		else if (ev == EVT_BUTTON && arg == 2) {
			uint8_t i = (uint8_t)((ui_click_x - 182) / 16);
			if (i < MAX_BASES && ST->base[i].name[0]) { ctx.base = i; info_calc(); UI_GO(A_REDRAW, 0); }
		} else if (ev == EVT_BUTTON && arg == 13)
			UI_GO(A_PUSH, SCR_TRANSFERS);
		break;
	case SCR_BUILD_FACILITIES:
		if (ev == EVT_OPEN) {                     // BuildFacilitiesState: исследовано, не лифт
			r_facilities_t f;
			rtab_open(RES_RULE_FACILITIES, &t);
			build_n = 0;
			for (uint8_t i = 0; i < t.n && build_n < sizeof build_list; i++) {
				rtab_get(&t, i, &f);
				if (f.flags & FACILITIES_F_LIFT) continue;
				if (!reqs_done(&t, i, offsetof(r_facilities_t, requires))) continue;
				build_list[build_n++] = i;
			}
		} else if (ev == EVT_LIST && arg < build_n) {
			ctx.facility = build_list[arg];
			UI_GO(A_PUSH, SCR_PLACE_FACILITY);
		}
		break;
	case SCR_PLACE_FACILITY:
		if (ev == EVT_OPEN) {
			rtab_open(RES_RULE_FACILITIES, &t);
			place_size = (uint8_t)rtab_word(&t, ctx.facility, offsetof(r_facilities_t, size));
		} else if (ev == EVT_TICK) sel_tick(place_size);
		else if (ev == EVT_DRAW) draw_base(b, 0, 8);
		else if (ev == EVT_BUTTON) {
			uint8_t cx = (uint8_t)(ui_click_x / 32), cy = (uint8_t)((ui_click_y - 8) / 32);
			r_facilities_t f;
			rtab_open(RES_RULE_FACILITIES, &t);
			rtab_get(&t, ctx.facility, &f);
			if (!fac_can_place(b, ctx.facility, cx, cy)) msg(STR_CANNOT_BUILD_HERE);
			else if (ST->funds < (int32_t)f.build_cost) { str_copy(STR_NOT_ENOUGH_MONEY, ui_msg, sizeof ui_msg); UI_GO(A_POP_PUSH, SCR_ERROR); }
			else { fac_build(b, ctx.facility, cx, cy); UI_GO(A_POP, 0); }
		}
		break;
	case SCR_DISMANTLE:
		if (ev == EVT_BUTTON && arg == 1) { fac_dismantle(b, ctx.facility); UI_GO(A_POP, 0); }
		break;
	case SCR_CRAFTS:
		if (ev == EVT_LIST) {
			uint8_t c = craft_nth(b, arg);
			if (c != NONE8) { ctx.craft = c; UI_GO(A_PUSH, SCR_CRAFT_INFO); }
		}
		break;
	case SCR_SOLDIERS:
		if (ev == EVT_LIST) {
			uint8_t s = soldier_nth(b, arg);
			if (s != NONE8) { sol_load(s); ctx.soldier = arg; UI_GO(A_PUSH, SCR_SOLDIER_INFO); }
		}
		break;
	case SCR_SOLDIER_INFO:
		if (ev == EVT_OPEN) { uint8_t s = soldier_nth(b, ctx.soldier); if (s != NONE8) sol_load(s); }
		else if (ev == EVT_BUTTON) {
			sol_store();                           // имя — при любом уходе с солдата
			if (arg == 1) { UI_GO(A_POP, 0); break; }
			uint8_t n = soldiers_count(b, 0xFE, 0);
			if (!n) break;
			ctx.soldier = arg == 2 ? (ctx.soldier ? ctx.soldier - 1 : n - 1) : (ctx.soldier + 1 < n ? ctx.soldier + 1 : 0);
			sol_load(soldier_nth(b, ctx.soldier));
			UI_GO(A_REDRAW, 0);
		}
		break;
	case SCR_SACK_SOLDIER:
		if (ev == EVT_BUTTON && arg == 1) {
			econ_sell(b, SELL_SOLDIER, sol_i, 1);
			if (ctx.soldier) ctx.soldier--;
			UI_GO(A_POP2, 0);
		}
		break;
	case SCR_PLACE_LIFT:
		if (ev == EVT_OPEN || ev == EVT_BUTTON) {    // лифт — первая постройка с lift
			r_facilities_t f;
			rtab_open(RES_RULE_FACILITIES, &t);
			for (uint8_t i = 0; i < t.n; i++) {
				rtab_get(&t, i, &f);
				if (!(f.flags & FACILITIES_F_LIFT)) continue;
				if (ev == EVT_OPEN) { place_size = f.size; break; }
				uint8_t cx = (uint8_t)(ui_click_x / 32), cy = (uint8_t)((ui_click_y - 8) / 32);
				if (cx + f.size > BASE_SIZE || cy + f.size > BASE_SIZE) break;
				ST->base[b].fac[0].type = i;
				ST->base[b].fac[0].xy = cx | (cy << 4);
				ST->base[b].fac[0].days = 0;
				UI_GO(A_POP_PUSH, SCR_BASESCAPE);
				break;
			}
		} else if (ev == EVT_TICK) sel_tick(place_size);
		else if (ev == EVT_DRAW) draw_base(b, 0, 8);
		break;
	case SCR_SAVE_NAME:
		if (ev == EVT_BUTTON && arg == 1) {
			uint8_t r = st_save(ctx.item, ui_edit);
			if (r != SAVE_OK) { strcpy(ui_msg, "SAVE FAILED"); UI_GO(A_POP_PUSH, SCR_ERROR); }
			else UI_GO(A_POP2, 0);                // имя + список: назад в паузу
		}
		break;
	}
	return 0;
}
