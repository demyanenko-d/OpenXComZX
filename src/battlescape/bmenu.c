// Меню действий с предметом в руке (банк 33): ActionMenuState (REF/OpenXcom/src/Battlescape/
// ActionMenuState.cpp) и ActionMenuItem (ActionMenuItem.cpp). Пункт — 272x40 в (24, 160 - 40 * i)
// (от (x + 24, y - id * 40), y = панель + 16): рамка Frame толщиной 8 с контрастом (цвета элемента
// actionMenu: border — средняя линия, color2 — фон, под курсором фон темнее на 3, у TFTD — на 12),
// крупно: название в (10, 13) 200x20, «Acc>n%» в (140, 13), «TU>n» в (210, 13).
// Состав: бросить (если не встроенное), подготовить гранату, очередь / навскидку / прицельный
// (или пуск ракеты), удар, аптечка, сканер, пси — как в оригинале. Меткость — getFiringAccuracy
// (меткость бойца × оружия, на колене +15 %, двуручное при занятых руках -20 %, × поправка на
// здоровье getAccuracyModifier), бросок — getThrowingAccuracy; цена — getActionTUs (процент от
// времени бойца, у flatRate — как есть; бросок 25 %, подготовка 50 %).
#include <stdint.h>
#include <string.h>
#include "far.h"
#include "res.h"
#include "rules.h"
#include "str_ids.h"
#include "ui.h"
#include "ui_ids.h"
#include "gfx.h"
#include "text.h"
#include "bitems.h"
#include "bmenu.h"

#define AM_MAX 6
typedef struct {
	uint8_t n;
	uint8_t act[AM_MAX], tu[AM_MAX];
	int16_t acc[AM_MAX];       // -1 — без меткости (подготовить гранату, использовать)
	uint16_t name[AM_MAX];
} amenu_t;
static amenu_t __at(0xB000) am;          // память банка 33 (build.ps1 ram)

#define DT_STUN 6
#define BT_SCANNER 7
#define BT_MINDPROBE 8
#define BT_PSIAMP 9

static const amreq_t *rq;
static r_items_t __at(0xB040) wr;        // правило предмета в руке
static uint8_t both;                     // заняты обе руки (двуручное — -20 %)

static int16_t acc_mod(void)             // getAccuracyModifier: ранений пока нет
{
	int16_t m = rq->hp_max ? 25 * (int16_t)rq->hp / rq->hp_max + 75 : 100;
	return m < 10 ? 10 : m;
}

static void add(uint8_t ba, uint16_t name)
{
	if (am.n >= AM_MAX) return;
	uint8_t i = am.n++;
	am.act[i] = ba; am.name[i] = name;
	int16_t acc = -1;
	uint16_t cost = 0;
	switch (ba) {
	case BA_PRIME: cost = 50; break;
	case BA_THROW: cost = 25; acc = (int16_t)((uint16_t)rq->throwing * acc_mod() / 100); break;
	case BA_AUTOSHOT: cost = wr.tu_auto; break;
	case BA_SNAPSHOT: cost = wr.tu_snap; break;
	case BA_HIT: cost = wr.tu_melee; break;
	case BA_LAUNCH: case BA_AIMEDSHOT: cost = wr.tu_aimed; break;
	default: cost = wr.tu_use; break;
	}
	if (!(wr.flags & ITEMS_F_FLAT_RATE) || ba == BA_THROW || ba == BA_PRIME)
		cost = (uint16_t)rq->tu_max * cost / 100;
	if (ba == BA_AUTOSHOT || ba == BA_SNAPSHOT || ba == BA_AIMEDSHOT || ba == BA_LAUNCH) {
		uint8_t w = ba == BA_AUTOSHOT ? wr.accuracy_auto : ba == BA_SNAPSHOT ? wr.accuracy_snap : wr.accuracy_aimed;
		int16_t r = (int16_t)((uint16_t)rq->firing * w / 100);
		if (rq->kneel) r = r * 115 / 100;
		if ((wr.flags & ITEMS_F_TWO_HANDED) && both) r = r * 80 / 100;
		acc = (int16_t)((int32_t)r * acc_mod() / 100);
	} else if (ba == BA_HIT) {
		acc = (wr.flags & ITEMS_F_SKILL_APPLIED)
			? (int16_t)((int32_t)((uint16_t)rq->melee * wr.accuracy_melee / 100) * acc_mod() / 100)
			: (int16_t)((int32_t)wr.accuracy_melee * acc_mod() / 100);
	}
	am.acc[i] = acc;
	am.tu[i] = cost > 255 ? 255 : (uint8_t)cost;
}

uint8_t am_build(const amreq_t *r) __banked
{
	bitem_t b, o;
	rtab_t t;
	am.n = 0;
	rq = r;
	if (!bi_hand(r->page, r->unit, r->right, &b) || !rtab_open(RES_RULE_ITEMS, &t) || b.item >= t.n) return 0;
	rtab_get(&t, b.item, &wr);
	both = bi_hand(r->page, r->unit, !r->right, &o);
	if (!(wr.flags & ITEMS_F_FIXED_WEAPON)) add(BA_THROW, STR_THROW);
	if (wr.battle_type == BT_GRENADE || wr.battle_type == BT_PROXIMITYGRENADE)
		add(BA_PRIME, STR_PRIME_GRENADE);          // подготовленных гранат пока нет (fuseTimer)
	if (wr.battle_type == BT_FIREARM) {
		uint8_t wp = wr.waypoints;
		if (!wp && b.ammo != 0xFFFF) {             // у ракетницы путевые точки — у снаряда
			r_items_t a;
			rtab_get(&t, b.ammo, &a);
			wp = a.waypoints;
		}
		if (wp) add(BA_LAUNCH, STR_LAUNCH_MISSILE);
		else {
			rtab_get(&t, b.item, &wr);
			if (wr.accuracy_auto) add(BA_AUTOSHOT, STR_AUTO_SHOT);
			if (wr.accuracy_snap) add(BA_SNAPSHOT, STR_SNAP_SHOT);
			if (wr.accuracy_aimed) add(BA_AIMEDSHOT, STR_AIMED_SHOT);
		}
	}
	if (wr.tu_melee) add(BA_HIT, wr.battle_type == BT_MELEE && wr.damage_type == DT_STUN ? STR_STUN : STR_HIT_MELEE);
	else if (wr.battle_type == BT_MEDIKIT) add(BA_USE, STR_USE_MEDI_KIT);
	else if (wr.battle_type == BT_SCANNER) add(BA_USE, STR_USE_SCANNER);
	else if (wr.battle_type == BT_PSIAMP && r->psi_skill) { add(BA_MINDCONTROL, STR_MIND_CONTROL); add(BA_PANIC, STR_PANIC_UNIT); }
	else if (wr.battle_type == BT_MINDPROBE) add(BA_USE, STR_USE_MIND_PROBE);
	return am.n;
}

// Frame::draw: толщина 8, контраст (шаг 2): от средней линии border + 9 к темнее и обратно, крайние
// линии — самый тёмный цвет блока; внутри — фон
static void frame(int16_t x, int16_t y, uint8_t border, uint8_t bg)
{
	int16_t w = 272, h = 40;
	uint8_t c = border + 9, dark = (uint8_t)((border & 0xF0) + 15);
	for (uint8_t i = 0; i < 8; i++) {
		uint8_t k = (i == 0 || i == 7) ? dark : c;
		gfx_fill(x, y, w, 1, k);
		gfx_fill(x, y + h - 1, w, 1, k);
		gfx_fill(x, y, 1, h, k);
		gfx_fill(x + w - 1, y, 1, h, k);
		if (i < 4) c -= 2; else c += 2;
		x++; y++; w -= 2; h -= 2;
	}
	gfx_fill(x, y, w, h, bg);
}

static void item(uint8_t i, uint8_t lit)
{
	uint8_t ui = UI_SCR_BATTLESCAPE;
	uint8_t tc = ui_color_in(ui, UI_EL_ACTIONMENU, 0), bg = ui_color_in(ui, UI_EL_ACTIONMENU, 1);
	uint8_t bd = ui_color_in(ui, UI_EL_ACTIONMENU, 2), mod = res_game() == 2 ? 12 : 3;
	char s[24], n[8];
	{
		int16_t y = 160 - 40 * (int16_t)i;
		if (i >= am.n || y < 0) return;
		frame(24, y, bd, lit ? (uint8_t)(bg - mod) : bg);
		tbox_t tb = { 34, y + 13, 200, 20, FNT_BIG, tc, tc, TX_CONTRAST };
		text_draw(&tb, str_get(am.name[i]));
		if (am.acc[i] >= 0) {
			fmt_num(n, am.acc[i], 0);
			strcat(n, "%");
			str_fmt(s, str_get(STR_ACCURACY_SHORT), n, "");
			tb.x = 164; tb.w = 100;
			text_draw(&tb, s);
		}
		fmt_num(n, am.tu[i], 0);
		str_fmt(s, str_get(STR_TIME_UNITS_SHORT), n, "");
		tb.x = 234; tb.w = 80;
		text_draw(&tb, s);
	}
}

void am_draw(uint8_t hl) __banked
{
	for (uint8_t i = 0; i < am.n; i++) item(i, i == hl);
}

// Курсор перешёл с пункта old на new (ActionMenuItem::mouseIn / mouseOut): только эти два
void am_hover(uint8_t old, uint8_t now) __banked
{
	if (old != 0xFF) item(old, 0);
	if (now != 0xFF) item(now, 1);
}

uint8_t am_hit(int16_t x, int16_t y) __banked
{
	if (x < 24 || x >= 24 + 272) return 0xFF;
	for (uint8_t i = 0; i < am.n; i++) {
		int16_t top = 160 - 40 * (int16_t)i;
		if (y >= top && y < top + 40) return i;
	}
	return 0xFF;
}

uint8_t am_action(uint8_t i, uint8_t *tu) __banked
{
	if (i >= am.n) return BA_NONE;
	*tu = am.tu[i];
	return am.act[i];
}
