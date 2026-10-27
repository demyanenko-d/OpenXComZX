// Гранаты (банк 38): окно таймера PrimeGrenadeState (REF/OpenXcom/src/Battlescape/PrimeGrenadeState.cpp)
// и взвод (BattlescapeGame::handleNonTargetAction, BA_PRIME). Окно: фон 192x93 в (65, 45) цветом
// grenadeBackground.color; рамка Frame 192x27 в (65, 37) толщиной 3 с контрастом (средняя линия —
// grenadeMenu.border, фон — color2); заголовок «SET TIMER» крупно по центру в (65, 44) 192x24;
// 24 кнопки 22x22 в (66 + i%8*24, 64 + i/8*25): рамка grenadeBackground.border, внутри color2,
// число 0..23 крупно по центру клетки 20x20 в (67 + i%8*24, 67 + i/8*25) цветом grenadeMenu.color.
// Таймер — в rounds предмета (bitems.h): 0 — не взведена, иначе таймер + 1.
#include <stdint.h>
#include <string.h>
#include <stddef.h>
#include "far.h"
#include "res.h"
#include "rules.h"
#include "str_ids.h"
#include "ui.h"
#include "ui_ids.h"
#include "gfx.h"
#include "text.h"
#include "loader.h"
#include "bitems.h"
#include "bgren.h"

// Frame::draw: от средней линии color на (1 + толщина) · mul / 2 светлее, к середине толщины — темнее
static void frame3(int16_t x, int16_t y, int16_t w, int16_t h, uint8_t color, uint8_t bg)
{
	uint8_t c = color + 4;                   // (1 + 3) · 2 / 2
	for (uint8_t i = 0; i < 3; i++) {
		gfx_fill(x, y, w, 1, c);
		gfx_fill(x, y + h - 1, w, 1, c);
		gfx_fill(x, y, 1, h, c);
		gfx_fill(x + w - 1, y, 1, h, c);
		if (i < 1) c -= 2; else c += 2;
		x++; y++; w -= 2; h -= 2;
	}
	gfx_fill(x, y, w, h, bg);
}

void gr_draw(void) __banked
{
	uint8_t ui = UI_SCR_BATTLESCAPE;
	uint8_t mc = ui_color_in(ui, UI_EL_GRENADEMENU, 0), mbg = ui_color_in(ui, UI_EL_GRENADEMENU, 1);
	uint8_t mbd = ui_color_in(ui, UI_EL_GRENADEMENU, 2);
	uint8_t bc = ui_color_in(ui, UI_EL_GRENADEBACKGROUND, 0), bc2 = ui_color_in(ui, UI_EL_GRENADEBACKGROUND, 1);
	uint8_t bbd = ui_color_in(ui, UI_EL_GRENADEBACKGROUND, 2);
	gfx_fill(65, 45, 192, 93, bc);
	frame3(65, 37, 192, 27, mbd, mbg);
	tbox_t tb = { 65, 44, 192, 24, FNT_BIG, mc, mc, TX_CENTER | TX_CONTRAST };
	text_draw(&tb, str_get(STR_SET_TIMER));
	tb.w = 20; tb.h = 20; tb.flags = TX_CENTER | TX_MIDDLE | TX_CONTRAST;
	for (uint8_t i = 0; i < 24; i++) {
		int16_t x = 66 + (i & 7) * 24, y = 64 + (i >> 3) * 25;
		gfx_fill(x, y, 22, 22, bbd);
		gfx_fill(x + 1, y + 1, 20, 20, bc2);
		char s[4];
		fmt_num(s, i, 0);
		tb.x = x + 1; tb.y = y + 3;
		text_draw(&tb, s);
	}
}

uint8_t gr_hit(int16_t x, int16_t y) __banked
{
	for (uint8_t i = 0; i < 24; i++) {
		int16_t bx = 66 + (i & 7) * 24, by = 64 + (i >> 3) * 25;
		if (x >= bx && x < bx + 22 && y >= by && y < by + 22) return i;
	}
	return 0xFF;
}

uint8_t gr_window(uint8_t page, uint8_t unit, uint8_t right) __banked
{
	bitem_t b;
	rtab_t t;
	if (!bi_hand(page, unit, right, &b) || !rtab_open(RES_RULE_ITEMS, &t) || b.item >= t.n) return 0;
	return (uint8_t)rtab_word(&t, b.item, offsetof(r_items_t, battle_type)) != BT_PROXIMITYGRENADE;
}

void gr_prime(uint8_t page, uint8_t unit, uint8_t right, uint8_t fuse) __banked
{
	bitem_t b;
	uint16_t i = bi_hand(page, unit, right, &b);
	if (!i) return;
	b.rounds = fuse + 1;
	far_write(FAR(page + 1, TL_BITEMS) + (i - 1) * sizeof b, &b, sizeof b);
}
