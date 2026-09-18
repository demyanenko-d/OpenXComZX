// Экран инвентаря бойца (InventoryState OpenXcom, REF/OpenXcom/src/Battlescape/InventoryState.cpp):
// кукла в броне, предметы по секциям (правила invs: руки, пояс, рюкзак, плечи, ноги) и «земля» —
// груз корабля. Рисуется поверх фона TAC01.SCR палитрой боя; спрайты предметов — BIGOBS.PCK
// (32x48 = 2x3 клетки по 16). Записи инвентаря — общий пул в состоянии (state.h, inv_t).
//
// Пока — просмотр и перекладывание кликом: взять предмет (первый клик) и положить в клетку
// (второй). Правая кнопка по оружию вынимает обойму на землю. Экран открывается из снаряжения
// корабля («Inventory»), поэтому боец берётся из экипажа: стрелки листают экипаж.
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "res.h"
#include "res_ids.h"
#include "rules.h"
#include "ui.h"
#include "text.h"
#include "gfx.h"
#include "state.h"
#include "game.h"
#include "scrdef.h"
#include "input.h"

#define SLOT_W    16
#define SLOT_H    16
#define HAND_W    2
#define HAND_H    3
#define INV_GROUND  0                // порядок записей invs: земля первой (inventories.rul)
#define GROUND_COLS 12               // клеток «земли» в ряд (x 0..191 при прокрутке 0)

static uint8_t sel_sol;              // боец: запись в state (soldier_nth по экипажу)
static uint8_t crew_i;               // его номер в экипаже корабля
static uint16_t carry;               // взятая запись инвентаря (INV_NONE — ничего)
static uint8_t ground_x;             // прокрутка «земли»

static const wdef_t w_inv[] = {
	IMG(0, 0, 320, 200, RES_TAC01_SCR),
	CUS(0, 0, 320, 200, DYN(9), A_CUSTOM, 1),      // кукла, предметы, земля (рисует экран)
	TXT(28, 6, 210, 17, EL_RAW + 0, DYN(0), BIG),
	TXT(245, 24, 70, 9, EL_RAW + 1, DYN(1), 0),    // вес
	TXT(128, 140, 160, 9, EL_RAW + 1, DYN(2), 0),  // предмет под курсором
	HOT(237, 1, 35, 22, A_POP, 0, ESC),            // OK
	HOT(273, 1, 23, 22, A_CUSTOM, 2, 0),           // предыдущий боец
	HOT(297, 1, 23, 22, A_CUSTOM, 3, 0),           // следующий
};

static const scr_t tab[] = {
	SCR(SCR_INVENTORY, UI_SCR_INVENTORY, RES_PAL_BATTLESCAPE, RES_TAC01_SCR, SF_RAWPAL, w_inv),
};

// --- правила секций

static uint8_t inv_sections(void)
{
	rtab_t t;
	rtab_open(RES_RULE_INVS, &t);
	return (uint8_t)t.n;
}

static void inv_section(uint8_t i, r_invs_t *r)
{
	rtab_t t;
	rtab_open(RES_RULE_INVS, &t);
	rtab_get(&t, i, r);
}

// Экранные координаты клетки (x, y) секции
static void slot_xy(const r_invs_t *r, uint8_t x, uint8_t y, int16_t *sx, int16_t *sy)
{
	*sx = (int16_t)r->x + (int16_t)x * SLOT_W;
	*sy = (int16_t)r->y + (int16_t)y * SLOT_H;
}

// Сетка слотов (Inventory::drawGrid): рамка каждой клетки цветом сетки, внутри — чёрное
static void grid_rect(int16_t x, int16_t y, int16_t w, int16_t h, uint8_t c)
{
	gfx_fill(x, y, w, 1, c);
	gfx_fill(x, y + h - 1, w, 1, c);
	gfx_fill(x, y, 1, h, c);
	gfx_fill(x + w - 1, y, 1, h, c);
}

static void draw_grid(void)
{
	r_invs_t r;
	uint8_t ns = inv_sections(), c = 5;          // interfaces.rul: inventory.grid
	for (uint8_t i = 0; i < ns; i++) {
		inv_section(i, &r);
		if (r.type == 1) {                       // рука: одна рамка 2x3 клетки
			grid_rect((int16_t)r.x, (int16_t)r.y, HAND_W * SLOT_W, HAND_H * SLOT_H, c);
		} else if (r.type == 2) {                // земля: ряд клеток до края экрана
			for (int16_t x = (int16_t)r.x; x + SLOT_W <= 320; x += SLOT_W)
				grid_rect(x, (int16_t)r.y, SLOT_W + 1, SLOT_H + 1, c);
		} else {                                 // сетка: клетки по маске
			for (uint8_t y = 0; y < r.rows; y++)
				for (uint8_t x = 0; x < r.cols; x++) {
					if (!(r.mask & (1u << (y * 4 + x)))) continue;
					int16_t sx, sy;
					slot_xy(&r, x, y, &sx, &sy);
					grid_rect(sx, sy, SLOT_W + 1, SLOT_H + 1, c);
				}
		}
	}
}

// --- предметы бойца и земли

static uint8_t item_big(uint16_t item, int16_t *w, int16_t *h)
{
	rtab_t t;
	r_items_t it;
	rtab_open(RES_RULE_ITEMS, &t);
	if (item >= t.n) return 0;
	rtab_get(&t, item, &it);
	*w = it.inv_width ? it.inv_width : 1;
	*h = it.inv_height ? it.inv_height : 1;
	return it.big_sprite >= 0 ? (uint8_t)it.big_sprite : 0xFF;
}

// Спрайт предмета в клетке секции (кадр BIGOBS — 32x48, рисуется как есть)
static void draw_item(uint16_t item, int16_t x, int16_t y)
{
	int16_t w, h;
	uint8_t sp = item_big(item, &w, &h);
	if (sp == 0xFF) return;
	gfx_sprite(RES_BIGOBS_PCK, sp, x, y);
}

static void draw_soldier(void)
{
	soldier_t so;
	rtab_t t;
	r_armors_t ar;
	soldier_get(sel_sol, &so);
	if (so.armor == NONE8) return;
	rtab_open(RES_RULE_ARMORS, &t);
	rtab_get(&t, so.armor, &ar);
	if (!ar.inv_image) return;
	gfx_key = 1;                                 // кукла — картинка 320x200 с прозрачным фоном
	gfx_blit(ar.inv_image, 0, 0, 0, 0, 320, 200);
	gfx_key = 0;
}

// Предметы бойца по секциям и груз корабля на «земле»
static void draw_items(void)
{
	inv_t v;
	r_invs_t r;
	uint8_t ns = inv_sections();
	for (uint16_t i = 0; i < ST->ninv; i++) {
		inv_get(i, &v);
		if (v.soldier != sel_sol || v.slot >= ns || i == carry) continue;
		inv_section(v.slot, &r);
		int16_t x, y;
		slot_xy(&r, v.x, v.y, &x, &y);
		draw_item(v.item, x, y);
	}
	// земля: груз корабля по клеткам слева направо
	craft_t *cr = &ST->craft[ctx.craft];
	if (cr->cargo == NONE8) return;
	inv_section(INV_GROUND, &r);
	uint8_t k = 0;
	for (uint8_t i = 0; i < CARGO_ITEMS; i++) {
		uint8_t it = ST->cargo[cr->cargo].it[i].item;
		if (it == NONE8 || !ST->cargo[cr->cargo].it[i].qty) continue;
		if (k >= ground_x && k - ground_x < GROUND_COLS) {
			int16_t x, y;
			slot_xy(&r, (uint8_t)(k - ground_x), 0, &x, &y);
			draw_item(it, x, y);
		}
		k++;
	}
}

// --- перекладывание: груз корабля («земля») <-> слоты бойца

// Предмет груза по клетке земли (k — номер клетки с учётом прокрутки); NONE8 — пусто
static uint8_t ground_item(uint8_t cell, uint8_t *slot_out)
{
	craft_t *cr = &ST->craft[ctx.craft];
	uint8_t k = 0;
	if (cr->cargo == NONE8) return NONE8;
	for (uint8_t i = 0; i < CARGO_ITEMS; i++) {
		if (ST->cargo[cr->cargo].it[i].item == NONE8 || !ST->cargo[cr->cargo].it[i].qty) continue;
		if (k == cell + ground_x) { if (slot_out) *slot_out = i; return ST->cargo[cr->cargo].it[i].item; }
		k++;
	}
	return NONE8;
}

static void ground_add(uint8_t item)
{
	craft_t *cr = &ST->craft[ctx.craft];
	if (cr->cargo == NONE8) return;
	for (uint8_t i = 0; i < CARGO_ITEMS; i++)
		if (ST->cargo[cr->cargo].it[i].item == item) { ST->cargo[cr->cargo].it[i].qty++; return; }
	for (uint8_t i = 0; i < CARGO_ITEMS; i++)
		if (ST->cargo[cr->cargo].it[i].item == NONE8) {
			ST->cargo[cr->cargo].it[i].item = item;
			ST->cargo[cr->cargo].it[i].qty = 1;
			return;
		}
}

// Запись инвентаря в клетке секции (или INV_NONE): предмет занимает inv_width x inv_height
static uint16_t item_at(uint8_t slot, uint8_t x, uint8_t y)
{
	inv_t v;
	int16_t w, h;
	for (uint16_t i = 0; i < ST->ninv; i++) {
		inv_get(i, &v);
		if (v.soldier != sel_sol || v.slot != slot) continue;
		item_big(v.item, &w, &h);
		if (x >= v.x && x < v.x + w && y >= v.y && y < v.y + h) return i;
	}
	return INV_NONE;
}

// Помещается ли предмет в секцию с клетки (x, y): клетки есть в маске и свободны
static uint8_t fits(const r_invs_t *r, uint8_t slot, uint16_t item, uint8_t x, uint8_t y)
{
	int16_t w, h;
	item_big(item, &w, &h);
	if (r->type == 1) return x == 0 && y == 0 && w <= HAND_W && h <= HAND_H && item_at(slot, 0, 0) == INV_NONE;
	for (int16_t j = 0; j < h; j++)
		for (int16_t i = 0; i < w; i++) {
			uint8_t cx = x + (uint8_t)i, cy = y + (uint8_t)j;
			if (cx >= r->cols || cy >= r->rows) return 0;
			if (!(r->mask & (1u << (cy * 4 + cx)))) return 0;
			if (item_at(slot, cx, cy) != INV_NONE) return 0;
		}
	return 1;
}

// Секция и клетка под точкой экрана; 0xFF — мимо
static uint8_t hit_slot(int16_t px, int16_t py, uint8_t *cx, uint8_t *cy)
{
	r_invs_t r;
	uint8_t ns = inv_sections();
	for (uint8_t i = 0; i < ns; i++) {
		inv_section(i, &r);
		int16_t w = r.type == 1 ? HAND_W * SLOT_W : r.type == 2 ? 320 - (int16_t)r.x : (int16_t)r.cols * SLOT_W;
		int16_t h = r.type == 1 ? HAND_H * SLOT_H : r.type == 2 ? SLOT_H : (int16_t)r.rows * SLOT_H;
		if (px < (int16_t)r.x || py < (int16_t)r.y || px >= (int16_t)r.x + w || py >= (int16_t)r.y + h) continue;
		*cx = (uint8_t)((px - (int16_t)r.x) / SLOT_W);
		*cy = (uint8_t)((py - (int16_t)r.y) / SLOT_H);
		if (r.type == 1) { *cx = 0; *cy = 0; }
		return i;
	}
	return 0xFF;
}

// Клик: взять предмет или положить взятый
static void click_at(int16_t px, int16_t py)
{
	r_invs_t r;
	uint8_t cx, cy, slot = hit_slot(px, py, &cx, &cy);
	if (slot == 0xFF) return;
	inv_section(slot, &r);
	if (carry == INV_NONE) {                     // взять
		if (r.type == 2) {
			uint8_t ci, it = ground_item(cx, &ci);
			if (it == NONE8) return;
			uint16_t rec = inv_alloc();
			if (rec == INV_NONE) return;
			inv_t v = { sel_sol, slot, cx, cy, it, NONE16, 0 };
			inv_put(rec, &v);
			ST->cargo[ST->craft[ctx.craft].cargo].it[ci].qty--;
			if (!ST->cargo[ST->craft[ctx.craft].cargo].it[ci].qty)
				ST->cargo[ST->craft[ctx.craft].cargo].it[ci].item = NONE8;
			carry = rec;
		} else {
			carry = item_at(slot, cx, cy);
		}
		return;
	}
	inv_t v;                                     // положить
	inv_get(carry, &v);
	if (r.type == 2) {                           // назад в груз корабля
		ground_add((uint8_t)v.item);
		v.soldier = NONE8;
		inv_put(carry, &v);
		carry = INV_NONE;
		return;
	}
	uint16_t keep = carry;
	carry = INV_NONE;                            // fits не должен считать взятый предмет занятым
	inv_t t;
	inv_get(keep, &t);
	t.soldier = NONE8;
	inv_put(keep, &t);
	if (!fits(&r, slot, v.item, cx, cy)) { inv_put(keep, &v); carry = keep; return; }
	v.slot = slot; v.x = cx; v.y = cy;
	inv_put(keep, &v);
}

// --- экран

uint8_t inv_scr_get(uint8_t id, sdef_t *s, wdef_t *w) __banked
{
	if (!scr_find(tab, sizeof tab / sizeof tab[0], id, s, w)) return 0;
	ui_raw[0][0] = 48; ui_raw[0][1] = 16;        // interfaces.rul: textName, textWeight
	ui_raw[1][0] = 48; ui_raw[1][1] = 16;
	return 1;
}

void inv_scr_text(uint8_t id, uint8_t slot, uint8_t row, char *buf) __banked
{
	(void)id; (void)row;
	soldier_t so;
	buf[0] = 0;
	switch (slot) {
	case 0:
		soldier_get(sel_sol, &so);
		strcpy(buf, so.name);
		break;
	case 1: {                                    // вес: сумма weight предметов бойца
		rtab_t t;
		inv_t v;
		uint16_t wt = 0;
		rtab_open(RES_RULE_ITEMS, &t);
		for (uint16_t i = 0; i < ST->ninv; i++) {
			inv_get(i, &v);
			if (v.soldier != sel_sol) continue;
			wt += (uint8_t)rtab_word(&t, v.item, offsetof(r_items_t, weight));
		}
		str_copy(STR_WEIGHT, buf, 32);
		strcat(buf, "> ");
		fmt_num(buf + strlen(buf), wt, 0);
		break;
	}
	}
}

// Экипаж корабля: номер бойца в state по его месту в списке
static uint8_t crew_nth(uint8_t n)
{
	soldier_t so;
	uint8_t k = 0;
	for (uint8_t i = 0; i < ST->nsoldiers; i++) {
		soldier_get(i, &so);
		if (so.base == NONE8 || so.craft != ctx.craft) continue;
		if (k == n) return i;
		k++;
	}
	return NONE8;
}

static uint8_t crew_count(void)
{
	soldier_t so;
	uint8_t k = 0;
	for (uint8_t i = 0; i < ST->nsoldiers; i++) {
		soldier_get(i, &so);
		if (so.base != NONE8 && so.craft == ctx.craft) k++;
	}
	return k;
}

uint8_t inv_scr_event(uint8_t id, uint8_t ev, uint8_t arg) __banked
{
	(void)id;
	switch (ev) {
	case EVT_OPEN:
		crew_i = 0;
		carry = INV_NONE;
		ground_x = 0;
		sel_sol = crew_nth(crew_i);
		break;
	case EVT_DRAW:
		draw_grid();
		draw_soldier();
		draw_items();
		break;
	case EVT_BUTTON: {
		if (arg == 1) {                          // клик по кукле, слотам или земле
			click_at(ui_click_x, ui_click_y);
			ui_dirty(1);
			ui_dirty(9);
			break;
		}
		uint8_t n = crew_count();
		if (!n || sel_sol == NONE8) break;
		if (arg == 2) crew_i = crew_i ? crew_i - 1 : n - 1;
		else if (arg == 3) crew_i = crew_i + 1 < n ? crew_i + 1 : 0;
		else break;
		sel_sol = crew_nth(crew_i);
		ui_dirty(0);
		ui_dirty(1);
		ui_dirty(9);
		break;
	}
	}
	return 0;
}
