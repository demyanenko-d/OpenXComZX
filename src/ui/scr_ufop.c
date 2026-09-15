// Банк 13: статьи Уфопедии (ArticleState* OpenXcom, все 17 типов: UFO 1..9,
// TFTD 10..17). Раскладки, цвета и палитры — tmp/screens_ufopaedia.json: статьи не
// пользуются interfaces.rul — палитра явная (SF_RAWPAL), цвета числами (ui_raw).
// Картинки UP*.SPK/BDY, MAN_*.SPK, BIGOBS — с SD (sdres.c). Навигация «<< >>» —
// по всем видимым статьям подряд с переходом через конец (Ufopaedia::next/prev).
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

static uint8_t art;                     // запись таблицы ufopaedia
static r_ufopaedia_t ad;
static char t1[48], t2[48];

// Слоты DYN
#define D_TITLE 0
#define D_INFO  1
#define D_STATS 2
#define D_LIST  3
#define D_LIST2 4
#define D_AMMOT 10                      // 10..12 — тип урона слота
#define D_AMMOD 13                      // 13..15 — сила
#define D_PREV  40
#define D_NEXT  41

// Явные цвета (ui_raw): кнопки, заголовок, текст (+ значения), урон боеприпаса
#define C_BTN   ((uint8_t)(EL_RAW + 0))
#define C_TITLE ((uint8_t)(EL_RAW + 1))
#define C_BODY  ((uint8_t)(EL_RAW + 2))
#define C_DMG   ((uint8_t)(EL_RAW + 3))

// ---------------------------------------------------------------- видимость

static uint8_t is_done(uint16_t t) { return t < RES_BITS * 8 && ((ST->discovered[t >> 3] >> (t & 7)) & 1); }

static uint8_t reqs_done(const rtab_t *t, const rlist_t *l)
{
	uint16_t v[16];
	uint8_t n = l->n > 16 ? 16 : l->n;
	if (n) rtab_tail(t, l->off, v, n * 2);
	for (uint8_t i = 0; i < n; i++) if (!is_done(v[i])) return 0;
	return 1;
}

// Ufopaedia::isArticleAvailable + раздел не STR_NOT_AVAILABLE
uint8_t ufop_visible(uint8_t i) __banked
{
	rtab_t t;
	r_ufopaedia_t a;
	if (!rtab_open(RES_RULE_UFOPAEDIA, &t) || i >= t.n) return 0;
	rtab_get(&t, i, &a);
	return a.section != STR_NOT_AVAILABLE && reqs_done(&t, &a.requires);
}

// Ufopaedia::getArticleIndex для темы исследования: статья с тем же названием,
// иначе первая видимая, у которой тема в requires. NONE8 — нет.
uint8_t ufop_find_topic(uint8_t topic) __banked
{
	rtab_t t, tr;
	r_ufopaedia_t a;
	uint16_t v[16];
	rtab_open(RES_RULE_RESEARCH, &tr);
	uint16_t lk = rtab_word(&tr, topic, offsetof(r_research_t, lookup));
	if (lk < tr.n) topic = (uint8_t)lk;
	uint16_t name = rtab_word(&tr, topic, 0);
	rtab_open(RES_RULE_UFOPAEDIA, &t);
	for (uint8_t i = 0; i < t.n; i++) if (rtab_word(&t, i, 0) == name && ufop_visible(i)) return i;
	for (uint8_t i = 0; i < t.n; i++) {
		rtab_get(&t, i, &a);
		uint8_t n = a.requires.n > 16 ? 16 : a.requires.n;
		if (n) rtab_tail(&t, a.requires.off, v, n * 2);
		for (uint8_t k = 0; k < n; k++) if (v[k] == topic && ufop_visible(i)) return i;
	}
	return NONE8;
}

void ufop_open(uint8_t rec) __banked { art = rec; }

// ---------------------------------------------------------------- построение экрана

static const wdef_t w_art[] = {
	HOT(0, 0, 0, 0, A_POP, 0, ESC),             // keyCancel
};

static const scr_t tab[] = {
	SCR(SCR_ARTICLE, 0xFF, RES_PAL_UFOPAEDIA, 0, SF_RAWPAL, w_art),
};

static sdef_t *S_;
static wdef_t *W_;
static uint8_t info_skip;                // прокрутка описания: строк сверху скрыто
uint8_t ufop_next = 0xFF;                // ResearchComplete «Отчёты»: статья бонуса под статьёй темы

static wdef_t *add(uint8_t type, int16_t x, int16_t y, int16_t w, int16_t h, uint8_t el, uint16_t str, uint8_t fl)
{
	if (S_->n >= SDEF_MAXW) return &W_[SDEF_MAXW - 1];
	wdef_t *b = &W_[S_->n++];
	b->type = type; b->x = x; b->y = y; b->w = w; b->h = h;
	b->el = el; b->str = str; b->flags = fl; b->act = 0; b->arg = 0; b->key = 0;
	return b;
}

static void img(uint16_t res, uint8_t key)
{
	add(W_IMAGE, 0, 0, 320, 200, 0xFF, res, key);
}

static void buttons(int16_t x, int16_t y, int16_t w, int16_t h, int16_t step)
{
	wdef_t *b = add(W_BUTTON, x, y, w, h, C_BTN, STR_OK, 0);
	b->act = A_POP; b->key = ENT;
	b = add(W_BUTTON, x + step, y, w, h, C_BTN, DYN(D_PREV), 0);
	b->act = A_CUSTOM; b->arg = 1;
	b = add(W_BUTTON, x + 2 * step, y, w, h, C_BTN, DYN(D_NEXT), 0);
	b->act = A_CUSTOM; b->arg = 2;
}

static void colors(uint8_t btn, uint8_t title, uint8_t body, uint8_t val, uint8_t dmg)
{
	ui_raw[0][0] = ui_raw[0][1] = btn;
	ui_raw[1][0] = ui_raw[1][1] = title;
	ui_raw[2][0] = body; ui_raw[2][1] = val;
	ui_raw[3][0] = ui_raw[3][1] = dmg;
}

static uint16_t rule_word(uint16_t table, uint8_t off)
{
	rtab_t t;
	rtab_open(table, &t);
	return rtab_word(&t, ad.rule, off);
}

// Предмет и его первый боеприпас для статьи о технике (предмет с тем же названием, что юнит)
static uint8_t vehicle_item(void)
{
	rtab_t tu, ti;
	rtab_open(RES_RULE_UNITS, &tu);
	uint16_t nm = rtab_word(&tu, ad.rule, 0);
	rtab_open(RES_RULE_ITEMS, &ti);
	for (uint8_t i = 0; i < ti.n; i++) if (rtab_word(&ti, i, 0) == nm) return i;
	return NONE8;
}

static uint8_t item_ammo(uint8_t it, uint16_t *out, uint8_t max)
{
	rtab_t t;
	rlist_t l;
	rtab_open(RES_RULE_ITEMS, &t);
	far_read(t.base + 8 + (uint32_t)it * t.size + offsetof(r_items_t, compatible_ammo), &l, sizeof l);
	uint8_t n = l.n > max ? max : l.n;
	if (n) rtab_tail(&t, l.off, out, n * 2);
	return n;
}

// Статья о боеприпасе доступна (только requires, раздел не важен)
static uint8_t ammo_known(uint16_t ammo)
{
	rtab_t t;
	r_ufopaedia_t a;
	rtab_open(RES_RULE_UFOPAEDIA, &t);
	for (uint8_t i = 0; i < t.n; i++) {
		rtab_get(&t, i, &a);
		if ((a.type == 4 || a.type == 14) && a.rule == ammo) return reqs_done(&t, &a.requires);
	}
	return 1;
}

#define BT_FIREARM 1
#define BT_AMMO    2
#define BT_MELEE   3
#define BT_GREN    4
#define BT_PROX    5

static uint8_t ammo_slots(uint8_t *bt)      // сколько слотов урона/боеприпасов показывать
{
	uint8_t it = (uint8_t)ad.rule;
	*bt = (uint8_t)rule_word(RES_RULE_ITEMS, offsetof(r_items_t, battle_type));
	if (*bt == BT_FIREARM) {
		uint16_t am[3];
		uint8_t n = item_ammo(it, am, 3);
		return n ? n : 1;
	}
	return (*bt == BT_AMMO || *bt == BT_GREN || *bt == BT_PROX || *bt == BT_MELEE) ? 1 : 0;
}

// Текст слота урона: тип урона и сила (пустой, если боеприпас не исследован)
static void ammo_text(uint8_t i, uint8_t power, char *buf)
{
	static const uint16_t dmg_str[10] = {
		STR_UNKNOWN, STR_DAMAGE_ARMOR_PIERCING, STR_DAMAGE_INCENDIARY, STR_DAMAGE_HIGH_EXPLOSIVE, STR_DAMAGE_LASER_BEAM,
		STR_DAMAGE_PLASMA_BEAM, STR_DAMAGE_STUN, STR_DAMAGE_MELEE, STR_DAMAGE_ACID, STR_DAMAGE_SMOKE,
	};
	rtab_t t;
	r_items_t r;
	uint8_t bt, it = (uint8_t)ad.rule;
	rtab_open(RES_RULE_ITEMS, &t);
	rtab_get(&t, it, &r);
	bt = r.battle_type;
	if (bt == BT_FIREARM) {
		uint16_t am[3];
		uint8_t n = item_ammo(it, am, 3);
		if (n) {
			if (i >= n || am[i] >= t.n || !ammo_known(am[i])) return;
			rtab_get(&t, (uint8_t)am[i], &r);
		} else if (i) return;
	} else if (i) return;
	if (!power) { str_copy(dmg_str[r.damage_type < 10 ? r.damage_type : 0], buf, 40); return; }
	fmt_num(buf, r.power, 0);
	if (r.shotgun_pellets && (ad.type < 10 || bt == BT_FIREARM)) { strcat(buf, "x"); fmt_num(buf + strlen(buf), r.shotgun_pellets, 0); }
}

uint8_t ufop_get(uint8_t id, sdef_t *s, wdef_t *w) __banked
{
	rtab_t t;
	if (!scr_find(tab, sizeof tab / sizeof tab[0], id, s, w)) return 0;
	S_ = s; W_ = w;
	info_skip = 0;
	rtab_open(RES_RULE_UFOPAEDIA, &t);
	rtab_get(&t, art, &ad);
	uint8_t ty = ad.type;
	if (ty >= 10) {                              // TFTD: палитра базы, всё цветом 2
		s->palui = RES_PAL_BASESCAPE;
		colors(2, 2, 2, 244, 54);
		img(RES_BACK08_SCR, 0);
		if (ad.image) img(ad.image, 1);
		buttons(227, 179, 23, 10, 27);
		add(W_TEXT, 36, 14, 284, 16, C_TITLE, DYN(D_TITLE), BIG | TC | TW);
		int16_t ih = 136;
		switch (ty) {
		case 11: ih = 80; break;
		case 12: ih = 88; break;
		case 13: case 15: ih = 72; break;
		case 16: ih = rule_word(RES_RULE_FACILITIES, offsetof(r_facilities_t, defense)) ? 96 : 112; break;
		case 17: ih = 112; break;
		case 14: { uint8_t bt; if (ammo_slots(&bt)) { char b[48]; b[0] = 0; ammo_text(0, 0, b); if (b[0]) ih = 112; } break; }
		}
		add(W_TEXT, 320 - (int16_t)ad.text_width, 34, (int16_t)ad.text_width, ih, C_BODY, DYN(D_INFO), TW);
		switch (ty) {
		case 11: add(W_TEXT, 187, 116, 131, 56, C_BODY, DYN(D_STATS), 0); break;
		case 12: add(W_LIST, 168, 126, 150, 50, C_BODY, DYN(D_LIST), 0); break;
		case 13:
			add(W_LIST, 168, 106, 150, 65, C_BODY, DYN(D_LIST), 0);
			add(W_LIST, 25, 166, 195, 33, C_BODY, DYN(D_LIST2), 0);
			break;
		case 14: {
			uint8_t bt, n = ammo_slots(&bt);
			if (bt == BT_FIREARM) {
				add(W_TEXT, 8, 157, 53, 17, C_BODY, STR_SHOT_TYPE, TW);
				add(W_TEXT, 61, 157, 57, 17, C_BODY, STR_ACCURACY_UC, TW);
				add(W_TEXT, 118, 157, 56, 17, C_BODY, STR_TIME_UNIT_COST, TW);
				add(W_LIST, 8, 170, 140, 30, C_BODY, DYN(D_LIST), 0);
			}
			for (uint8_t i = 0; i < n; i++) {
				add(W_TEXT, 168, 144 + i * 10, 120, 9, C_BODY, DYN(D_AMMOT + i), TW);
				add(W_TEXT, 300, 144 + i * 10, 20, 9, C_DMG, DYN(D_AMMOD + i), 0);
			}
			break;
		}
		case 15: add(W_LIST, 168, 110, 150, 64, C_BODY, DYN(D_LIST), 0); break;
		case 16: {
			uint8_t d = rule_word(RES_RULE_FACILITIES, offsetof(r_facilities_t, defense)) != 0;
			add(W_LIST, 168, d ? 134 : 150, 150, 50, C_BODY, DYN(D_LIST), 0);
			break;
		}
		case 17: add(W_LIST, 168, 142, 150, 50, C_BODY, DYN(D_LIST), 0); break;
		}
		return 1;
	}
	// UFO: палитра и цвета по классу статьи
	switch (ty) {
	case 1:                                      // корабль: картинка, раскладка из rect_stats / rect_text
		colors(239, 239, 239, 244, 0);
		img(ad.image, 0);
		buttons(5, 5, 30, 14, 35);
		add(W_TEXT, 5, 24, 210, 32, C_TITLE, DYN(D_TITLE), BIG | TW);
		{
			int16_t r[4] = { 0, 0, 0, 0 }, q[4] = { 0, 0, 0, 0 };
			if (ad.rect_text.n >= 4) rtab_tail(&t, ad.rect_text.off, r, 8);
			if (ad.rect_stats.n >= 4) rtab_tail(&t, ad.rect_stats.off, q, 8);
			add(W_TEXT, r[0], r[1], r[2], r[3], C_BODY, DYN(D_INFO), TW);
			add(W_TEXT, q[0], q[1], q[2], q[3], C_BODY, DYN(D_STATS), 0);
		}
		break;
	case 2:
		s->palui = RES_PAL_BATTLEPEDIA;
		colors(16, 239, 239, 244, 0);
		img(ad.image, 0);
		buttons(5, 5, 30, 14, 35);
		add(W_TEXT, 5, 24, 200, 32, C_TITLE, DYN(D_TITLE), BIG | TW);
		add(W_TEXT, 5, 160, 310, 32, C_BODY, DYN(D_INFO), TW);
		add(W_LIST, 5, 80, 250, 111, C_BODY, DYN(D_LIST), BIG);
		break;
	case 3: {                                    // заголовок и список — 244, текст — 239
		colors(80, 244, 239, 239, 0);
		ui_raw[4][0] = ui_raw[4][1] = 244;
		img(RES_BACK10_SCR, 0);
		buttons(5, 5, 30, 14, 35);
		add(W_TEXT, 5, 23, 310, 17, C_TITLE, DYN(D_TITLE), BIG);
		uint8_t it = vehicle_item();
		uint16_t am[1];
		uint8_t ammo = it != NONE8 && item_ammo(it, am, 1);
		add(W_TEXT, 10, ammo ? 138 : 122, 300, 150, C_BODY, DYN(D_INFO), TW);
		add(W_LIST, 10, 48, 300, 89, (uint8_t)(EL_RAW + 4), DYN(D_LIST), 0);
		break;
	}
	case 4: {
		s->palui = RES_PAL_BATTLEPEDIA;
		colors(144, 239, 239, 244, 32);
		img(RES_BACK08_SCR, 0);
		buttons(5, 5, 30, 14, 35);
		add(W_TEXT, 5, 24, 148, 32, C_TITLE, DYN(D_TITLE), BIG | TW);
		add(W_CUSTOM, 157, 5, 32, 48, 0xFF, NOSTR, 0);   // картинка предмета (BIGOBS)
		uint8_t bt, n = ammo_slots(&bt);
		uint16_t am[3];
		uint8_t na = bt == BT_FIREARM ? item_ammo((uint8_t)ad.rule, am, 3) : 0;
		if (bt == BT_FIREARM) {
			add(W_TEXT, 8, 66, 100, 17, C_BODY, STR_SHOT_TYPE, TW);
			add(W_TEXT, 104, 66, 50, 17, C_BODY, STR_ACCURACY_UC, TW);
			add(W_TEXT, 158, 66, 60, 17, C_BODY, STR_TIME_UNIT_COST, TW);
			add(W_LIST, 8, 82, 204, 55, C_BODY, DYN(D_LIST), BIG);
			add(W_TEXT, 8, 138, na < 3 ? 300 : 180, 56, C_BODY, DYN(D_INFO), TW);
		} else
			add(W_TEXT, 8, 67, 300, 125, C_BODY, DYN(D_INFO), TW);
		for (uint8_t i = 0; i < n; i++) {
			add(W_TEXT, 194, 20 + i * 49, 82, 16, C_BODY, DYN(D_AMMOT + i), TC | TW);
			add(W_TEXT, 194, 40 + i * 49, 82, 17, C_DMG, DYN(D_AMMOD + i), BIG | TC);
			if (bt == BT_FIREARM && i < na) add(W_CUSTOM, 280, 16 + i * 49, 32, 48, 0xFF, NOSTR, 0);
		}
		if (n) add(W_TEXT, 194, 7, 82, 10, C_BODY, STR_DAMAGE_UC, TC);
		if (bt == BT_FIREARM) add(W_TEXT, 268, 7, 50, 10, C_BODY, STR_AMMO, TC);
		break;
	}
	case 5:
		s->palui = RES_PAL_BATTLEPEDIA;
		colors(15, 239, 239, 244, 0);
		buttons(5, 5, 30, 14, 35);
		add(W_TEXT, 5, 24, 300, 17, C_TITLE, DYN(D_TITLE), BIG);
		if (ad.image) img(ad.image, 1);
		add(W_LIST, 150, 46, 150, 96, C_BODY, DYN(D_LIST), 0);
		add(W_TEXT, 8, 150, 300, 48, C_BODY, DYN(D_INFO), TW);
		break;
	case 6:
		s->palui = RES_PAL_BASESCAPE;
		colors(64, 218, 218, 208, 0);
		img(RES_BACK09_SCR, 0);
		buttons(5, 5, 30, 14, 35);
		add(W_TEXT, 10, 24, 200, 17, C_TITLE, DYN(D_TITLE), BIG);
		add(W_CUSTOM, 232, 16, 64, 64, 0xFF, NOSTR, 0);  // постройка из BASEBITS
		add(W_TEXT, 10, 104, 300, 90, C_BODY, DYN(D_INFO), TW);
		add(W_LIST, 10, 42, 200, 42, C_BODY, DYN(D_LIST), 0);
		break;
	case 7: {
		colors(83, 244, 239, 239, 0);
		img(ad.image, 0);
		buttons(5, 5, 30, 14, 35);
		tbox_t b;
		b.x = 5; b.y = 22; b.w = (int16_t)ad.text_width; b.h = 48; b.font = FNT_BIG; b.flags = TW;
		char tt[64];
		str_copy(ad.name, tt, sizeof tt);
		int16_t th = text_height(&b, tt);
		add(W_TEXT, 5, 22, (int16_t)ad.text_width, 48, C_TITLE, DYN(D_TITLE), BIG | TW);
		add(W_TEXT, 5, 23 + th, (int16_t)ad.text_width, 176 - th, C_BODY, DYN(D_INFO), TW);
		break;
	}
	case 8:
		colors(80, 244, 239, 239, 0);
		img(RES_BACK10_SCR, 0);
		buttons(5, 5, 30, 14, 35);
		add(W_TEXT, 5, 23, 296, 17, C_TITLE, DYN(D_TITLE), BIG);
		add(W_TEXT, 10, 48, 296, 150, C_BODY, DYN(D_INFO), TW);
		break;
	case 9:
		s->palui = RES_PAL_GEOSCAPE;
		colors(133, 133, 133, 133, 0);
		img(RES_BACK11_SCR, 0);
		buttons(5, 5, 30, 14, 35);
		add(W_TEXT, 5, 24, 155, 32, C_TITLE, DYN(D_TITLE), BIG | TW);
		add(W_CUSTOM, 160, 6, 160, 52, 0xFF, NOSTR, 0);  // НЛО из INTERWIN
		add(W_TEXT, 10, 140, 300, 50, C_BODY, DYN(D_INFO), TW);
		add(W_LIST, 10, 68, 310, 64, C_BODY, DYN(D_LIST), BIG);
		break;
	}
	return 1;
}

// ---------------------------------------------------------------- тексты

// Строка списка «метка.....\t{ALT}значение» (TextList::setDot)
static void dot_row(char *buf, uint16_t label, uint8_t colw, uint8_t font)
{
	str_copy(label, buf, 64);
	int16_t w = text_width(font, buf), dw = text_width(font, ".");
	uint8_t n = (uint8_t)strlen(buf);
	while (w < colw && n < 100) { buf[n++] = '.'; w += dw; }
	buf[n] = 0;
	strcat(buf, "\t\x01");
}

static void num_row(char *buf, uint16_t label, uint8_t colw, uint8_t font, int32_t v, char sep, const char *suffix)
{
	dot_row(buf, label, colw, font);
	fmt_num(buf + strlen(buf), v, sep);
	if (suffix) strcat(buf, suffix);
}

static void arg_row(char *buf, uint16_t label, uint8_t colw, uint8_t font, uint16_t pat, int32_t v, char sep)
{
	dot_row(buf, label, colw, font);
	fmt_num(t1, v, sep);
	str_fmt(buf + strlen(buf), str_get(pat), t1, "");
}

static const uint16_t stat_lbl[11] = {
	STR_TIME_UNITS, STR_STAMINA, STR_HEALTH, STR_BRAVERY, STR_REACTIONS, STR_FIRING_ACCURACY,
	STR_THROWING_ACCURACY, STR_STRENGTH, STR_PSIONIC_STRENGTH, STR_PSIONIC_SKILL, STR_MELEE_ACCURACY,
};
// порядок бонусов брони в статье: tu, stamina, health, bravery, reactions, firing, throwing, melee, strength, psiS, psiK
static const uint8_t stat_ord[11] = { 0, 1, 2, 3, 4, 5, 6, 10, 7, 8, 9 };
static const uint16_t dmg_lbl[10] = {
	STR_UNKNOWN, STR_DAMAGE_ARMOR_PIERCING, STR_DAMAGE_INCENDIARY, STR_DAMAGE_HIGH_EXPLOSIVE, STR_DAMAGE_LASER_BEAM,
	STR_DAMAGE_PLASMA_BEAM, STR_DAMAGE_STUN, STR_DAMAGE_MELEE, STR_DAMAGE_ACID, STR_DAMAGE_SMOKE,
};

// Строки брони (ArticleStateArmor): 5 брони, пусто, модификаторы != 100 %, пусто, бонусы
static uint8_t armor_row(uint8_t row, char *buf, uint8_t font, uint8_t colw)
{
	rtab_t t;
	r_armors_t a;
	int16_t md[10];
	uint8_t k = 0;
	rtab_open(RES_RULE_ARMORS, &t);
	rtab_get(&t, (uint8_t)ad.rule, &a);
	for (uint8_t i = 0; i < 10; i++) md[i] = 100;
	uint8_t nm = a.damage_modifier.n > 10 ? 10 : a.damage_modifier.n;
	if (nm) rtab_tail(&t, a.damage_modifier.off, md, nm * 2);
	static const uint16_t al[5] = { STR_FRONT_ARMOR, STR_LEFT_ARMOR, STR_RIGHT_ARMOR, STR_REAR_ARMOR, STR_UNDER_ARMOR };
	uint8_t av[5] = { a.front_armor, a.side_armor, a.side_armor, a.rear_armor, a.under_armor };
	for (uint8_t i = 0; i < 5; i++) {
		if (!av[i]) continue;
		if (k++ == row) { if (buf) num_row(buf, al[i], colw, font, av[i], 0, 0); return 1; }
	}
	if (k++ == row) { if (buf) buf[0] = 0; return 1; }
	for (uint8_t i = 1; i < 10; i++) {           // множитель * 100 — это и есть проценты
		if (md[i] == 100) continue;
		if (k++ == row) { if (buf) num_row(buf, dmg_lbl[i], colw, font, md[i], 0, "%"); return 1; }
	}
	if (k++ == row) { if (buf) buf[0] = 0; return 1; }
	uint8_t *st = (uint8_t *)&a.stats;
	for (uint8_t j = 0; j < 11; j++) {
		uint8_t v = st[stat_ord[j]];
		if (!v) continue;
		if (k++ == row) {
			if (buf) { dot_row(buf, stat_lbl[stat_ord[j]], colw, font); strcat(buf, "+"); fmt_num(buf + strlen(buf), v, 0); }
			return 1;
		}
	}
	return 0;
}

// Строки техники (ArticleStateVehicle): TU, здоровье, броня; оружие и боеприпас
static uint8_t vehicle_row(uint8_t list, uint8_t row, char *buf, uint8_t font, uint8_t colw)
{
	rtab_t tu, ta, ti;
	r_units_t u;
	rtab_open(RES_RULE_UNITS, &tu);
	rtab_get(&tu, (uint8_t)ad.rule, &u);
	rtab_open(RES_RULE_ARMORS, &ta);
	uint8_t arm[4] = { 0, 0, 0, 0 };
	if (u.armor < ta.n) {
		arm[0] = (uint8_t)rtab_word(&ta, u.armor, offsetof(r_armors_t, front_armor));
		arm[1] = (uint8_t)rtab_word(&ta, u.armor, offsetof(r_armors_t, side_armor));
		arm[2] = (uint8_t)rtab_word(&ta, u.armor, offsetof(r_armors_t, rear_armor));
		arm[3] = (uint8_t)rtab_word(&ta, u.armor, offsetof(r_armors_t, under_armor));
	}
	static const uint16_t lbl[7] = { STR_TIME_UNITS, STR_HEALTH, STR_FRONT_ARMOR, STR_LEFT_ARMOR, STR_RIGHT_ARMOR, STR_REAR_ARMOR, STR_UNDER_ARMOR };
	uint8_t val[7] = { u.stats.tu, u.stats.health, arm[0], arm[1], arm[1], arm[2], arm[3] };
	uint8_t k = 0;
	if (list == 0) {
		if (row < 7) { if (buf) num_row(buf, lbl[row], colw, font, val[row], 0, 0); return 1; }
		if (ad.type == 13) return 0;                 // TFTD: оружие — во втором списке
		k = 7;
	}
	uint8_t r = row - k;
	uint8_t it = vehicle_item();
	uint16_t am[1];
	uint8_t na = it != NONE8 ? item_ammo(it, am, 1) : 0;
	rtab_open(RES_RULE_ITEMS, &ti);
	if (r == 0) { if (buf) { dot_row(buf, STR_WEAPON, colw, font); str_copy(ad.weapon, buf + strlen(buf), 40); } return 1; }
	if (it == NONE8) return 0;
	if (r == 1) {
		uint16_t pw = rtab_word(&ti, na ? am[0] : it, offsetof(r_items_t, power));
		if (buf) num_row(buf, STR_WEAPON_POWER, colw, font, pw, 0, 0);
		return 1;
	}
	if (!na) return 0;
	if (r == 2) { if (buf) { dot_row(buf, STR_AMMUNITION, colw, font); str_copy(rtab_word(&ti, am[0], 0), buf + strlen(buf), 40); } return 1; }
	if (r == 3) {
		int16_t cs = (int16_t)rtab_word(&ti, it, offsetof(r_items_t, clip_size));
		if (cs <= 0) cs = (int16_t)rtab_word(&ti, am[0], offsetof(r_items_t, clip_size));
		if (buf) num_row(buf, STR_ROUNDS, colw, font, cs, 0, 0);
		return 1;
	}
	return 0;
}

// Строки списков статьи; buf == 0 — только проверить, есть ли строка
static uint8_t list_row(uint8_t slot, uint8_t row, char *buf)
{
	rtab_t t;
	uint8_t big = ad.type < 10 && (ad.type == 2 || ad.type == 4 || ad.type == 9);
	uint8_t font = big ? FNT_BIG : FNT_SMALL;
	switch (ad.type) {
	case 2: case 12: {                           // оружие корабля
		r_craftWeapons_t c;
		uint8_t cw = ad.type == 2 ? 180 : 100;
		rtab_open(RES_RULE_CRAFTWEAPONS, &t);
		rtab_get(&t, (uint8_t)ad.rule, &c);
		if (row > (ad.type == 2 ? 4 : 3)) return 0;
		if (!buf) return 1;
		switch (row) {
		case 0: num_row(buf, STR_DAMAGE, cw, font, c.damage, ',', 0); break;
		case 1: arg_row(buf, STR_RANGE, cw, font, STR_KILOMETERS, c.range, 0); break;
		case 2: num_row(buf, STR_ACCURACY, cw, font, c.accuracy, 0, "%"); break;
		case 3: arg_row(buf, STR_RE_LOAD_TIME, cw, font, STR_SECONDS, c.reload_standard, 0); break;
		case 4: num_row(buf, STR_ROUNDS, cw, font, c.ammo_max, ',', 0); break;
		}
		return 1;
	}
	case 3: case 13: return vehicle_row(slot == D_LIST2, row, buf, font, ad.type == 3 ? 175 : (slot == D_LIST2 ? 65 : 100));
	case 4: case 14: {                           // стрельба: авто, прицельный, точный (TU > 0)
		r_items_t it;
		rtab_open(RES_RULE_ITEMS, &t);
		rtab_get(&t, (uint8_t)ad.rule, &it);
		static const uint16_t sn[3] = { STR_SHOT_TYPE_AUTO, STR_SHOT_TYPE_SNAP, STR_SHOT_TYPE_AIMED };
		uint8_t acc[3] = { it.accuracy_auto, it.accuracy_snap, it.accuracy_aimed };
		uint8_t tu[3] = { it.tu_auto, it.tu_snap, it.tu_aimed };
		uint8_t k = 0;
		for (uint8_t i = 0; i < 3; i++) {
			if (!tu[i]) continue;
			if (k++ != row) continue;
			if (buf) {
				str_copy(sn[i], buf, 40);
				strcat(buf, "\t\x01");
				fmt_num(buf + strlen(buf), acc[i], 0);
				strcat(buf, "%\t\x01");
				fmt_num(buf + strlen(buf), tu[i], 0);
				if (!(it.flags & ITEMS_F_FLAT_RATE)) strcat(buf, "%");
			}
			return 1;
		}
		return 0;
	}
	case 5: case 15: return armor_row(row, buf, font, 125);
	case 6: case 16: {                           // постройка
		r_facilities_t f;
		uint8_t cw = ad.type == 6 ? 140 : 104;
		rtab_open(RES_RULE_FACILITIES, &t);
		rtab_get(&t, (uint8_t)ad.rule, &f);
		uint8_t r = row;
		if (ad.type == 16 && f.defense) {        // TFTD: оборона первой
			if (r < 2) goto defense;
			r -= 2;
		}
		if (r < 3) {
			if (!buf) return 1;
			if (r == 0) { dot_row(buf, STR_CONSTRUCTION_TIME, cw, font); str_plural(buf + strlen(buf), STR_DAY_one, f.build_time); }
			else if (r == 1) { dot_row(buf, STR_CONSTRUCTION_COST, cw, font); fmt_funds(buf + strlen(buf), (int32_t)f.build_cost); }
			else { dot_row(buf, STR_MAINTENANCE_COST, cw, font); fmt_funds(buf + strlen(buf), (int32_t)f.monthly_cost); }
			return 1;
		}
		if (ad.type == 16 || !f.defense) return 0;
		r -= 3;
	defense:
		if (r > 1) return 0;
		if (!buf) return 1;
		if (r == 0) num_row(buf, STR_DEFENSE_VALUE, cw, font, f.defense, 0, 0);
		else num_row(buf, STR_HIT_RATIO, cw, font, f.hit_ratio, 0, "%");
		return 1;
	}
	case 9: case 17: {                           // НЛО / USO
		r_ufos_t u;
		uint8_t cw = ad.type == 9 ? 200 : 95;
		rtab_open(RES_RULE_UFOS, &t);
		rtab_get(&t, (uint8_t)ad.rule, &u);
		if (row > 3) return 0;
		if (!buf) return 1;
		switch (row) {
		case 0: num_row(buf, STR_DAMAGE_CAPACITY, cw, font, u.damage_max, ',', 0); break;
		case 1: num_row(buf, STR_WEAPON_POWER, cw, font, u.power, ',', 0); break;
		case 2: arg_row(buf, STR_WEAPON_RANGE, cw, font, STR_KILOMETERS, u.range, 0); break;
		case 3: arg_row(buf, STR_MAXIMUM_SPEED, cw, font, STR_KNOTS, u.speed_max, ','); break;
		}
		return 1;
	}
	}
	return 0;
}

static void list_colw(uint8_t slot, char *buf)
{
	uint8_t cw[3], n = 2;
	switch (ad.type) {
	case 2: cw[0] = 180; cw[1] = 70; break;
	case 12: cw[0] = 100; cw[1] = 68; break;
	case 3: cw[0] = 175; cw[1] = 145; break;
	case 13: if (slot == D_LIST2) { cw[0] = 65; cw[1] = 130; } else { cw[0] = 100; cw[1] = 50; } break;
	case 4: n = 3; cw[0] = 100; cw[1] = 52; cw[2] = 52; break;
	case 14: n = 3; cw[0] = 70; cw[1] = 40; cw[2] = 30; break;
	case 5: case 15: cw[0] = 125; cw[1] = 25; break;
	case 6: cw[0] = 140; cw[1] = 60; break;
	case 16: cw[0] = 104; cw[1] = 46; break;
	case 9: cw[0] = 200; cw[1] = 110; break;
	default: cw[0] = 95; cw[1] = 55; break;      // 17
	}
	list_cols(buf, n, cw, 0);
}

// Характеристики корабля: строки «МЕТКА>{ALT}значение{ALT}» через перевод строки
static void craft_stats(char *buf)
{
	rtab_t t;
	r_crafts_t c;
	rtab_open(RES_RULE_CRAFTS, &t);
	rtab_get(&t, (uint8_t)ad.rule, &c);
	static const uint16_t lbl[7] = { STR_MAXIMUM_SPEED_UC, STR_ACCELERATION, STR_FUEL_CAPACITY, STR_WEAPON_PODS, STR_DAMAGE_CAPACITY_UC, STR_CARGO_SPACE, STR_HWP_CAPACITY };
	int32_t v[7] = { c.speed_max, c.accel, c.fuel_max, c.weapons, c.damage_max, c.soldiers, c.vehicles };
	static const uint8_t sep[7] = { ',', 0, ',', 0, ',', 0, 0 };
	buf[0] = 0;
	for (uint8_t i = 0; i < 7; i++) {
		if (i) strcat(buf, "\n");
		fmt_num(t1, v[i], sep[i]);
		str_fmt(buf + strlen(buf), str_get(lbl[i]), t1, "");
	}
}

void ufop_text(uint8_t id, uint8_t slot, uint8_t row, char *buf) __banked
{
	(void)id;
	switch (slot) {
	case D_TITLE: str_copy(ad.name, buf, 256); return;
	case D_INFO: tx_skip = info_skip; str_copy(ad.text, buf, 1024); return;   // прокрутка — у text_draw
	case D_STATS: craft_stats(buf); return;
	case D_PREV: strcpy(buf, "<<"); return;
	case D_NEXT: strcpy(buf, ">>"); return;
	case D_LIST: case D_LIST2:
		if (row == LIST_COLS) list_colw(slot, buf);
		else list_row(slot, row, buf);
		return;
	}
	if (slot >= D_AMMOT && slot < D_AMMOT + 3) ammo_text(slot - D_AMMOT, 0, buf);
	else if (slot >= D_AMMOD && slot < D_AMMOD + 3) ammo_text(slot - D_AMMOD, 1, buf);
}

uint8_t ufop_rows(uint8_t id, uint8_t slot) __banked
{
	(void)id;
	uint8_t n = 0;
	while (n < 20 && list_row(slot, n, 0)) n++;
	return n;
}

// Картинки W_CUSTOM: предмет и боеприпасы (BIGOBS), постройка (BASEBITS), НЛО (INTERWIN)
static void big_sprite(uint8_t it, int16_t x, int16_t y)
{
	rtab_t t;
	rtab_open(RES_RULE_ITEMS, &t);
	int16_t f = (int16_t)rtab_word(&t, it, offsetof(r_items_t, big_sprite));
	uint8_t iw = (uint8_t)rtab_word(&t, it, offsetof(r_items_t, inv_width)), ih = (uint8_t)rtab_word(&t, it, offsetof(r_items_t, inv_height));
	if (f < 0) return;
	gfx_sprite(RES_BIGOBS_PCK, f, x + (2 - iw) * 8, y + (3 - ih) * 8);
}

static void art_draw(uint8_t i)
{
	uint8_t k = 0;
	for (uint8_t j = 0; j < i; j++) if (W_[j].type == W_CUSTOM) k++;   // номер W_CUSTOM на экране
	const wdef_t *w = &W_[i];
	if (ad.type == 4) {
		if (!k) { big_sprite((uint8_t)ad.rule, w->x, w->y); return; }
		uint16_t am[3];
		uint8_t n = item_ammo((uint8_t)ad.rule, am, 3);
		if (k - 1 < n && ammo_known(am[k - 1])) big_sprite((uint8_t)am[k - 1], w->x, w->y);
	} else if (ad.type == 6) {
		r_facilities_t f;
		rtab_t t;
		rtab_open(RES_RULE_FACILITIES, &t);
		rtab_get(&t, (uint8_t)ad.rule, &f);
		if (f.size == 1) {
			gfx_sprite(RES_BASEBITS_PCK, f.sprite_shape, w->x + 16, w->y + 16);
			gfx_sprite(RES_BASEBITS_PCK, f.sprite_facility, w->x + 16, w->y + 16);
		} else {
			uint8_t num = 0;
			for (uint8_t y = 0; y < f.size; y++)
				for (uint8_t x = 0; x < f.size; x++, num++) gfx_sprite(RES_BASEBITS_PCK, f.sprite_shape + num, w->x + x * 32, w->y + y * 32);
		}
	} else if (ad.type == 9) {                    // рамка 15, верх окна перехвата, НЛО previewMid (y 140 + 52 * sprite)
		gfx_fill(w->x, w->y, w->w, w->h, 15);
		gfx_blit(RES_INTERWIN_DAT, 0, 0, w->x, w->y, 160, 52);
		int16_t sp = (int16_t)rule_word(RES_RULE_UFOS, offsetof(r_ufos_t, sprite));
		gfx_key = 1;
		gfx_blit(RES_INTERWIN_DAT, 0, 140 + 52 * sp, w->x, w->y, 160, 52);
		gfx_key = 0;
	}
}

// Следующая / предыдущая видимая статья с переходом через конец
static void step(int8_t dir)
{
	rtab_t t;
	rtab_open(RES_RULE_UFOPAEDIA, &t);
	uint8_t i = art;
	for (uint8_t n = 0; n < t.n; n++) {
		i = dir > 0 ? (i + 1 >= t.n ? 0 : i + 1) : (i ? i - 1 : t.n - 1);
		if (ufop_visible(i)) { art = i; return; }
	}
}

// Text::setScrollable (колесо мыши — по строке): у нас стрелки ZX вверх/вниз; текст
// описания длиннее рамки — до последней строки внизу рамки
static void info_scroll(int8_t dir)
{
	for (uint8_t i = 0; i < S_->n; i++) {
		const wdef_t *w = &W_[i];
		if (w->type != W_TEXT || w->str != DYN(D_INFO)) continue;
		tbox_t b;
		b.x = w->x; b.y = w->y; b.w = w->w; b.h = w->h;
		b.font = FNT_SMALL; b.color = b.color2 = 0; b.flags = TX_WRAP;
		uint8_t fh = font_height(FNT_SMALL);
		int16_t over = (text_height(&b, str_get(ad.text)) - w->h + fh - 1) / fh;   // строк за рамкой
		uint8_t s = info_skip, old = s;
		if (dir < 0 && s) s--;
		else if (dir > 0 && (int16_t)s < over) s++;
		if (s != old) { info_skip = s; ui_dirty(D_INFO); }
		return;
	}
}

uint8_t ufop_event(uint8_t id, uint8_t ev, uint8_t arg) __banked
{
	(void)id;
	if (ev == EVT_DRAW) art_draw(arg);
	else if (ev == EVT_BUTTON && (arg == 1 || arg == 2)) {
		step(arg == 1 ? -1 : 1);
		UI_GO(A_REDRAW, 0);
	} else if (ev == EVT_KEY && (arg == KEY_UP || arg == KEY_DOWN))
		info_scroll(arg == KEY_UP ? -1 : 1);
	else if (ev == EVT_CLOSE && ufop_next != 0xFF) {   // под статьёй темы — статья бонуса
		ufop_open(ufop_next);
		ufop_next = 0xFF;
		UI_GO(A_PUSH, SCR_ARTICLE);
	}
	return 0;
}
