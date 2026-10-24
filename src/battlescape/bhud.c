// Панель боя (банк 33): картинка ICONS.PCK и всё, что поверх неё рисует BattlescapeState
// (REF/OpenXcom/src/Battlescape/BattlescapeState.cpp: раскладка — конструктор, :90-150;
// содержимое — updateSoldierInfo, :1276). Цвета — элементы категории battlescape (interfaces.rul).
//   имя бойца — Text 136x10 в (135, 32) панели; значок звания — SMOKE.PCK кадр 20 + звание, (107, 33);
//   числа — NumberText (цифры 3x5): время (136, 42), энергия (154, 42), здоровье (136, 50),
//   мораль (154, 50); полоски Bar 102x3 в x 170: время y 41, энергия 45, здоровье 49 (поверх —
//   оглушение, color2), мораль 53; этаж — NumberText в (232, 6) цвета blockOffset(1) - 2;
//   предметы в руках — BIGOBS.PCK по центру клеток 32x48 в (8, 4) и (280, 4) (drawHandSprite),
//   патроны — NumberText в их левом верхнем углу.
// Нажатые кнопки (BattlescapeButton): «присесть», резерв времени, «на колено» — картинка кнопки
// инвертирована: пиксель p -> p + 2 * (цвет + 3 - p), у TFTD — по таблице цветов (initSurfaces).
#include <stdint.h>
#include <string.h>
#include "far.h"
#include "pages.h"
#include "res.h"
#include "res_ids.h"
#include "rules.h"
#include "ui.h"
#include "ui_ids.h"
#include "gfx.h"
#include "text.h"
#include "state.h"
#include "bitems.h"
#include "bhud.h"
#include "str_ids.h"

#define BA_AUTO  5                       // BattleActionType (bmenu.h)
#define BA_SNAP  6
#define BA_AIMED 7

#define PY 144                               // панель — строки экрана 144..199

// NumberText: цифры 3x5 (Interface/NumberText.cpp), пиксель цифры — цвет + 1, шаг 4 точки
static const uint8_t digits[10][5] = {
	{7,5,5,5,7}, {2,6,2,2,7}, {7,1,7,4,7}, {7,1,7,1,7}, {5,5,7,1,1},
	{7,4,7,1,7}, {7,4,7,5,7}, {7,1,1,1,1}, {7,5,7,5,7}, {7,5,7,1,7},
};

static void number(int16_t x, int16_t y, uint16_t v, uint8_t c)
{
	char s[6];
	fmt_num(s, v, 0);
	uint8_t n = (uint8_t)strlen(s), old = pg_win3();
	for (uint8_t r = 0; r < 5; r++) {
		uint8_t *p = gfx_map(x, y + r);
		for (uint8_t i = 0; i < n; i++, p += 4) {
			uint8_t g = digits[s[i] - '0'][r];
			if (g & 4) p[0] = c + 1;
			if (g & 2) p[1] = c + 1;
			if (g & 1) p[2] = c + 1;
		}
	}
	pg_map3(old);
}

// Bar::draw (scale 1): рамка шириной max + 1 цвета color + 4, внутри строка нулём, на ней —
// значение и поверх второе (оглушение)
static void bar(int16_t y, uint8_t v, uint8_t v2, uint8_t max, uint8_t c, uint8_t c2)
{
	uint8_t w = max < 101 ? max : 101;
	gfx_fill(170, y, w + 1, 3, c + 4);
	gfx_fill(170, y + 1, w, 1, 0);
	if (v) gfx_fill(170, y + 1, v < w ? v : w, 1, c);
	if (v2) gfx_fill(170, y + 1, v2 < w ? v2 : w, 1, c2);
}

// Инверсия кнопки (BattlescapeButton::initSurfaces)
static const uint8_t tftd_from[11] = { 1, 2, 3, 4, 7, 8, 31, 47, 153, 156, 159 };
static const uint8_t tftd_to[11] = { 2, 3, 4, 5, 11, 10, 2, 2, 96, 9, 97 };

static void invert(int16_t x, int16_t y, uint8_t w, uint8_t h, uint8_t color)
{
	uint8_t old = pg_win3(), tftd = res_game() == 2;
	for (uint8_t r = 0; r < h; r++) {
		uint8_t *p = gfx_map(x, y + r);
		for (uint8_t i = 0; i < w; i++) {
			uint8_t v = p[i];
			if (!v) continue;
			if (tftd) {
				for (uint8_t k = 0; k < 11; k++)
					if (v == tftd_from[k]) { p[i] = tftd_to[k]; break; }
			} else p[i] = (uint8_t)(v + 2 * ((int16_t)color + 3 - v));
		}
	}
	pg_map3(old);
}

// Предмет в руке: кадр BIGOBS по центру клетки 2x3 слотов по 16 (RuleItem::drawHandSprite),
// у оружия с обоймой — патроны (999 у NumberText — только «не задано»)
static void hand(const bhud_t *h, uint8_t right, int16_t x, uint8_t c_ammo)
{
	bitem_t b;
	rtab_t t;
	r_items_t it;
	if (!bi_hand(h->page, h->unit, right, &b) || !rtab_open(RES_RULE_ITEMS, &t) || b.item >= t.n) return;
	rtab_get(&t, b.item, &it);
	if (it.big_sprite >= 0) {
		uint8_t iw = it.inv_width ? it.inv_width : 1, ih = it.inv_height ? it.inv_height : 1;
		gfx_sprite(RES_BIGOBS_PCK, (uint16_t)it.big_sprite, x + (2 - iw) * 8, PY + 4 + (3 - ih) * 8);
	}
	if (it.battle_type == BT_FIREARM && (it.compatible_ammo.n || it.clip_size > 0))
		number(x, PY + 4, b.ammo != 0xFFFF || it.clip_size > 0 ? b.rounds : 0, c_ammo);
}

// ---------------------------------------------------------------- WarningMessage
// REF/OpenXcom/src/Battlescape/WarningMessage.cpp: фон — цвет элемента warning + шаг затухания
// (0..12, раз в 50 мс), текст — color2 с контрастом, по центру, мелким шрифтом; на 24-м шаге
// плашка исчезает
static uint16_t wr_str;
static uint16_t wr_t0;
static uint8_t wr_step;                  // #FF — плашки нет
extern volatile uint16_t frames;

static void warn_draw(void)
{
	uint8_t c = ui_color_in(UI_SCR_BATTLESCAPE, UI_EL_WARNING, 0), t = ui_color_in(UI_SCR_BATTLESCAPE, UI_EL_WARNING, 1);
	gfx_fill(48, PY + 32, 224, 24, (uint8_t)(c + (wr_step > 12 ? 12 : wr_step)));
	tbox_t tb = { 48, PY + 32, 224, 24, FNT_SMALL, t, t, TX_CENTER | TX_MIDDLE | TX_WRAP | TX_CONTRAST };
	text_draw(&tb, str_get(wr_str));
}

void hud_warn(uint16_t str) __banked
{
	wr_str = str;
	wr_step = 0;
	wr_t0 = frames;
	warn_draw();
}

uint8_t hud_warn_tick(void) __banked
{
	if (wr_step == 0xFF || wr_step >= 24) return 0;
	uint16_t s = (uint16_t)(frames - wr_t0) * 20 / 50;   // кадр — 20 мс, шаг — 50
	if (s == wr_step) return 0;
	wr_step = s > 24 ? 24 : (uint8_t)s;
	if (wr_step >= 24) { wr_step = 0xFF; return 1; }
	if (wr_step <= 12) warn_draw();      // дальше цвет уже не меняется
	return 0;
}

// ---------------------------------------------------------------- резерв времени
// Оружие из рук, на которое держится резерв (BattleUnit::getMainHandWeapon(false), сторона
// игрока): заряженное (или без обоймы вовсе); из двух — активная рука, если у обоих есть
// выстрел навскидку, иначе то, у которого он есть
static uint8_t armed(const bitem_t *b, r_items_t *it)
{
	rtab_t t;
	if (!rtab_open(RES_RULE_ITEMS, &t) || b->item >= t.n) return 0;
	rtab_get(&t, b->item, it);
	if (it->battle_type != BT_FIREARM && it->battle_type != BT_MELEE) return 0;
	return !it->compatible_ammo.n || (b->ammo != 0xFFFF && b->rounds);
}

static uint8_t act_tu(uint8_t ba, const r_items_t *w, uint8_t tu_max)
{
	uint16_t c = ba == BA_SNAP ? w->tu_snap : ba == BA_AUTO ? w->tu_auto : ba == BA_AIMED ? w->tu_aimed : 0;
	if (!(w->flags & ITEMS_F_FLAT_RATE)) c = (uint16_t)tu_max * c / 100;
	return c > 255 ? 255 : (uint8_t)c;
}

uint8_t hud_reserve(const rsreq_t *r, uint8_t cost) __banked
{
	bitem_t br, bl;
	r_items_t wr_, wl, *w = 0;
	uint8_t hr = bi_hand(r->page, r->unit, 1, &br) && armed(&br, &wr_);
	uint8_t hl = bi_hand(r->page, r->unit, 0, &bl) && armed(&bl, &wl);
	if (hr && !hl) w = &wr_;
	else if (hl && !hr) w = &wl;
	else if (hr && hl) {
		if (!wl.tu_snap && wr_.tu_snap) w = &wr_;
		else if (!wr_.tu_snap && wl.tu_snap) w = &wl;
		else w = r->hand ? &wr_ : &wl;
	}
	uint8_t res = r->reserve;            // BA_* резерва
	if (w && res == BA_AUTO && !act_tu(BA_AUTO, w, r->tu_max)) res = BA_SNAP;
	if (w && res == BA_SNAP && !act_tu(BA_SNAP, w, r->tu_max)) res = BA_AIMED;
	uint8_t kn = r->res_kneel && !r->kneeled ? 4 : 0;
	uint8_t need = w ? act_tu(res, w, r->tu_max) : 0;
	if (!need && res == BA_AIMED) { if (kn) res = 0; else return 1; }
	if (!w) res = 0;
	if ((res || r->res_kneel) && (uint16_t)cost + kn + need > r->tu && kn + need <= r->tu) {
		uint16_t s;
		if (kn) s = res ? STR_TIME_UNITS_RESERVED_FOR_KNEELING_AND_FIRING : STR_TIME_UNITS_RESERVED_FOR_KNEELING;
		else s = r->reserve == BA_SNAP ? STR_TIME_UNITS_RESERVED_FOR_SNAP_SHOT
			: r->reserve == BA_AUTO ? STR_TIME_UNITS_RESERVED_FOR_AUTO_SHOT : STR_TIME_UNITS_RESERVED_FOR_AIMED_SHOT;
		hud_warn(s);
		return 0;
	}
	return 1;
}

void hud_draw(const bhud_t *h) __banked
{
	uint8_t ui = UI_SCR_BATTLESCAPE;
	if (!h) { wr_step = 0xFF; return; }      // вход в бой: плашки нет (_DATA обнулён, а 0 — «видна»)
	gfx_blit(RES_ICONS_PCK, 0, PY, 0, PY, 320, 200 - PY);
	if (res_game() == 2) {                   // заплатка OpenXcom: прозрачная точка панели TFTD
		uint8_t old = pg_win3();
		*gfx_map(46, PY + 44) = 8;
		pg_map3(old);
	}
	number(232, PY + 6, h->level + 1, 14);   // _numLayers->setColor(Palette::blockOffset(1) - 2)
	// нажатые кнопки резерва времени — группа, кнопка резерва «на колено» — переключатель
	static const uint8_t rx[4] = { 60, 78, 78, 60 }, ry[4] = { 33, 33, 45, 45 };
	static const uint8_t rel[4] = { UI_EL_BUTTONRESERVENONE, UI_EL_BUTTONRESERVESNAP,
					UI_EL_BUTTONRESERVEAUTO, UI_EL_BUTTONRESERVEAIMED };
	uint8_t r = h->reserve & 3;
	invert(rx[r], PY + ry[r], 17, 11, ui_color_in(ui, rel[r], 0));
	if (h->res_kneel) invert(96, PY + 33, 10, 23, ui_color_in(ui, UI_EL_BUTTONRESERVEKNEEL, 0));
	if (!h->on) return;
	if (h->kneel) invert(112, PY + 16, 32, 16, 0);   // «присесть»: элемента buttonKneel в правилах нет
	if (h->sol != 0xFF) {
		soldier_t s;
		far_read(FAR(SOLDIER_PAGE, 0) + (uint16_t)h->sol * sizeof s, &s, sizeof s);
		gfx_sprite(RES_SMOKE_PCK, 20 + s.rank, 107, PY + 33);
		char name[SOLDIER_NAME + 1];
		memcpy(name, s.name, SOLDIER_NAME);
		name[SOLDIER_NAME] = 0;
		tbox_t tb = { 135, PY + 32, 136, 10, FNT_SMALL, 0, 0, 0 };
		tb.color = tb.color2 = ui_color_in(ui, UI_EL_TEXTNAME, 0);
		text_draw(&tb, name);
	}
	number(136, PY + 42, h->tu, ui_color_in(ui, UI_EL_NUMTUS, 0));
	number(154, PY + 42, h->en, ui_color_in(ui, UI_EL_NUMENERGY, 0));
	number(136, PY + 50, h->hp, ui_color_in(ui, UI_EL_NUMHEALTH, 0));
	number(154, PY + 50, h->mor, ui_color_in(ui, UI_EL_NUMMORALE, 0));
	bar(PY + 41, h->tu, 0, h->tu_max, ui_color_in(ui, UI_EL_BARTUS, 0), 0);
	bar(PY + 45, h->en, 0, h->en_max, ui_color_in(ui, UI_EL_BARENERGY, 0), 0);
	bar(PY + 49, h->hp, h->stun, h->hp_max, ui_color_in(ui, UI_EL_BARHEALTH, 0), ui_color_in(ui, UI_EL_BARHEALTH, 1));
	bar(PY + 53, h->mor, 0, 100, ui_color_in(ui, UI_EL_BARMORALE, 0), 0);
	hand(h, 0, 8, ui_color_in(ui, UI_EL_NUMAMMOLEFT, 0));
	hand(h, 1, 280, ui_color_in(ui, UI_EL_NUMAMMORIGHT, 0));
	if (wr_step < 24) warn_draw();       // плашка ещё видна — поверх перерисованной панели
}
