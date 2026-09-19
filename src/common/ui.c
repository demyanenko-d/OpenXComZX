// Ядро интерфейса (банк 1): стек экранов, рисование виджетов, ввод.
// Правила рисования повторяют Interface/* OpenXcom (08_ui_port_plan.md §6):
// Window — 5 колец c+3,c+2,c+1,c+2,c+3 и фон экрана, выровненный по началу экрана;
// TextButton — фаска c+1..c+5, нажатие — инверсия вокруг c+3 (геоскейп — c+2);
// TextList — строки через 8 (мелкий) / 16 (крупный) пикселей, стрелки 13x14.
// Описание экрана копируется из банка экранов в Win1 (S, W); цвета — ресурс UI.
#include <stdint.h>
#include <string.h>
#include "tsconf.h"
#include "memmap.h"
#include "far.h"
#include "pages.h"
#include "res.h"
#include "res_ids.h"
#include "ui_ids.h"
#include "gfx.h"
#include "text.h"
#include "music.h"
#include "input.h"
#include "ui.h"
#include "screens.h"
#include "dbg.h"

#define STACK_MAX 8
#define MAX_EL    48
#define EL_SIZE   14

uint8_t ui_request;
uint8_t ui_req_op, ui_req_arg;
int16_t ui_click_x, ui_click_y;
char ui_msg[128];
char ui_edit[UI_EDIT_MAX];
int8_t ui_arrow_dir;
uint8_t ui_arrow_max;

extern volatile uint16_t frames;

static uint8_t stk[STACK_MAX], stkf[STACK_MAX], depth;
static uint8_t scroll[STACK_MAX][SDEF_MAXW];
static uint8_t dirty;             // стек изменён без перерисовки
static sdef_t S;                  // загруженный экран
static wdef_t W[SDEF_MAXW];
static uint8_t cur, lvl;          // его номер и уровень в стеке

static far_t ui_base;             // ресурс UI
static uint8_t nel;
static uint8_t els[MAX_EL][EL_SIZE];
static uint16_t el_pal;
static uint8_t el_parent;

static char buf[1024];                 // текст виджета (статьи Уфопедии — до ~900 символов)
static uint8_t ed_pos, ed_blink;       // поле ввода: каретка, её мигание (TextEdit, 100 мс)
static uint16_t ed_t;
uint8_t ui_raw[16][2];                 // явные цвета: элемент #F0 + i -> color, color2 (статьи)
static uint8_t col, col2, colb;   // цвета текущего виджета

static void wait_frames(uint8_t n)
{
	uint16_t f = frames;
	while ((uint16_t)(frames - f) < n)
		__asm__("halt");
}

// ---------------------------------------------------------------- элементы interfaces.rul

static void load_elems(uint8_t ui)
{
	uint8_t h[5];
	nel = 0; el_pal = 0; el_parent = 0xFF;
	if (ui >= UI_SCR_COUNT || !ui_base) return;
	uint16_t off = far_word(ui_base + 2 + (uint16_t)ui * 2);
	if (!off) return;
	far_read(ui_base + off, h, 5);
	el_pal = h[0] | (h[1] << 8);
	el_parent = h[2];
	nel = h[4] > MAX_EL ? MAX_EL : h[4];
	far_read(ui_base + off + 5, els, (uint16_t)nel * EL_SIZE);
}

static const uint8_t *elem(uint8_t id)
{
	for (uint8_t i = 0; i < nel; i++)
		if (els[i][0] == id) return els[i];
	return 0;
}

// State::add: color, color2 (по умолчанию = color), border (по умолчанию = color)
static void wcolors(const wdef_t *w)
{
	if (w->el >= EL_RAW && w->el != 0xFF) {        // явный цвет (ui_raw)
		col = colb = ui_raw[w->el - EL_RAW][0];
		col2 = ui_raw[w->el - EL_RAW][1];
		return;
	}
	const uint8_t *e = w->el != 0xFF ? elem(w->el) : 0;
	col = col2 = colb = 0;
	if (!e) return;
	if (e[1] & 1) col = e[2];
	col2 = (e[1] & 2) ? e[3] : col;
	colb = (e[1] & 4) ? e[4] : col;
}

static uint8_t pick_color(const uint8_t *e, uint8_t which)
{
	if (which == 1 && (e[1] & 2)) return e[3];
	if (which == 2 && (e[1] & 4)) return e[4];
	return e[2];
}

uint8_t ui_color(uint8_t el, uint8_t which) __banked
{
	const uint8_t *e = elem(el);
	return e ? pick_color(e, which) : 0;
}

// Цвет элемента чужой категории (State::add(surface, id, category)): вид базы в окне
// установки постройки берёт цвета из basescape, а не из placeFacility.
uint8_t ui_color_in(uint8_t ui, uint8_t el, uint8_t which) __banked
{
	uint8_t h[5], e[EL_SIZE];
	if (ui >= UI_SCR_COUNT || !ui_base) return 0;
	uint16_t off = far_word(ui_base + 2 + (uint16_t)ui * 2);
	if (!off) return 0;
	far_read(ui_base + off, h, 5);
	for (uint8_t i = 0; i < h[4]; i++) {
		far_read(ui_base + off + 5 + (uint16_t)i * EL_SIZE, e, EL_SIZE);
		if (e[0] == el) return pick_color(e, which);
	}
	return 0;
}

// State::setInterface: палитра экрана (или родителя, иначе PAL_GEOSCAPE) и блок
// BACKPALS из элемента palette (color, при alterPal — color2).
static void apply_palette(void)
{
	if (S.flags & SF_RAWPAL) {                   // статьи Уфопедии: палитра задана явно (palui)
		gfx_palette(S.palui, -1);
		load_elems(S.ui);
		return;
	}
	uint8_t pu = S.palui != 0xFF ? S.palui : S.ui;
	load_elems(pu);
	uint16_t pal = el_pal;
	if (!pal && el_parent != 0xFF) {
		uint16_t off = far_word(ui_base + 2 + (uint16_t)el_parent * 2);
		if (off) pal = far_word(ui_base + off);
	}
	if (!pal) pal = RES_PAL_GEOSCAPE;
	int8_t bp = -1;
	const uint8_t *e = elem(UI_EL_PALETTE);
	if (e) {
		if ((S.flags & SF_ALTPAL) && (e[1] & 2)) bp = (int8_t)e[3];
		else if (e[1] & 1) bp = (int8_t)e[2];
	}
	gfx_palette(pal, bp);
	if (pu != S.ui) load_elems(S.ui);
}

// ---------------------------------------------------------------- рисование

static void get_text(uint16_t str, uint8_t row)
{
	buf[0] = 0;
	if (str == NOSTR) return;
	if (str & 0x8000) scr_text(cur, (uint8_t)str, row, buf);
	else str_copy(str, buf, sizeof buf);
}

static void draw_text(int16_t x, int16_t y, int16_t w, int16_t h, uint8_t font, uint8_t flags)
{
	tbox_t b;
	b.x = x; b.y = y; b.w = w; b.h = h;
	b.font = font; b.color = col; b.color2 = col2; b.flags = flags;
	text_draw(&b, buf);
}

static uint8_t inv_mid;                      // цвет середины нажатой кнопки (рисует gfx_bevel, банк 11)

static void draw_window(const wdef_t *w)
{
	inv_mid = 0;
	gfx_window(w->x, w->y, w->w, w->h, col, S.bg, w->flags & WF_THIN);
}

// TextButton::draw: фаска и текст; нажатая — инверсия вокруг c+3 (геоскейп — c+2).
static void draw_button(const wdef_t *w, uint8_t press)
{
	int16_t x = w->x, y = w->y;
	uint8_t c = col, geo = w->flags & WF_GEO, big = w->flags & WF_BIG;
	if (press > 1) { col = c = press; inv_mid = press + 4; }     // ToggleTextButton::setInvertColor (EVT_QUERY — цвет)
	else inv_mid = press ? (uint8_t)(c + (geo ? 2 : 3)) : 0;
	gfx_bevel(x, y, w->w, w->h, c, geo, inv_mid);
	if (w->type == W_COMBO) {                    // ComboBox::drawArrow: 11x8 в (w-14, 4)
		int16_t ax = x + w->w - 14 + 1, ay = y + 4 + 2;
		for (uint8_t k = 0; k < 5; k++) {
			gfx_fill(ax + k, ay + k, 9 - 2 * k, 1, c + 3);
			if (k < 4) gfx_fill(ax + 1 + k, ay + k, 7 - 2 * k, 1, c + 1);
		}
	}
	get_text(w->str, 0);
	col2 = c;
	draw_text(x, y, w->w, w->h, geo ? (big ? FNT_GEO_BIG : FNT_GEO_SMALL) : (big ? FNT_BIG : FNT_SMALL),
		TX_CENTER | TX_MIDDLE | TX_WRAP | (press ? TX_INVERT : 0));
}

// ArrowButton 13x14 (ARROW_BIG_UP / ARROW_BIG_DOWN)

// ArrowButton 11x8 — стрелки у строк списка: shape 0 вверх, 1 вниз, 2 влево, 3 вправо
// (ARROW_SMALL_*; треугольники влево/вправо — прямоугольники ArrowButton::draw).


static uint8_t list_font(const wdef_t *w) { return (w->flags & WF_BIG) ? FNT_BIG : FNT_SMALL; }

static void list_geom(uint8_t i, uint8_t *step, uint8_t *vis, uint8_t *n)
{
	const wdef_t *w = &W[i];
	*step = font_height(list_font(w));
	*vis = (uint8_t)((w->h + *step - 1) / *step);
	*n = scr_rows(cur, (uint8_t)w->str);
}

// Колонки списка (LIST_COLS) для draw_list / draw_row
static uint8_t lc_n, lc_ca[12], lc_arrows, lc_ah;
static int16_t lc_w[12];

static void list_cols_load(const wdef_t *w)
{
	scr_text(cur, (uint8_t)w->str, LIST_COLS, buf);
	lc_n = (uint8_t)buf[0];
	lc_arrows = lc_ah = 0;
	if (lc_n > 12) lc_n = 12;
	if (!lc_n) { lc_n = 1; lc_w[0] = w->w; lc_ca[0] = w->flags & (TX_CENTER | TX_RIGHT); return; }
	for (uint8_t c = 0; c < lc_n; c++) { lc_w[c] = (uint8_t)buf[1 + c]; lc_ca[c] = (uint8_t)buf[1 + lc_n + c]; }
	lc_arrows = (uint8_t)buf[1 + 2 * lc_n];
	lc_ah = buf[2 + 2 * lc_n] ? 2 : 0;             // ARROW_HORIZONTAL: влево/вправо
}

// Строка под курсором (TextList::mouseOver, _selector): фон строки темнее —
// offsetBlock(-10) по блокам 16 цветов, в ComboBox — offset(+1, от 224)
static uint8_t hov_w = 0xFF, hov_row;
static int16_t hov_x = -1, hov_y;

// Строка row списка i на экранной позиции r (колонки — list_cols_load)
static void draw_row(uint8_t i, uint8_t row, uint8_t r, uint8_t step)
{
	const wdef_t *w = &W[i];
	uint8_t hw = hov_w, hr = hov_row;             // SDCC 4.5: сравнения — копиями
	if (i == hw && row == hr) gfx_selector(w->x, w->y + r * step, w->w, step, cur == SCR_COMBO);
	scr_text(cur, (uint8_t)w->str, row, buf);
	int16_t x = w->x;
	char *p = buf;
	uint8_t rc = col;                            // #03 c — цвет строки (setRowColor)
	if ((uint8_t)*p == 0x03) { rc = (uint8_t)p[1]; p += 2; }
	for (uint8_t c = 0; c < lc_n; c++) {
		char *q = p;
		while (*q && *q != '\t') q++;
		char end = *q;
		*q = 0;
		tbox_t b;
		b.x = x; b.y = w->y + r * step; b.w = lc_w[c]; b.h = step + 1;
		b.font = list_font(w); b.color = rc; b.color2 = col2; b.flags = lc_ca[c] & 0x7F;
		text_draw(&b, p);
		if ((lc_ca[c] & LC_DOT) && end) {        // TextList::setDot: точки до ширины колонки
			int16_t tw = text_width(b.font, p), dw = text_width(b.font, ".");
			char d[48];
			uint8_t k = 0;
			b.x += tw;
			while (tw < lc_w[c] && k < sizeof d - 1) { d[k++] = '.'; tw += dw; }
			d[k] = 0;
			b.flags = 0;
			text_draw(&b, d);
		}
		x += lc_w[c];
		if (!end) break;
		p = q + 1;
	}
	if (lc_arrows) {
		gfx_arrow_small(w->x + lc_arrows, w->y + r * step, colb, lc_ah);
		gfx_arrow_small(w->x + lc_arrows + 12, w->y + r * step, colb, lc_ah + 1);
	}
}

static void draw_list(uint8_t i)
{
	const wdef_t *w = &W[i];
	uint8_t step, vis, n;
	list_geom(i, &step, &vis, &n);
	uint8_t first = scroll[lvl][i];
	list_cols_load(w);
	for (uint8_t r = 0; r < vis && first + r < n; r++) draw_row(i, first + r, r, step);
	if (n > vis) {
		gfx_arrow(w->x + w->w + 4, w->y, colb, 0);
		gfx_arrow(w->x + w->w + 4, w->y + w->h - 14, colb, 1);
	}
}

// W_IMAGE: act/arg — начало картинки на экране /2 (0 — картинка во весь экран).
static void draw_image(const wdef_t *w, int16_t x, int16_t y, int16_t ww, int16_t hh)
{
	int16_t ox = (int16_t)w->act * 2, oy = (int16_t)w->arg * 2;
	gfx_key = w->flags & 1;                      // 1 — цвет 0 прозрачный (картинка поверх фона)
	gfx_blit(w->str, x - ox, y - oy, x, y, ww, hh);
	gfx_key = 0;
}

static void restore(uint8_t i, int16_t x, int16_t y, int16_t w, int16_t h);

static void draw_widget(uint8_t i)
{
	const wdef_t *w = &W[i];
	wcolors(w);
	switch (w->type) {
	case W_WINDOW: draw_window(w); break;
	case W_FRAME:
		gfx_fill(w->x + 5, w->y + 5, w->w - 10, w->h - 10, col2);
		gfx_rings(w->x, w->y, w->w, w->h, col);
		break;
	case W_FILL: gfx_fill(w->x, w->y, w->w, w->h, w->el == 0xFF ? w->arg : col); break;
	case W_IMAGE: draw_image(w, w->x, w->y, w->w, w->h); break;
	case W_TEXT:
		get_text(w->str, 0);
		draw_text(w->x, w->y, w->w, w->h, list_font(w), w->flags & 0x1F);
		break;
	case W_BUTTON: case W_COMBO: draw_button(w, 0); break;
	case W_TOGGLE: draw_button(w, scr_event(cur, EVT_QUERY, w->arg)); break;
	case W_LIST: draw_list(i); break;
	case W_ARROW: gfx_arrow(w->x, w->y, col, w->flags & 1); break;
	case W_CUSTOM: scr_event(cur, EVT_DRAW, i); break;
	case W_EDIT: {                               // TextEdit: текст и мигающая каретка «|»
		uint8_t f = list_font(w), al = w->flags & 0x1F;
		strcpy(buf, ui_edit);
		draw_text(w->x, w->y, w->w, w->h, f, al);
		if (!ed_blink) break;
		int16_t x = w->x, tw = text_width(f, ui_edit);
		if (al & TX_CENTER) x += (w->w - tw) / 2;
		else if (al & TX_RIGHT) x += w->w - tw;
		buf[ed_pos] = 0;
		x += text_width(f, buf);
		strcpy(buf, "|");
		draw_text(x, w->y, 16, w->h, f, al & TX_MIDDLE);
		break;
	}
	case W_BAR: {
		// Bar::draw: рамка (border или c+4) длиной max+1, внутри прозрачно, затем value, value2
		scr_text(cur, (uint8_t)w->str, 0, buf);
		uint16_t v = (uint8_t)buf[0] | ((uint8_t)buf[1] << 8), v2 = (uint8_t)buf[2] | ((uint8_t)buf[3] << 8);
		uint16_t mx = (uint8_t)buf[4] | ((uint8_t)buf[5] << 8);
		uint8_t fc = colb != col ? colb : col + 4, c1 = col, c2 = col2;
		if (mx > w->w - 1) mx = w->w - 1;
		if (v > mx) v = mx;
		if (v2 > mx) v2 = mx;
		gfx_fill(w->x, w->y, mx + 1, w->h, fc);
		restore(i, w->x, w->y + 1, mx, w->h - 2);
		if (v) gfx_fill(w->x, w->y + 1, v, w->h - 2, c1);
		if (v2) gfx_fill(w->x, w->y + 1, v2, w->h - 2, c2);
		break;
	}
	}
}

// Восстановить фон под виджетом i: картинка, окно или заливка ниже по списку.
static void restore(uint8_t i, int16_t x, int16_t y, int16_t w, int16_t h)
{
	for (uint8_t j = i; j-- > 0;) {
		const wdef_t *b = &W[j];
		if (x < b->x || y < b->y || x + w > b->x + b->w || y + h > b->y + b->h) continue;
		if (b->type == W_IMAGE) {
			if (b->flags & 1) restore(j, x, y, w, h);   // под прозрачной — сначала нижние
			draw_image(b, x, y, w, h);
			return;
		}
		if (b->type == W_WINDOW) {
			wcolors(b);
			// фон окна — только внутри рамки (draw_window: отступ 5, у тонкой 3): текст бывает
			// шире внутренней области, и фон затирал кольца рамки, а их никто не перерисовывал
			int16_t m = (b->flags & WF_THIN) ? 3 : 5, e = (b->flags & WF_THIN) ? 2 : 5;
			int16_t x0 = b->x + m, y0 = b->y + m, x1 = b->x + b->w - e, y1 = b->y + b->h - e;
			int16_t rx = x < x0 ? x0 : x, ry = y < y0 ? y0 : y;
			int16_t rw = (x + w > x1 ? x1 : x + w) - rx, rh = (y + h > y1 ? y1 : y + h) - ry;
			if (rw <= 0 || rh <= 0) return;
			if (S.bg) gfx_bg(S.bg, rx, ry, rw, rh); else gfx_fill(rx, ry, rw, rh, col + 3);
			return;
		}
		if (b->type == W_FILL) { wcolors(b); gfx_fill(x, y, w, h, b->el == 0xFF ? b->arg : col); return; }
	}
	gfx_fill(x, y, w, h, 0);
}

// Перерисовка набора виджетов в два прохода: mark_one восстанавливает фон под
// текстом/списком и отмечает виджет, flush_marked рисует отмеченные по порядку —
// каждый не больше одного раза. Рамки текстов в раскладках OpenXcom перекрываются
// (строка y 24 h 9 и заголовок на 32, поля часов геоскейпа): тексты, задетые
// восстановленным фоном, тоже отмечаются.
static uint8_t marked[SDEF_MAXW];

// Подпись нарисованного содержимого DYN-виджета: пока она та же, виджет не трогаем — ни фон под ним,
// ни вывод. Это и есть «виджет знает, надо ли перерисовываться»: раньше мигал фоном даже когда текст
// не менялся (перерисовка стека, закрытие окна, такт геоскейпа). 0 — содержимое неизвестно.
static uint16_t sig[SDEF_MAXW];
static int16_t stg_x, stg_y, stg_w, stg_h;   // прямоугольник, собираемый в рабочей области

static uint16_t sig_bytes(const char *p, uint8_t n)
{
	uint16_t h = 0x1505;
	while (n--) h = (uint16_t)(h * 33u + (uint8_t)*p++);
	return h ? h : 1;
}

static uint16_t sig_str(const char *p)
{
	uint16_t h = 0x1505;
	while (*p) h = (uint16_t)(h * 33u + (uint8_t)*p++);
	return h ? h : 1;
}

// 1 — на экране уже то же самое (подпись совпала); иначе подпись запоминается
static uint8_t content_same(uint8_t i)
{
	const wdef_t *w = &W[i];
	uint8_t dyn = w->str != NOSTR && (w->str & 0x8000);
	uint16_t h;
	switch (w->type) {
	case W_TEXT: case W_BUTTON: case W_COMBO:
		if (!dyn) return 0;
		get_text(w->str, 0);
		h = sig_str(buf);
		break;
	case W_BAR:
		if (!dyn) return 0;
		scr_text(cur, (uint8_t)w->str, 0, buf);
		h = sig_bytes(buf, 6);
		break;
	case W_TOGGLE:
		h = (uint16_t)(0x100 + scr_event(cur, EVT_QUERY, w->arg));
		break;
	default:
		return 0;                            // списки, W_CUSTOM, картинки — как раньше
	}
	if (sig[i] == h) return 1;
	sig[i] = h;
	return 0;
}

static void mark_one(uint8_t i)
{
	const wdef_t *w = &W[i];
	if (content_same(i)) return;                 // то же содержимое — экран не трогаем
	if (w->type == W_TEXT || w->type == W_LIST) {
		int16_t x = w->x, y = w->y, x2 = x + w->w, y2 = y + w->h + 1;
		marked[i] = 2;                       // 2 — под ним нужен фон (его вернёт flush_marked)
		const wdef_t *t = W;
		for (uint8_t j = 0; j < S.n; j++, t++) {
			if (t->type != W_TEXT) continue;
			if (t->x >= x2 || t->x + t->w <= x || t->y >= y2 || t->y + t->h <= y) continue;
			if (!marked[j]) marked[j] = 1;
		}
	} else if (w->type == W_CUSTOM || w->type == W_BUTTON || w->type == W_TOGGLE || w->type == W_BAR || w->type == W_COMBO)
		marked[i] = 1;
}

// Группа отмеченных виджетов собирается в рабочей области экранной памяти (строки STAGE_Y…,
// map.h: видимое окно — 0..199, задний буфер глобуса — 280..479) и уходит на экран одним DMA:
// фон и содержимое сменяются за одну пересылку, поэтому пустой прямоугольник не мелькает.
// Не помещается в рабочую область (высокая группа, список во весь экран) — рисуем прямо на экран,
// как раньше. W_CUSTOM рисует экран сам (globe.s пишет в свои строки) — он идёт отдельно.
// Рабочая область в невидимых строках экранной памяти (map.h): 200..275 свободны, пока не открыт
// экран воздушного боя — он собирает там поле; в бою берём 480..511 (они не заняты никогда).
#define STAGE_BIG_Y   200
#define STAGE_BIG_H   56
#define STAGE_SAFE_Y  480
#define STAGE_SAFE_H  32
static int16_t stage_y, stage_h;

static void stage_pick(void)
{
	stage_y = STAGE_BIG_Y;
	stage_h = STAGE_BIG_H;
	for (uint8_t i = 0; i < depth; i++)
		if (stk[i] == SCR_DOGFIGHT) { stage_y = STAGE_SAFE_Y; stage_h = STAGE_SAFE_H; return; }
}

// 1 — дальше рисуем в рабочую область (в неё уже перенесено то, что сейчас на экране: края
// прямоугольника после выравнивания по два пикселя и места, которым фон не восстанавливают).
// 0 — не помещается, рисуем прямо на экран, как раньше.
static uint8_t stage_begin(int16_t x, int16_t y, int16_t w, int16_t h)
{
	stage_pick();
	if (x < 0 || y < 0 || x + w > SCREEN_W || y + h > SCREEN_H || h > stage_h) return 0;
	stg_x = x & ~1;
	stg_w = (x + w - stg_x + 1) & ~1;
	if (stg_x + stg_w > SCREEN_W) stg_w = SCREEN_W - stg_x;
	stg_y = y;
	stg_h = h;
	gfx_copy(stg_x, stg_y, stage_y, stg_w, stg_h);
	gfx_yb = tx_yb = (uint16_t)(stage_y - y);
	return 1;
}

static void stage_end(void)
{
	gfx_yb = tx_yb = 0;
	gfx_copy(stg_x, stage_y, stg_y, stg_w, stg_h);
}

// Отмеченные виджеты идут группами: в группу берутся те, что целиком помещаются в рабочую область
// от самого верхнего из оставшихся. Группа собирается в буфере и уходит на экран одним DMA, поэтому
// ряд кнопок (графики) появляется сразу, а не сверху вниз. Виджет выше области (список, окно)
// рисуется прямо на экран, как раньше.
static uint8_t flush_group(void)
{
	uint8_t top = 0xFF;
	for (uint8_t i = 0; i < S.n; i++) {
		const wdef_t *w = &W[i];
		if (!marked[i] || w->type == W_CUSTOM) continue;
		if (top == 0xFF || w->y < W[top].y) top = i;
	}
	if (top == 0xFF) return 0;
	stage_pick();
	int16_t by = W[top].y, be = 32000;   // пока одной группой: сборка по группам вскрывает дефект
	(void)stage_h;                       // отрисовки со смещением (см. 15_widgets.md)
	int16_t x0 = SCREEN_W, y0 = SCREEN_H, x1 = 0, y1 = 0;
	uint8_t n = 0;
	for (uint8_t i = 0; i < S.n; i++) {
		const wdef_t *w = &W[i];
		if (!marked[i] || w->type == W_CUSTOM || w->y < by || w->y + w->h + 1 > be) continue;
		if (w->x < x0) x0 = w->x;
		if (w->y < y0) y0 = w->y;
		if (w->x + w->w > x1) x1 = w->x + w->w;
		if (w->y + w->h + 1 > y1) y1 = w->y + w->h + 1;
		n++;
	}
	if (!n) {                                    // сам не помещается — прямо на экран
		if (marked[top] == 2) restore(top, W[top].x, W[top].y, W[top].w, W[top].h + 1);
		draw_widget(top);
		marked[top] = 0;
		return 1;
	}
	uint8_t stage = stage_begin(x0, y0, x1 - x0, y1 - y0);
	for (uint8_t i = 0; i < S.n; i++) {          // фон под теми, у кого он меняется
		const wdef_t *w = &W[i];
		if (marked[i] == 2 && w->type != W_CUSTOM && w->y >= by && w->y + w->h + 1 <= be)
			restore(i, w->x, w->y, w->w, w->h + 1);
	}
	for (uint8_t i = 0; i < S.n; i++) {
		const wdef_t *w = &W[i];
		if (!marked[i] || w->type == W_CUSTOM || w->y < by || w->y + w->h + 1 > be) continue;
		draw_widget(i);
		marked[i] = 0;
	}
	if (stage) stage_end();
	return 1;
}

static void flush_marked(void)
{
	while (flush_group()) ;
	for (uint8_t i = 0; i < S.n; i++)             // глобус и прочие «свои» виджеты — прямо на экран
		if (marked[i] && W[i].type == W_CUSTOM) draw_widget(i);
	memset(marked, 0, sizeof marked);
}

// Перерисовать динамические виджеты (str = DYN): тексты, списки, W_CUSTOM.
static void redraw_dyn(void)
{
	for (uint8_t i = 0; i < S.n; i++) {
		const wdef_t *w = &W[i];
		// переключатели — тоже: скорость времени меняется не только щелчком (центр на цель, начало боя)
		if (w->type == W_TOGGLE || (w->str != NOSTR && (w->str & 0x8000))) mark_one(i);
	}
	flush_marked();
}

// Частичная перерисовка (ui_dirty / ui_dirty_row): после события или тика
// перерисовываются только отмеченные DYN-слоты и отдельные строки списков.
#define DROWS 8
static uint8_t dslot[8];                     // бит на слот 0..63
static uint8_t drow_slot[DROWS], drow_row[DROWS], ndrow;

void ui_dirty(uint8_t slot) __banked
{
	if (slot < 64) dslot[slot >> 3] |= (uint8_t)(1 << (slot & 7));
}

void ui_dirty_row(uint8_t slot, uint8_t row) __banked
{
	uint8_t n = ndrow;                           // сравнения — с локальными копиями (SDCC 4.5)
	for (uint8_t k = 0; k < n; k++) {
		uint8_t s = drow_slot[k], r = drow_row[k];
		if (s == slot && r == row) return;
	}
	if (n < DROWS) { drow_slot[n] = slot; drow_row[n] = row; ndrow = n + 1; }
	else ui_dirty(slot);                     // много строк — весь список
}

static void dirty_clear(void)
{
	memset(dslot, 0, sizeof dslot);
	ndrow = 0;
}

// Одна строка списка i заново: фон полосы строки (соседние не трогать) и текст
static void row_redraw(uint8_t i, uint8_t row)
{
	const wdef_t *w = &W[i];
	uint8_t step, vis, n, first = scroll[lvl][i];
	list_geom(i, &step, &vis, &n);
	if (row < first || row >= first + vis || row >= n) return;
	uint8_t r = row - first;
	wcolors(w);
	list_cols_load(w);          // цвета элемента списка — до restore: он ставит свои (окно, заливка)
	uint8_t st = stage_begin(w->x, w->y + r * step, w->w, step);
	restore(i, w->x, w->y + r * step, w->w, step);
	wcolors(w);                 // restore мог сменить цвета на цвета фонового виджета
	draw_row(i, row, r, step);
	if (st) stage_end();
}

static void redraw_dirty(void)
{
	uint8_t any = ndrow;
	for (uint8_t k = 0; k < 8; k++) any |= dslot[k];
	if (!any) return;
	for (uint8_t i = 0; i < S.n; i++) {
		const wdef_t *w = &W[i];
		if (w->str == NOSTR || !(w->str & 0x8000)) continue;
		uint8_t s = (uint8_t)w->str;
		if (s < 64 && (dslot[s >> 3] & (uint8_t)(1 << (s & 7)))) { mark_one(i); continue; }
		if (w->type != W_LIST) continue;
		for (uint8_t k = 0; k < ndrow; k++)
			if (drow_slot[k] == s) row_redraw(i, drow_row[k]);
	}
	flush_marked();
	dirty_clear();
}

// ---------------------------------------------------------------- стек экранов

static uint8_t load(uint8_t l)
{
	lvl = l;
	cur = stk[l];
	hov_w = 0xFF;                                // строка под курсором — заново по курсору
	hov_x = -1;
	if (!scr_get(cur, &S, W)) { S.n = 0; return 0; }
	if (S.n > SDEF_MAXW) S.n = SDEF_MAXW;
	load_elems(S.ui);
	return 1;
}

static void draw_all(void)
{
	memset(sig, 0, sizeof sig);                  // экран другой — подписи прошлого не годятся
	for (uint8_t i = 0; i < S.n; i++) draw_widget(i);
}

static uint8_t bg_broken;                     // закрыто окно без перерисовки — на экране его остатки

// Весь видимый стек: от последнего полноэкранного экрана до верхнего.
static void redraw(void)
{
	uint8_t top = depth - 1, b = top;
	while (b > 0 && (stkf[b] & SF_POPUP)) b--;
	dirty_clear();                           // всё и так перерисуется
	bg_broken = 0;
	load(top);
	apply_palette();
	if (!(stkf[b] & SF_POPUP)) gfx_fill(0, 0, SCREEN_W, SCREEN_H, 0);   // полноэкранный: под ним ничего
	for (uint8_t l = b; l <= top; l++) {
		load(l);
		draw_all();
	}
	dirty = 0;
}

// Набор виджетов верхнего экрана изменился (окна воздушного боя разворачиваются и сворачиваются):
// перерисовать DYN-виджеты экрана под ним (у геоскейпа это глобус с метками и значками — фон под
// окнами) и заново разложить верхний экран. Дешевле полной перерисовки стека (не мигает панель).
// Данные под открытыми окнами изменились (построен или снесён модуль базы): сохранённый фон
// устарел, и при закрытии окна стек перерисовывается заново, а не восстанавливается снимком
void ui_bg_stale(void) __banked
{
	bg_broken = 1;
}

// Каретку поля ввода — в конец строки: экран сменил содержимое поля, не открывая его заново
// (перемотка бойцов кнопками в статистике)
void ui_edit_end(void) __banked
{
	ed_pos = (uint8_t)strlen(ui_edit);
	ed_blink = 1;
}

void ui_relayout(void) __banked
{
	if (depth < 2) return;
	if (load(depth - 2)) redraw_dyn();
	if (load(depth - 1)) draw_all();
}

// Снять подсветку строки под курсором (TextList::mouseOut). Иначе она остаётся на экране навсегда:
// новый экран забывает, где была подсветка, а всплывающее окно ещё и запоминает её в сохранённом фоне
static void hover_clear(void)
{
	uint8_t ow = hov_w, orow = hov_row;
	hov_w = 0xFF;
	hov_x = -1;
	if (ow != 0xFF) row_redraw(ow, orow);
}

static void do_push(uint8_t id)
{
	if (depth >= STACK_MAX) return;
	hover_clear();
	stk[depth] = id;
	memset(scroll[depth], 0, SDEF_MAXW);
	depth++;
	scr_event(id, EVT_OPEN, 0);
	ed_pos = (uint8_t)strlen(ui_edit);           // каретка — в конце (TextEdit::setText)
	ed_blink = 1;
	if (!load(depth - 1)) {
		depth--;
		if (depth) load(depth - 1);
		return;
	}
	stkf[depth - 1] = S.flags;
	const wdef_t *w = W;                         // окно: фон под всплывающим — запомнить, рамка растёт (Window::popup)
	uint8_t pop = (S.flags & SF_POPUP) && depth > 1 && !dirty;
	if (depth > 1 && w->type == W_WINDOW) {
		if (pop) gfx_bgsave(depth - 1, w->x, w->y, w->w, w->h);
		if (w->flags & (WF_POPH | WF_POPV)) {
			apply_palette();
			wcolors(w);
			gfx_popup(w->x, w->y, w->w, w->h, col, S.bg, w->flags);
		}
	}
	if (pop) {
		apply_palette();
		draw_all();
	} else
		redraw();
}

static void do_pop(uint8_t draw)
{
	uint8_t lvl = depth - 1;
	if (depth) {
		scr_event(stk[depth - 1], EVT_CLOSE, 0);
		depth--;
	}
	if (!depth) { dirty = 1; do_push(SCR_MAIN_MENU); return; }
	dirty = 1;
	if (gfx_bgrestore(lvl, draw && !bg_broken)) { // фон под окном — назад; у открывшегося — только DYN
		load(depth - 1);
		apply_palette();
		dirty_clear();
		redraw_dyn();
		dirty = 0;
		return;
	}
	if (!draw) bg_broken = 1;
	if (draw) redraw();
	else load(depth - 1);
}

static void do_set(uint8_t id)
{
	while (depth) {
		scr_event(stk[depth - 1], EVT_CLOSE, 0);
		depth--;
		gfx_bgrestore(depth, 0);
	}
	dirty = 1;
	do_push(id);
}

static uint8_t process(void)
{
	uint8_t any = 0;
	while (ui_req_op) {
		uint8_t op = ui_req_op, a = ui_req_arg;
		ui_req_op = 0;
		any = 1;
		switch (op) {
		case A_POP: do_pop(1); break;
		case A_POP2: do_pop(0); do_pop(1); break;
		case A_POPN: while (a-- > 1) do_pop(0); do_pop(1); break;
		case A_PUSH: do_push(a); break;
		case A_SET: do_set(a); break;
		case A_POP_PUSH: do_pop(0); do_push(a); break;
		case A_REDRAW: redraw(); break;
		case A_DYN: redraw_dyn(); break;
		}
	}
	return any;
}

// ---------------------------------------------------------------- ввод

static uint8_t hit(const wdef_t *w, int16_t x, int16_t y)
{
	return x >= w->x && y >= w->y && x < w->x + w->w && y < w->y + w->h;
}

// Курсор над выбираемым списком верхнего экрана: подсветка строки (TextList::mouseOver/Out)
static void hover(void)
{
	int16_t cx = cursor_x, cy = cursor_y, px = hov_x, py = hov_y;
	if (cx == px && cy == py) return;
	hov_x = cx; hov_y = cy;
	uint8_t nw = 0xFF, nr = 0;
	for (uint8_t i = S.n; i-- > 0;) {
		const wdef_t *w = &W[i];
		if (w->type != W_LIST || !hit(w, cx, cy)) continue;
		if (w->flags & WF_SEL) {
			uint8_t step, vis, n;
			list_geom(i, &step, &vis, &n);
			uint8_t r = scroll[lvl][i] + (uint8_t)((cy - w->y) / step);
			if (r < n) { nw = i; nr = r; }
		}
		break;
	}
	uint8_t ow = hov_w, orow = hov_row;
	if (nw == ow && nr == orow) return;
	hov_w = nw; hov_row = nr;
	if (ow != 0xFF) row_redraw(ow, orow);
	if (nw != 0xFF) row_redraw(nw, nr);
}

static void activate(uint8_t i)
{
	const wdef_t *w = &W[i];
	switch (w->type) {
	case W_BUTTON:
		wcolors(w);
		draw_button(w, 1);
		wait_frames(3);
		draw_button(w, 0);
		break;
	case W_TOGGLE:
		scr_event(cur, EVT_BUTTON, w->arg);
		for (uint8_t j = 0; j < S.n; j++) {
			const wdef_t *t = &W[j];
			if (t->type == W_TOGGLE && (j == i || (w->act && t->act == w->act))) {
				wcolors(t);
				draw_button(t, scr_event(cur, EVT_QUERY, t->arg));
			}
		}
		return;
	}
	if (w->act == A_CUSTOM) scr_event(cur, EVT_BUTTON, w->arg);
	else if (w->act != A_NONE) UI_GO(w->act, w->arg);
}

// Автоповтор стрелок (Timer 250 мс, затем 50 мс в OpenXcom): пока левая кнопка
// держится, последний клик по стрелке повторяется.
static uint8_t rep_on, rep_scr;
static uint16_t rep_t;
static event_t rep_ev;

static uint8_t list_click(uint8_t i, int16_t x, int16_t y, uint8_t rclick)
{
	const wdef_t *w = &W[i];
	uint8_t step, vis, n;
	list_geom(i, &step, &vis, &n);
	// стрелки ± у строк (TextList::setArrowColumn): левая +1, правая -1; правая кнопка — до предела
	scr_text(cur, (uint8_t)w->str, LIST_COLS, buf);
	uint8_t nc = (uint8_t)buf[0], arrows = nc && nc <= 12 ? (uint8_t)buf[1 + 2 * nc] : 0;
	if (arrows && hit(w, x, y) && x >= w->x + arrows && x < w->x + arrows + 23) {
		uint8_t r = scroll[lvl][i] + (uint8_t)((y - w->y) / step);
		if (r < n) {
			ui_arrow_dir = x < w->x + arrows + 12 ? 1 : -1;
			ui_arrow_max = rclick;
			rep_on = !rclick;
			if (scr_event(cur, EVT_ARROW, r)) redraw_dyn();
		}
		return 1;
	}
	if (rclick && !(w->flags & WF_RSEL)) return 0;
	int16_t ax = w->x + w->w + 4;
	if (!rclick && n > vis && x >= ax && x < ax + 13) {
		uint8_t *s = &scroll[lvl][i];
		if (y >= w->y && y < w->y + 14) {
			if (!*s) return 1;
			(*s)--;
		} else if (y >= w->y + w->h - 14 && y < w->y + w->h) {
			if (*s + vis >= n) return 1;
			(*s)++;
		} else
			return 0;
		restore(i, w->x, w->y, w->w, w->h + 1);
		wcolors(w);
		hov_w = 0xFF;                            // подсветка — заново по курсору
		hov_x = -1;
		draw_list(i);
		return 1;
	}
	// невыделяемый список щелчок не держит (TextList): кнопки под ним (статьи 16/17) работают
	if (!hit(w, x, y) || !(w->flags & WF_SEL)) return 0;
	uint8_t r = scroll[lvl][i] + (uint8_t)((y - w->y) / step);
	ui_arrow_max = rclick;                       // правая кнопка (WF_RSEL)
	if (r < n) scr_event(cur, EVT_LIST, r);
	return 1;
}

// Клавиша в поле ввода: 1 — поглощена.
static uint8_t edit_idx(void)
{
	for (uint8_t i = 0; i < S.n; i++) if (W[i].type == W_EDIT) return i;
	return 0xFF;
}

// Поле ввода перерисовывается по 10 раз в секунду (мигает каретка), поэтому — через рабочую
// область: раньше на экране мелькал стёртый фон, и «мигал» весь текст, а не одна каретка
static void edit_redraw(uint8_t i)
{
	const wdef_t *w = &W[i];
	uint8_t st = stage_begin(w->x, w->y, w->w, w->h + 1);
	restore(i, w->x, w->y, w->w, w->h + 1);
	wcolors(w);                 // restore ставит цвета фонового виджета
	draw_widget(i);
	if (st) stage_end();
}

// Клавиша в поле ввода (TextEdit::keyboardPress): стрелки — каретка (вверх/вниз —
// Home/End), DELETE — символ перед кареткой, символ — вставка. 1 — поглощена.
static uint8_t edit_key(uint8_t k)
{
	uint8_t i = edit_idx();
	if (i == 0xFF || k == KEY_ENTER || k == KEY_ESC) return 0;
	uint8_t n = (uint8_t)strlen(ui_edit), max = W[i].arg ? W[i].arg : UI_EDIT_MAX - 1, p = ed_pos;
	if (max > UI_EDIT_MAX - 1) max = UI_EDIT_MAX - 1;
	if (p > n) p = n;
	if (k == KEY_DEL) { if (p) { memmove(ui_edit + p - 1, ui_edit + p, n - p + 1); p--; } }
	else if (k == KEY_LEFT) { if (p) p--; }
	else if (k == KEY_RIGHT) { if (p < n) p++; }
	else if (k == KEY_UP) p = 0;
	else if (k == KEY_DOWN) p = n;
	else if (k >= 0x20 && k < 0x7F && n < max) { memmove(ui_edit + p + 1, ui_edit + p, n - p + 1); ui_edit[p++] = (char)k; }
	else return 1;
	ed_pos = p;
	ed_blink = 1;
	ed_t = frames;
	edit_redraw(i);
	return 1;
}

static void handle(const event_t *e)
{
	if (e->type == EV_KEY) {
		uint8_t k = e->key;
		if (edit_key(k)) return;
		if (k == KEY_SPACE) k = KEY_ESC;       // вне поля ввода пробел — отмена
		for (uint8_t i = 0; i < S.n; i++)
			if (W[i].key == k) { activate(i); return; }
		if (scr_event(cur, EVT_KEY, k)) redraw_dyn();
		return;
	}
	if (e->type != EV_CLICK && e->type != EV_RCLICK) return;
	uint8_t rclick = e->type == EV_RCLICK;
	rep_on = 0;
	ui_click_x = e->x;
	ui_click_y = e->y;
	for (uint8_t i = S.n; i-- > 0;) {
		const wdef_t *w = &W[i];
		if (w->type == W_LIST) {
			if (list_click(i, e->x, e->y, rclick)) return;
			continue;
		}
		if (w->type == W_ARROW && hit(w, e->x, e->y)) {
			ui_arrow_max = rclick;
			rep_on = !rclick;
			activate(i);
			return;
		}
		if (rclick && !(w->type == W_CUSTOM && (w->flags & WF_RSEL))) continue;   // правая — стрелки, списки, свои виджеты
		if ((w->type == W_BUTTON || w->type == W_TOGGLE || w->type == W_CUSTOM || w->type == W_HOTSPOT
		     || w->type == W_COMBO) && hit(w, e->x, e->y)) {
			ui_arrow_max = rclick;
			activate(i);
			return;
		}
	}
}

// ---------------------------------------------------------------- главный цикл

static uint16_t t_start;

// Сценарий эмулятора: waitmark <номер экрана>; в консоль — кадров на обработку.
static void mark(void)
{
	OXZ_DBG_MARK = cur;
	dbg_puts("ui: screen ");
	dbg_dec(cur);
	dbg_puts(", ");
	dbg_dec((uint16_t)(frames - t_start));
	dbg_puts(" frames\n");
}

void ui_run(uint8_t first_screen) __banked
{
	res_t r;
	event_t e;
	if (res_find(RES_UI, &r)) ui_base = r.phys;
	t_start = frames;
	do_push(first_screen);
	process();
	mark();
	for (;;) {
		wait_frames(1);
		mus_pump();                  // такт музыки: подкачка с карты и долив кольца
		t_start = frames;
		if (ui_request) {
			uint8_t id = ui_request;
			ui_request = 0;
			OXZ_DBG_MARK = 0;
			do_push(id);
			process();
			mark();
			continue;
		}
		if (input_poll(&e)) {
			OXZ_DBG_MARK = 0;
			handle(&e);
			if (rep_on) { rep_ev = e; rep_t = frames; rep_scr = cur; }
			redraw_dirty();
			process();
			mark();
			continue;
		}
		if (rep_on) {
			uint8_t rs = rep_scr;               // SDCC #15242: сравнение с локальной копией
			if (!(mouse_buttons & 1) || cur != rs) rep_on = 0;
			else if ((uint16_t)(frames - rep_t) >= 13) {   // 250 мс, дальше каждые 3 кадра
				handle(&rep_ev);
				rep_t = frames - 10;
				redraw_dirty();
				process();
				continue;
			}
		}
		if ((uint16_t)(frames - ed_t) >= 5) {        // мигание каретки поля ввода
			uint8_t i = edit_idx();
			ed_t = frames;
			if (i != 0xFF) { ed_blink ^= 1; edit_redraw(i); }
		}
		hover();
		if (scr_event(cur, EVT_TICK, 0)) redraw_dyn();
		redraw_dirty();
		if (process()) mark();
	}
}
