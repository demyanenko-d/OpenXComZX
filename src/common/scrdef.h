// Описания экранов в банках экранов: макросы виджетов и поиск экрана по номеру.
// Раскладки — из OpenXcom (tmp/screens_geoscape.json, 08_ui_port_plan.md §6).
// Константы банка видны только коду этого же банка: тексты для ядра копируются
// в буфер Win1 (scr_text), описания — в S/W ядра (scr_find).
#ifndef SCRDEF_H
#define SCRDEF_H

#include <stdint.h>
#include <string.h>
#include "ui.h"
#include "ui_ids.h"
#include "str_ids.h"
#include "res_ids.h"
#include "input.h"
#include "screens.h"

typedef struct { uint8_t id; sdef_t s; const wdef_t *w; } scr_t;

#define SCR(id, ui, palui, bg, fl, arr) { id, { ui, palui, bg, fl, sizeof(arr) / sizeof(arr[0]) }, arr }
#define NOUI 0xFF

#define WIN(x, y, w, h, el)                   { W_WINDOW, x, y, w, h, el, NOSTR, 0, 0, 0, 0 }
#define WINP(x, y, w, h, el, pop)             { W_WINDOW, x, y, w, h, el, NOSTR, pop, 0, 0, 0 }
#define POPB (WF_POPH | WF_POPV)                // POPUP_BOTH
#define POPH WF_POPH                            // POPUP_HORIZONTAL
#define POPV WF_POPV                            // POPUP_VERTICAL
#define TXT(x, y, w, h, el, str, fl)          { W_TEXT, x, y, w, h, el, str, fl, 0, 0, 0 }
#define BTN(x, y, w, h, el, str, act, arg, key) { W_BUTTON, x, y, w, h, el, str, 0, act, arg, key }
#define BTNF(x, y, w, h, el, str, fl, act, arg, key) { W_BUTTON, x, y, w, h, el, str, fl, act, arg, key }
#define TGL(x, y, w, h, el, str, fl, grp, val, key) { W_TOGGLE, x, y, w, h, el, str, fl, grp, val, key }
#define LST(x, y, w, h, el, slot, fl)         { W_LIST, x, y, w, h, el, DYN(slot), fl, 0, 0, 0 }
#define IMG(x, y, w, h, res)                  { W_IMAGE, x, y, w, h, 0xFF, res, 0, 0, 0, 0 }
#define IMGAT(x, y, w, h, res, ox, oy)        { W_IMAGE, x, y, w, h, 0xFF, res, 0, (ox) / 2, (oy) / 2, 0 }
#define HOT(x, y, w, h, act, arg, key)        { W_HOTSPOT, x, y, w, h, 0xFF, NOSTR, 0, act, arg, key }
#define CUS(x, y, w, h, str, act, arg)        { W_CUSTOM, x, y, w, h, 0xFF, str, 0, act, arg, 0 }
#define CUSR(x, y, w, h, str, act, arg)       { W_CUSTOM, x, y, w, h, 0xFF, str, WF_RSEL, act, arg, 0 }   // и правая кнопка (ui_arrow_max = 1)
#define FIL(x, y, w, h, el, c)                { W_FILL, x, y, w, h, el, NOSTR, 0, 0, c, 0 }

// Флаги текста коротко
#define TC   TX_CENTER
#define TR   TX_RIGHT
#define TM   TX_MIDDLE
#define TW   TX_WRAP
#define BIG  WF_BIG
#define ESC  KEY_ESC
#define ENT  KEY_ENTER

// Кнопки вращения и масштаба глобуса поверх рисунка GEOBORD (InteractiveSurface).
#define GLOBE_HOTSPOTS \
	HOT(259, 176, 12, 10, A_NONE, 0, 0), \
	HOT(283, 176, 12, 10, A_NONE, 0, 0), \
	HOT(271, 162, 13, 12, A_NONE, 0, 0), \
	HOT(271, 187, 13, 12, A_NONE, 0, 0), \
	HOT(295, 156, 23, 23, A_NONE, 0, 0), \
	HOT(300, 182, 13, 17, A_NONE, 0, 0)

// Резидент (scrutil.c, Win0): таблица и массивы — константы банка вызывающего, он
// остаётся подключён на время вызова (в чужом банке их не видно). static inline в
// заголовке нельзя: с --debug SDCC делает глобальные символы строк C$scrdef.h$…
uint8_t scr_find(const scr_t *t, uint8_t n, uint8_t id, sdef_t *s, wdef_t *w);
// Раскладка колонок списка (ответ на scr_text(..., LIST_COLS, buf)); arrows —
// колонка стрелок ± (x от левого края списка), 0 — нет; horiz — стрелки влево/вправо.
void list_cols_ext(char *buf, uint8_t n, const uint8_t *w, const uint8_t *align, uint8_t arrows, uint8_t horiz);
#define list_cols_harrows(buf, n, w, align, arrows) list_cols_ext(buf, n, w, align, arrows, 1)
#define list_cols_arrows(buf, n, w, align, arrows)  list_cols_ext(buf, n, w, align, arrows, 0)
#define list_cols(buf, n, w, align)                 list_cols_ext(buf, n, w, align, 0, 0)

#endif
