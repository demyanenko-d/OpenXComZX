// Аптечка (банк 35): MedikitState и MedikitView (REF/OpenXcom/src/Battlescape/MedikitState.cpp,
// MedikitView.cpp). Окно на фоне MEDIBORD.PCK: тело (кадры MEDIBITS.DAT — зелёные целые части,
// красные раненые: конвертер готовит оба цвета), название выбранной части и число её ран, три
// кнопки с зарядами — обезболивающее, стимулятор, лечение; конец — своей кнопкой или правой.
// Цель (ActionMenuState, BA_USE аптечкой) — юнит в клетке перед лечащим, иначе он сам. Заряды —
// в записи предмета: rounds — лечение, ammo — обезболивающее (старший байт) и стимулятор (#FFFF —
// аптечка ещё полна: заряды из правил). Каждое действие — время действия (tu_use).
// Чего нет: оживление сразу (оглушённый встанет в начале своего хода, если оглушение меньше
// здоровья), статистика лечения; обезболивающее — без _moraleRestored (мораль + потерянное здоровье).
#include <stdint.h>
#include <stddef.h>
#include "far.h"
#include "res.h"
#include "res_ids.h"
#include "rules.h"
#include "str_ids.h"
#include "ui.h"
#include "ui_ids.h"
#include "scrdef.h"
#include "text.h"
#include "gfx.h"
#include "input.h"
#include "screens.h"
#include "loader.h"
#include "units.h"
#include "bitems.h"
#include "bfire.h"
#include "bhud.h"
#include "bmedi.h"

// Память банка 35 (с #BEB0; #BC00–#BEAC — bfire.c)
static uint8_t __at(0xBEB0) md_page;
static uint8_t __at(0xBEB1) md_actor;
static uint8_t __at(0xBEB2) md_target;
static uint8_t __at(0xBEB3) md_item;     // запись предмета
static uint8_t __at(0xBEB4) md_tu;
static uint8_t __at(0xBEB5) md_part;     // выбранная часть тела (0..5)

#define BODY_W 52
#define BODY_H 58

// MedikitState.cpp:122-148 (UFO) и interfaces.rul medikit (TFTD переставляет элементы)
static const wdef_t w_ufo[] = {
	IMG(0, 0, 320, 200, RES_MEDIBORD_PCK),          // фон (_bg: MEDIBORD.PCK), под текстами — его же
	CUS(95, 60, BODY_W, BODY_H, DYN(5), A_CUSTOM, 5),
	TXT(186, 37, 73, 9, UI_EL_TEXTPK, STR_PAIN_KILLER, TC),
	TXT(186, 73, 73, 9, UI_EL_TEXTSTIM, STR_STIMULANT, TC),
	TXT(186, 109, 73, 9, UI_EL_TEXTHEAL, STR_HEAL, TC),
	HOT(190, 48, 30, 20, A_CUSTOM, 1, 0),
	HOT(190, 84, 30, 20, A_CUSTOM, 2, 0),
	HOT(190, 120, 30, 20, A_CUSTOM, 3, 0),
	TXT(220, 52, 33, 17, UI_EL_NUMPK, DYN(2), TC | BIG),
	TXT(220, 88, 33, 17, UI_EL_NUMSTIM, DYN(3), TC | BIG),
	TXT(220, 124, 33, 17, UI_EL_NUMHEAL, DYN(4), TC | BIG),
	TXT(82, 120, 62, 9, UI_EL_TEXTPART, DYN(0), 0),
	TXT(145, 120, 14, 9, UI_EL_NUMWOUNDS, DYN(1), 0),
	HOT(220, 140, 20, 20, A_CUSTOM, 4, ESC),
};
static const wdef_t w_tftd[] = {
	IMG(0, 0, 320, 200, RES_MEDIBORD_PCK),
	CUS(90, 55, BODY_W, BODY_H, DYN(5), A_CUSTOM, 5),
	TXT(181, 33, 73, 9, UI_EL_TEXTPK, STR_PAIN_KILLER, TC),
	TXT(181, 69, 73, 9, UI_EL_TEXTSTIM, STR_STIMULANT, TC),
	TXT(181, 105, 73, 9, UI_EL_TEXTHEAL, STR_HEAL, TC),
	HOT(186, 44, 21, 21, A_CUSTOM, 1, 0),
	HOT(186, 80, 21, 21, A_CUSTOM, 2, 0),
	HOT(186, 116, 21, 21, A_CUSTOM, 3, 0),
	TXT(215, 48, 33, 17, UI_EL_NUMPK, DYN(2), TC | BIG),
	TXT(215, 84, 33, 17, UI_EL_NUMSTIM, DYN(3), TC | BIG),
	TXT(215, 120, 33, 17, UI_EL_NUMHEAL, DYN(4), TC | BIG),
	TXT(82, 116, 62, 9, UI_EL_TEXTPART, DYN(0), 0),
	TXT(145, 116, 14, 9, UI_EL_NUMWOUNDS, DYN(1), 0),
	HOT(292, 46, 28, 86, A_CUSTOM, 4, ESC),
};
static const scr_t tab_ufo = SCR(SCR_MEDIKIT, UI_SCR_MEDIKIT, RES_PAL_BATTLESCAPE, RES_MEDIBORD_PCK, SF_RAWPAL, w_ufo);
static const scr_t tab_tftd = SCR(SCR_MEDIKIT, UI_SCR_MEDIKIT, RES_PAL_BATTLESCAPE, RES_MEDIBORD_PCK, SF_RAWPAL, w_tftd);
static const uint16_t part_str[6] = { STR_HEAD, STR_TORSO, STR_RIGHT_ARM, STR_LEFT_ARM, STR_RIGHT_LEG, STR_LEFT_LEG };

static far_t u_at(uint8_t i) { return UNITS_FAR + (uint16_t)i * sizeof(unit_t); }
static far_t it_at(void) { return FAR(md_page + 1, TL_BITEMS) + (uint16_t)md_item * sizeof(bitem_t); }

static void wounds(uint8_t *w)
{
	if (md_target < MAX_UNITS) far_read(WOUNDS_AT(md_page, md_target), w, 6);
	else for (uint8_t i = 0; i < 6; i++) w[i] = 0;
}

// MedikitView::updateSelectedPart: первая раненая часть, иначе — прежняя
static void select_wounded(void)
{
	uint8_t w[6];
	wounds(w);
	for (uint8_t i = 0; i < 6; i++) if (w[i]) { md_part = i; return; }
}

// Заряды аптечки: [0] обезболивающее, [1] стимулятор, [2] лечение
static void charges(uint8_t *c, const r_items_t *r)
{
	bitem_t b;
	far_read(it_at(), &b, sizeof b);
	if (b.ammo == 0xFFFF) { c[0] = r->pain_killer; c[1] = r->stimulant; c[2] = r->heal; return; }
	c[0] = (uint8_t)(b.ammo >> 8); c[1] = (uint8_t)b.ammo; c[2] = b.rounds;
}

static void charges_put(const uint8_t *c)
{
	bitem_t b;
	far_read(it_at(), &b, sizeof b);
	b.ammo = ((uint16_t)c[0] << 8) | c[1]; b.rounds = c[2];
	far_write(it_at(), &b, sizeof b);
}

static uint8_t item_rule(r_items_t *r)
{
	rtab_t t;
	bitem_t b;
	far_read(it_at(), &b, sizeof b);
	if (!rtab_open(RES_RULE_ITEMS, &t) || b.item >= t.n) return 0;
	rtab_get(&t, b.item, r);
	return 1;
}

uint8_t md_open(uint8_t page, uint8_t actor, uint8_t hand, uint8_t tu, uint8_t n) __banked
{
	bitem_t b;
	r_items_t r;
	uint16_t k = bi_hand(page, actor, hand, &b);
	if (!k) return 0;
	md_page = page; md_actor = actor; md_item = (uint8_t)(k - 1); md_tu = tu;
	if (!item_rule(&r) || r.battle_type != BT_MEDIKIT) return 0;
	static const int8_t dx[8] = { 0, 1, 1, 1, 0, -1, -1, -1 }, dy[8] = { -1, -1, 0, 1, 1, 1, 0, -1 };
	unit_t a, u;
	far_read(u_at(actor), &a, sizeof a);
	uint8_t tx = (uint8_t)(a.x + dx[a.dir & 7]), ty = (uint8_t)(a.y + dy[a.dir & 7]);
	md_target = actor;                       // клетка перед лечащим: живой или оглушённый, иначе — он сам
	for (uint8_t i = 0; i < n; i++) {
		far_read(u_at(i), &u, sizeof u);
		if (i != actor && (u.alive || u.hp) && u.z == a.z && u.x == tx && u.y == ty) { md_target = i; break; }
	}
	md_part = 0;
	select_wounded();
	return 1;
}

uint8_t md_get(sdef_t *s, wdef_t *w) __banked
{
	return scr_find(res_game() == 2 ? &tab_tftd : &tab_ufo, 1, SCR_MEDIKIT, s, w);
}

void md_text(uint8_t slot, char *buf) __banked
{
	buf[0] = 0;
	if (slot == 0) { str_copy(part_str[md_part], buf, 32); return; }
	uint8_t w[6];
	if (slot == 1) { wounds(w); fmt_num(buf, w[md_part], 0); return; }
	r_items_t r;
	uint8_t c[3];
	if (slot >= 2 && slot <= 4 && item_rule(&r)) { charges(c, &r); fmt_num(buf, c[slot - 2], 0); }
}

// MedikitView::draw: части тела — кадр p (целая) или p + 6 (раненая)
static void body_draw(void)
{
	uint8_t w[6];
	int16_t x = res_game() == 2 ? 90 : 95, y = res_game() == 2 ? 55 : 60;
	wounds(w);
	for (uint8_t p = 0; p < 6; p++) gfx_sprite(RES_MEDIBITS_DAT, w[p] ? p + 6 : p, x, y);
}

// Действия (onPainKillerClick / onStimulantClick / onHealClick): заряд есть — время; не хватило — «мало
// времени» и окно закрывается. Вернёт 1 — перерисовать
static uint8_t act(uint8_t k, uint8_t *close)
{
	r_items_t r;
	uint8_t c[3];
	if (!item_rule(&r)) return 0;
	charges(c, &r);
	uint8_t i = k == 1 ? 0 : k == 2 ? 1 : 2;
	if (!c[i]) return 0;
	unit_t a, t;
	far_read(u_at(md_actor), &a, sizeof a);
	if (a.tu < md_tu) { hud_warn(STR_NOT_ENOUGH_TIME_UNITS); *close = 1; return 0; }
	a.tu -= md_tu;
	far_write(u_at(md_actor), &a, sizeof a);
	far_read(u_at(md_target), &t, sizeof t);
	if (k == 1) {                            // BattleUnit::painKillers: мораль + потерянное здоровье
		uint16_t m = t.mor + (t.hp_max > t.hp ? t.hp_max - t.hp : 0);
		t.mor = m > 100 ? 100 : (uint8_t)m;
	} else if (k == 2) {                     // stimulant: энергия, оглушение
		uint16_t e = t.en + r.energy_recovery;
		t.en = e > t.en_max ? t.en_max : (uint8_t)e;
		t.stun = r.stun_recovery >= t.stun ? 0 : t.stun - r.stun_recovery;
	} else {                                 // heal: раны выбранной части, здоровье
		if (md_target < MAX_UNITS) {
			far_t wa = WOUNDS_AT(md_page, md_target) + md_part;
			uint8_t wv = far_byte(wa);
			wv = r.wound_recovery >= wv ? 0 : wv - r.wound_recovery;
			far_write(wa, &wv, 1);
		}
		uint16_t h = t.hp + r.health_recovery;
		t.hp = h > t.hp_max ? t.hp_max : (uint8_t)h;
		select_wounded();                    // updateSelectedPart
	}
	far_write(u_at(md_target), &t, sizeof t);
	c[i]--;
	charges_put(c);
	return 1;
}

uint8_t md_event(uint8_t ev, uint8_t arg) __banked
{
	if (ev == EVT_DRAW) { body_draw(); return 0; }
	if (ev == EVT_KEY && arg == KEY_ESC) { UI_GO(A_POP, 0); return 0; }
	if (ev != EVT_BUTTON) return 0;
	if (ui_arrow_max || arg == 4) { UI_GO(A_POP, 0); return 0; }   // правая кнопка или «конец»
	if (arg == 5) {                          // MedikitView::mouseClick: часть под точкой (MEDIMAP)
		res_t m;
		int16_t x = cursor_x - (res_game() == 2 ? 90 : 95), y = cursor_y - (res_game() == 2 ? 55 : 60);
		if (x < 0 || y < 0 || x >= BODY_W || y >= BODY_H || !res_find(RES_MEDIMAP, &m)) return 0;
		uint8_t p = far_byte(m.phys + (uint16_t)y * BODY_W + x);
		if (p < 6) { md_part = p; ui_dirty(0); ui_dirty(1); }
		return 0;
	}
	uint8_t close = 0;
	if (act(arg, &close)) for (uint8_t s = 0; s < 6; s++) ui_dirty(s);   // числа, часть, тело
	if (close) UI_GO(A_POP, 0);
	return 0;
}
