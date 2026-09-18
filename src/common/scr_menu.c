// Банк 2: меню, загрузка/сохранение, опции, Уфопедия, отладочный список окон.
// Раскладки — MainMenuState, NewGameState, ListGamesState, PauseState,
// AbandonGameState, ErrorMessageState, UfopaediaStartState, UfopaediaSelectState.
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "tsconf.h"
#include "res.h"
#include "rules.h"
#include "text.h"
#include "state.h"
#include "game.h"
#include "scrdef.h"

static uint8_t ng_diff, ng_iron;

// ---------------------------------------------------------------- главное меню

static const wdef_t w_main[] = {
	WINP(32, 20, 256, 160, UI_EL_WINDOW, POPB),
	BTN(64, 90, 92, 20, UI_EL_BUTTON, STR_NEW_GAME, A_PUSH, SCR_NEW_GAME, 'n'),
	BTN(164, 90, 92, 20, UI_EL_BUTTON, STR_LOAD_SAVED_GAME, A_PUSH, SCR_LOAD, 'l'),
	BTN(64, 118, 92, 20, UI_EL_BUTTON, STR_OPTIONS, A_PUSH, SCR_OPTIONS, 'o'),
	BTN(164, 118, 92, 20, UI_EL_BUTTON, STR_QUIT, A_CUSTOM, 1, 'q'),
	BTN(64, 146, 192, 20, UI_EL_BUTTON, DYN(1), A_PUSH, SCR_DEBUG, 'd'),
	TXT(32, 45, 256, 30, UI_EL_TEXT, DYN(0), BIG | TC),
};

// ---------------------------------------------------------------- новая игра

static const wdef_t w_newgame[] = {
	WINP(64, 10, 192, 180, UI_EL_WINDOW, POPV),
	TGL(80, 32, 160, 18, UI_EL_BUTTON, STR_1_BEGINNER, 0, 1, 0, 0),
	TGL(80, 52, 160, 18, UI_EL_BUTTON, STR_2_EXPERIENCED, 0, 1, 1, 0),
	TGL(80, 72, 160, 18, UI_EL_BUTTON, STR_3_VETERAN, 0, 1, 2, 0),
	TGL(80, 92, 160, 18, UI_EL_BUTTON, STR_4_GENIUS, 0, 1, 3, 0),
	TGL(80, 112, 160, 18, UI_EL_BUTTON, STR_5_SUPERHUMAN, 0, 1, 4, 0),
	TGL(80, 138, 78, 18, UI_EL_IRONMAN, STR_IRONMAN, 0, 0, 10, 0),
	BTN(80, 164, 78, 16, UI_EL_BUTTON, STR_OK, A_CUSTOM, 20, ENT),
	BTN(162, 164, 78, 16, UI_EL_BUTTON, STR_CANCEL, A_POP, 0, ESC),
	TXT(64, 20, 192, 9, UI_EL_TEXT, STR_SELECT_DIFFICULTY_LEVEL, TC),
	TXT(162, 135, 90, 24, UI_EL_IRONMAN, STR_IRONMAN_DESC, TM | TW),
};

// ---------------------------------------------------------------- загрузка / сохранение

static const wdef_t w_saves[] = {
	WINP(0, 0, 320, 200, UI_EL_WINDOW, POPB),
	BTN(120, 172, 80, 16, UI_EL_BUTTON, STR_CANCEL, A_POP, 0, ESC),
	TXT(5, 7, 310, 17, UI_EL_TEXT, DYN(2), BIG | TC),
	TXT(5, 23, 310, 9, UI_EL_TEXT, STR_RIGHT_CLICK_TO_DELETE, TC),
	TXT(16, 32, 150, 9, UI_EL_TEXT, STR_NAME, 0),
	TXT(204, 32, 110, 9, UI_EL_TEXT, STR_DATE, 0),
	LST(8, 42, 288, 112, UI_EL_LIST, 0, WF_SEL | WF_RSEL),
	TXT(16, 156, 288, 16, UI_EL_TEXT, DYN(1), TW),
};

// DeleteGameState: правый клик по слоту в списке сохранений
static const wdef_t w_delete[] = {
	WINP(32, 50, 256, 100, UI_EL_CONFIRMDELETE, POPB),
	BTN(60, 122, 60, 18, UI_EL_CONFIRMDELETE, STR_YES, A_CUSTOM, 1, ENT),
	BTN(200, 122, 60, 18, UI_EL_CONFIRMDELETE, STR_NO, A_POP, 0, ESC),
	TXT(37, 70, 246, 32, UI_EL_CONFIRMDELETE, STR_IS_IT_OK_TO_DELETE_THE_SAVED_GAME, BIG | TC | TW),
};
static uint8_t slots_dirty;              // слот удалён — перечитать заголовки

// Слоты сохранений: заголовки читаются при открытии списка (st_peek).
static st_header_t slot_hdr[SAVE_SLOTS];
static uint8_t slot_state[SAVE_SLOTS];      // SAVE_OK / SAVE_EMPTY / ошибка

static void slots_scan(void)
{
	for (uint8_t i = 0; i < SAVE_SLOTS; i++) slot_state[i] = st_peek(i, &slot_hdr[i]);
}

static void two(char *p, uint8_t v) { p[0] = '0' + v / 10; p[1] = '0' + v % 10; p[2] = 0; }

// Строка слота: имя | дата | время (ListGamesState)
static void slot_row(uint8_t i, char *buf)
{
	const st_header_t *h = &slot_hdr[i];
	if (slot_state[i] != SAVE_OK) {
		strcpy(buf, slot_state[i] == SAVE_EMPTY ? "- - -" : "???");
		return;
	}
	strcpy(buf, h->name);
	if (h->ironman) { strcat(buf, " ("); str_copy(STR_IRONMAN, buf + strlen(buf), 32); strcat(buf, ")"); }
	strcat(buf, "\t");
	char *p = buf + strlen(buf);
	two(p, h->day); p[2] = '/'; two(p + 3, h->month); p[5] = '/';
	fmt_num(p + 6, h->year, 0);
	strcat(buf, "\t");
	p = buf + strlen(buf);
	two(p, h->hour); p[2] = ':'; two(p + 3, h->minute);
}

// После загрузки или новой игры: контекст окон и глобальные значения правил.
static void after_load(void)
{
	vars_load();
	memset(&ctx, 0, sizeof ctx);
	ctx.in_game = 1;
	ctx.base = ST->sel_base < MAX_BASES && ST->base[ST->sel_base].name[0] ? ST->sel_base : 0;
	geo_t g = ST->globe;                         // центр из сохранения, иначе — база
	if (!g.lon && !g.lat) g = ST->base[ctx.base].pos;
	ctx.globe_lon = (uint16_t)((uint32_t)g.lon >> 16);
	ctx.globe_lat = (int16_t)(g.lat >> 16);
	gev_n = 0;
}

// Отладка: игра с поставленной базой без диалогов (для списка окон).
static void quick_game(void)
{
	game_new(0, 0);
	strcpy(ST->base[0].name, "DEBUG BASE");
	ST->base[0].pos.lon = 0x20000000l;
	ST->base[0].pos.lat = -0x08000000l;
	game_start();
	after_load();
}

// ---------------------------------------------------------------- пауза, выход, ошибка

static const wdef_t w_pause[] = {
	WINP(20, 20, 216, 160, UI_EL_WINDOW, POPB),
	BTN(38, 52, 180, 18, UI_EL_BUTTON, STR_LOAD_GAME, A_PUSH, SCR_LOAD, 0),
	BTN(38, 74, 180, 18, UI_EL_BUTTON, STR_SAVE_GAME, A_PUSH, SCR_SAVE, 0),
	BTN(38, 96, 180, 18, UI_EL_BUTTON, STR_ABANDON_GAME, A_PUSH, SCR_ABANDON, 0),
	BTN(38, 122, 180, 18, UI_EL_BUTTON, STR_GAME_OPTIONS, A_PUSH, SCR_OPTIONS, 0),
	BTN(38, 150, 180, 18, UI_EL_BUTTON, STR_CANCEL_UC, A_POP, 0, ESC),
	TXT(25, 32, 206, 17, UI_EL_TEXT, STR_OPTIONS_UC, BIG | TC),
};

static const wdef_t w_abandon[] = {
	WINP(20, 20, 216, 160, UI_EL_GENERICWINDOW, POPB),
	BTN(38, 140, 50, 20, UI_EL_GENERICBUTTON2, STR_YES, A_CUSTOM, 1, ENT),
	BTN(168, 140, 50, 20, UI_EL_GENERICBUTTON2, STR_NO, A_POP, 0, ESC),
	TXT(25, 70, 206, 17, UI_EL_GENERICTEXT, STR_ABANDON_GAME_QUESTION, BIG | TC),
};

static const wdef_t w_error[] = {
	WINP(32, 20, 256, 160, UI_EL_GENERICWINDOW, POPB),
	BTN(100, 154, 120, 18, UI_EL_GENERICBUTTON2, STR_OK, A_POP, 0, ESC),
	TXT(37, 50, 246, 80, UI_EL_GENERICTEXT, DYN(0), BIG | TC | TM | TW),
};

// ---------------------------------------------------------------- опции (своё окно)

static const wdef_t w_options[] = {
	WIN(32, 20, 256, 160, UI_EL_WINDOW),
	TXT(37, 30, 246, 17, UI_EL_TEXT, STR_OPTIONS_UC, BIG | TC),
	TXT(48, 52, 224, 9, UI_EL_TEXT, DYN(1), 0),
	TGL(48, 62, 108, 14, UI_EL_BUTTON, DYN(2), 0, 1, SND_OPL3_AY, 0),
	TGL(164, 62, 108, 14, UI_EL_BUTTON, DYN(3), 0, 1, SND_AY, 0),
	TGL(48, 78, 108, 14, UI_EL_BUTTON, DYN(4), 0, 1, SND_2AY, 0),
	TGL(164, 78, 108, 14, UI_EL_BUTTON, DYN(5), 0, 1, SND_2YM, 0),
	TXT(48, 100, 224, 9, UI_EL_TEXT, DYN(6), 0),
	TGL(48, 110, 108, 14, UI_EL_BUTTON, DYN(7), 0, 0, 10, 'm'),
	TGL(164, 110, 108, 14, UI_EL_BUTTON, DYN(8), 0, 0, 11, 's'),
	BTN(96, 152, 128, 16, UI_EL_BUTTON, STR_OK, A_POP, 0, ESC),
};

static const char *const opt_text[] = {
	"", "Sound devices:", "OPL3 + AY", "AY (music+sfx)", "2 x AY", "2 x YM2203",
	"Sound:", "MUSIC", "EFFECTS",
};

// ---------------------------------------------------------------- Уфопедия

// Кнопки разделов добавляются в menu_get (как _btnSections в UfopaediaStartState).
static const wdef_t w_ufop[] = {
	WINP(32, 10, 256, 180, UI_EL_WINDOW, POPB),
	TXT(50, 33, 220, 17, UI_EL_TEXT, STR_UFOPAEDIA, BIG | TC),
	BTN(50, 167, 220, 12, UI_EL_BUTTON1, STR_OK, A_POP, 0, ESC),
};

static const wdef_t w_ufsel[] = {
	WIN(32, 10, 256, 180, UI_EL_WINDOW),
	TXT(48, 26, 224, 17, UI_EL_TEXT, STR_SELECT_ITEM, BIG | TC),
	BTN(48, 166, 224, 16, UI_EL_BUTTON2, STR_OK, A_POP, 0, ESC),
	LST(40, 50, 224, 104, UI_EL_LIST, 0, WF_SEL),
};

#define MAX_SEC 12
#define MAX_ART 96
static uint16_t sec[MAX_SEC];
static uint8_t nsec, cur_sec, nart;
static uint16_t art[MAX_ART];
static rtab_t upt;

static void ufop_scan(void)
{
	nsec = 0;
	if (!rtab_open(RES_RULE_UFOPAEDIA, &upt)) return;
	for (uint16_t i = 0; i < upt.n; i++) {
		uint16_t s = rtab_word(&upt, i, offsetof(r_ufopaedia_t, section));
		if (!ufop_visible((uint8_t)i)) continue;      // раздел виден, если в нём есть видимая статья
		uint8_t k = 0;
		while (k < nsec && sec[k] != s) k++;
		if (k == nsec && nsec < MAX_SEC) sec[nsec++] = s;
	}
}

static void ufop_articles(void)
{
	nart = 0;
	for (uint16_t i = 0; i < upt.n && nart < MAX_ART; i++)
		if (rtab_word(&upt, i, offsetof(r_ufopaedia_t, section)) == sec[cur_sec] && ufop_visible((uint8_t)i)) art[nart++] = i;
}

// ---------------------------------------------------------------- отладка: все окна

static const wdef_t w_debug[] = {
	WIN(0, 0, 320, 200, UI_EL_WINDOW),
	TXT(5, 7, 310, 17, UI_EL_TEXT, DYN(0), BIG | TC),
	LST(8, 28, 288, 136, UI_EL_LIST, 1, WF_SEL),
	BTN(120, 172, 80, 16, UI_EL_BUTTON, STR_CANCEL, A_POP, 0, ESC),
};

static const struct { uint8_t id; const char *name; } dbg_list[] = {
	{ SCR_BATTLE, "Battlescape" },   // пока отлаживаем бой — первой строкой
	{ SCR_GEOSCAPE, "Geoscape" }, { SCR_INTERCEPT, "Intercept" }, { SCR_GEO_CRAFT, "Geoscape craft" },
	{ SCR_TARGET_INFO, "Target info" }, { SCR_UFO_DETECTED, "UFO detected" }, { SCR_UFO_LOST, "UFO lost" },
	{ SCR_MISSION_DETECTED, "Mission detected" }, { SCR_FUNDING, "Funding" }, { SCR_MONTHLY_REPORT, "Monthly report" },
	{ SCR_GRAPHS, "Graphs" }, { SCR_BASE_NAME, "Base name" }, { SCR_BUILD_NEW_BASE, "Build new base" },
	{ SCR_CONFIRM_NEW_BASE, "Confirm new base" }, { SCR_CRAFT_PATROL, "Craft patrol" }, { SCR_LOW_FUEL, "Low fuel" },
	{ SCR_CRAFT_ERROR, "Craft error" }, { SCR_CONFIRM_DEST, "Confirm destination" }, { SCR_SELECT_DEST, "Select destination" },
	{ SCR_MULTI_TARGETS, "Multiple targets" }, { SCR_RESEARCH_COMPLETE, "Research complete" },
	{ SCR_NEW_POSS_RESEARCH, "New possible research" }, { SCR_NEW_POSS_MANUF, "New possible manufacture" },
	{ SCR_PRODUCTION_DONE, "Production complete" }, { SCR_ITEMS_ARRIVING, "Items arriving" },
	{ SCR_ALIEN_BASE, "Alien base" }, { SCR_BASE_DEFENSE, "Base defense" }, { SCR_BASE_DESTROYED, "Base destroyed" },
	{ SCR_CONFIRM_LANDING, "Confirm landing" }, { SCR_CONFIRM_CYDONIA, "Confirm Cydonia" },
	{ SCR_PSI_TRAINING, "Psi training" }, { SCR_ALLOC_PSI, "Allocate psi training" },
	{ SCR_RESEARCH_REQUIRED, "Research required" }, { SCR_DOGFIGHT_ERROR, "Dogfight error" },
	{ SCR_DOGFIGHT, "Dogfight" }, { SCR_PAUSE, "Pause" }, { SCR_ERROR, "Error message" },
	{ SCR_UFOPAEDIA, "Ufopaedia" }, { SCR_OPTIONS, "Options" },
	{ SCR_BASESCAPE, "Basescape" }, { SCR_BASE_INFO, "Base information" }, { SCR_STORES, "Stores" },
	{ SCR_MONTHLY_COSTS, "Monthly costs" }, { SCR_BUILD_FACILITIES, "Build facilities" },
	{ SCR_CRAFTS, "Crafts" }, { SCR_SOLDIERS, "Soldiers" }, { SCR_PURCHASE, "Purchase" }, { SCR_SELL, "Sell" },
	{ SCR_STATISTICS, "Statistics" }, { SCR_SLIDESHOW, "Lose/win slideshow" },
};
static uint8_t dbg_cut;                 // «Lose/win slideshow»: по очереди поражение и победа
#define NDBG (sizeof dbg_list / sizeof dbg_list[0])

// ---------------------------------------------------------------- ComboBox

// Список ComboBox (ComboBox.cpp): окно с тонкой рамкой под кнопкой, до 10 строк
// по 8 точек + поля 3, список с отступом 2 и без колонки кнопки (14). Клик мимо
// или ESC — закрыть без выбора. Окно и список добавляет menu_get по combo.
static const wdef_t w_combo[] = {
	HOT(0, 0, 320, 200, A_POP, 0, ESC),
};

static uint8_t combo_rows(void)
{
	uint8_t items = combo.n < 10 ? combo.n : 10;
	while (items && combo.y + combo.h + items * 8 + 6 > 200) items--;
	return items;
}

// ---------------------------------------------------------------- таблица экранов

static const scr_t tab[] = {
	SCR(SCR_MAIN_MENU, UI_SCR_MAINMENU, NOUI, RES_BACK01_SCR, 0, w_main),
	SCR(SCR_NEW_GAME, UI_SCR_NEWGAMEMENU, NOUI, RES_BACK01_SCR, 0, w_newgame),
	SCR(SCR_LOAD, UI_SCR_SAVEMENUS, UI_SCR_GEOSCAPE, RES_BACK01_SCR, SF_POPUP | SF_ALTPAL, w_saves),
	SCR(SCR_SAVE, UI_SCR_SAVEMENUS, UI_SCR_GEOSCAPE, RES_BACK01_SCR, SF_POPUP | SF_ALTPAL, w_saves),
	SCR(SCR_PAUSE, UI_SCR_PAUSEMENU, NOUI, RES_BACK01_SCR, SF_POPUP, w_pause),
	SCR(SCR_ABANDON, UI_SCR_GEOSCAPE, NOUI, RES_BACK01_SCR, SF_POPUP, w_abandon),
	SCR(SCR_ERROR, UI_SCR_GEOSCAPE, NOUI, RES_BACK01_SCR, SF_POPUP, w_error),
	SCR(SCR_OPTIONS, UI_SCR_OPTIONSMENU, NOUI, RES_BACK01_SCR, SF_POPUP, w_options),
	SCR(SCR_UFOPAEDIA, UI_SCR_UFOPAEDIA, NOUI, RES_BACK01_SCR, SF_POPUP, w_ufop),
	SCR(SCR_UFOP_SELECT, UI_SCR_UFOPAEDIA, NOUI, RES_BACK01_SCR, SF_POPUP, w_ufsel),
	SCR(SCR_DEBUG, UI_SCR_SAVEMENUS, UI_SCR_GEOSCAPE, RES_BACK01_SCR, SF_POPUP | SF_ALTPAL, w_debug),
	SCR(SCR_COMBO, 0, NOUI, 0, SF_POPUP, w_combo),
	SCR(SCR_DELETE_SAVE, UI_SCR_SAVEMENUS, UI_SCR_GEOSCAPE, RES_BACK01_SCR, SF_POPUP | SF_ALTPAL, w_delete),
};

uint8_t menu_get(uint8_t id, sdef_t *s, wdef_t *w) __banked
{
	if (!scr_find(tab, sizeof tab / sizeof tab[0], id, s, w)) return 0;
	if (slots_dirty && (id == SCR_LOAD || id == SCR_SAVE)) { slots_dirty = 0; slots_scan(); }
	if (ctx.in_game && ST->ironman) {             // Ironman: без загрузки и ручного сохранения
		if (id == SCR_PAUSE) {
			w[1].type = w[2].type = 0;
			w[3].str = STR_SAVE_AND_ABANDON_GAME;
		}
		if (id == SCR_SAVE) { w[1].type = 0; w[1].key = 0; }   // выбор слота обязателен
	}
	if (id == SCR_UFOPAEDIA) {
		for (uint8_t i = 0; i < nsec && s->n < SDEF_MAXW; i++) {
			wdef_t *b = &w[s->n++];
			b->type = W_BUTTON; b->x = 50; b->y = 50 + 13 * i; b->w = 220; b->h = 12;
			b->el = UI_EL_BUTTON1; b->str = DYN(10 + i); b->flags = 0;
			b->act = A_CUSTOM; b->arg = 10 + i; b->key = 0;
		}
	}
	if (id == SCR_COMBO) {
		uint8_t items = combo_rows();
		int16_t py = combo.y + combo.h;
		s->ui = combo.ui;
		// без картинки фона: у оригинала ComboBox::setBackground никто не зовёт, окно заливается
		// цветом элемента — иначе подсветка строки (offset +1, не ниже backPos 224) превращает
		// пиксели картинки в однотонную заливку 224 и выглядит как инверсия
		s->bg = 0;
		wdef_t *b = &w[s->n++];
		b->type = W_WINDOW; b->x = combo.x; b->y = py; b->w = combo.w; b->h = items * 8 + 6;
		b->el = combo.el; b->str = NOSTR; b->flags = WF_THIN; b->act = b->arg = b->key = 0;
		b = &w[s->n++];
		b->type = W_LIST; b->x = combo.x + 2; b->y = py + 3; b->w = combo.w - 4 - 14 + 1; b->h = items * 8;
		b->el = combo.el; b->str = DYN(0); b->flags = WF_SEL | TC; b->act = b->arg = b->key = 0;
	}
	return 1;
}

void menu_text(uint8_t id, uint8_t slot, uint8_t row, char *buf) __banked
{
	switch (id) {
	case SCR_MAIN_MENU:
		if (slot == 0) {
			str_copy(STR_OPENXCOM, buf, 200);
			strcat(buf, "\x02ZX Evolution / TS-Config");
		} else
			strcpy(buf, "DEBUG: ALL WINDOWS");
		break;
	case SCR_LOAD:
	case SCR_SAVE:
		if (slot == 2) str_copy(id == SCR_LOAD ? STR_SELECT_GAME_TO_LOAD : STR_SELECT_SAVE_POSITION, buf, 256);
		else if (slot == 1) str_fmt(buf, str_get(STR_DETAILS), "", "");
		else if (row == LIST_COLS) {
			static const uint8_t cw[] = { 188, 60, 40 };
			list_cols(buf, 3, cw, 0);
		} else if (row < SAVE_SLOTS)
			slot_row(row, buf);
		break;
	case SCR_ERROR:
		strcpy(buf, ui_msg);
		break;
	case SCR_COMBO:
		if (row != LIST_COLS && row < combo.n) str_copy(combo.item[row], buf, 128);
		break;
	case SCR_OPTIONS:
		strcpy(buf, opt_text[slot]);
		break;
	case SCR_UFOPAEDIA:
		if (slot >= 10 && slot - 10 < nsec) str_copy(sec[slot - 10], buf, 256);
		break;
	case SCR_UFOP_SELECT:
		if (row == LIST_COLS) {
			static const uint8_t cw[] = { 206 }, ca[] = { TX_CENTER };
			list_cols(buf, 1, cw, ca);
		} else if (row < nart)
			str_copy(rtab_word(&upt, art[row], offsetof(r_ufopaedia_t, name)), buf, 256);
		break;
	case SCR_DEBUG:
		if (slot == 0) strcpy(buf, "ALL WINDOWS (DEBUG)");
		else if (row == LIST_COLS) {
			static const uint8_t cw[] = { 30, 250 };
			list_cols(buf, 2, cw, 0);
		} else if (row < NDBG) {
			fmt_num(buf, dbg_list[row].id, 0);
			strcat(buf, "\t");
			strcat(buf, dbg_list[row].name);
		}
		break;
	}
}

uint8_t menu_rows(uint8_t id, uint8_t slot) __banked
{
	(void)slot;
	switch (id) {
	case SCR_LOAD: case SCR_SAVE: return SAVE_SLOTS;
	case SCR_UFOP_SELECT: return nart;
	case SCR_DEBUG: return NDBG;
	case SCR_COMBO: return combo.n;
	}
	return 0;
}

uint8_t menu_event(uint8_t id, uint8_t ev, uint8_t arg) __banked
{
	switch (id) {
	case SCR_MAIN_MENU:
		if (ev == EVT_BUTTON && arg == 1) {       // выход: в эмуляторе — код 0
			OXZ_DBG_EXIT = 0;
			for (;;) __asm__("halt");
		}
		break;
	case SCR_NEW_GAME:
		if (ev == EVT_OPEN) { ng_diff = 0; ng_iron = 0; }
		else if (ev == EVT_QUERY) return arg == 10 ? ng_iron : arg == ng_diff;
		else if (ev == EVT_BUTTON) {
			if (arg == 10) ng_iron ^= 1;
			else if (arg < 5) ng_diff = arg;
			else if (arg == 20) {                   // новая игра: сразу — место первой базы
				game_new(ng_diff, ng_iron);
				after_load();
				UI_GO(A_SET, SCR_GEOSCAPE);
				ui_request = SCR_BUILD_NEW_BASE;
			}
		}
		break;
	case SCR_DELETE_SAVE:
		if (ev == EVT_BUTTON && arg == 1) { st_delete(ctx.item); slots_dirty = 1; UI_GO(A_POP, 0); }
		break;
	case SCR_ABANDON:                             // AbandonGameState: Ironman — сохранить и выйти
		if (ev == EVT_BUTTON && arg == 1) {
			if (ST->ironman) st_ironsave();
			UI_GO(A_SET, SCR_MAIN_MENU);
		}
		break;
	case SCR_LOAD:
		if (ev == EVT_OPEN) slots_scan();
		else if (ev == EVT_LIST && ui_arrow_max) {       // правая кнопка — удалить
			if (arg < SAVE_SLOTS && slot_state[arg] == SAVE_OK) { ctx.item = arg; UI_GO(A_PUSH, SCR_DELETE_SAVE); }
		} else if (ev == EVT_LIST && arg < SAVE_SLOTS && slot_state[arg] == SAVE_OK) {
			uint8_t r = st_load(arg);
			if (r == SAVE_OK) { after_load(); UI_GO(A_SET, SCR_GEOSCAPE); }
			else {
				strcpy(ui_msg, r == SAVE_VERSION ? "WRONG SAVE VERSION" : r == SAVE_OTHERGAME ? "SAVE OF OTHER GAME" : "SAVE IS DAMAGED");
				if (r == SAVE_BAD) ctx.in_game = 0;
				UI_GO(r == SAVE_BAD ? A_SET : A_PUSH, r == SAVE_BAD ? SCR_MAIN_MENU : SCR_ERROR);
			}
		}
		break;
	case SCR_SAVE:
		if (ev == EVT_OPEN) slots_scan();
		else if (ev == EVT_LIST && ui_arrow_max) {
			if (arg < SAVE_SLOTS && slot_state[arg] == SAVE_OK) { ctx.item = arg; UI_GO(A_PUSH, SCR_DELETE_SAVE); }
		} else if (ev == EVT_LIST && arg < SAVE_SLOTS) {  // имя: прежнее или «GAME n»
			ctx.item = arg;
			if (slot_state[arg] == SAVE_OK) strcpy(ui_edit, slot_hdr[arg].name);
			else { strcpy(ui_edit, "GAME "); fmt_num(ui_edit + 5, arg + 1, 0); }
			UI_GO(A_PUSH, SCR_SAVE_NAME);
		}
		break;
	case SCR_OPTIONS:
		if (ev == EVT_QUERY) return arg == 10 ? opt.music : arg == 11 ? opt.sfx : arg == opt.sound;
		if (ev == EVT_BUTTON) {
			if (arg == 10) opt.music ^= 1;
			else if (arg == 11) opt.sfx ^= 1;
			else opt.sound = arg;
		}
		break;
	case SCR_UFOPAEDIA:
		if (ev == EVT_OPEN) ufop_scan();
		else if (ev == EVT_BUTTON && arg >= 10) {
			cur_sec = arg - 10;
			UI_GO(A_PUSH, SCR_UFOP_SELECT);
		}
		break;
	case SCR_UFOP_SELECT:
		if (ev == EVT_OPEN) ufop_articles();
		else if (ev == EVT_LIST && arg < nart) {
			ufop_open((uint8_t)art[arg]);
			UI_GO(A_PUSH, SCR_ARTICLE);
		}
		break;
	case SCR_COMBO:
		if (ev == EVT_LIST && arg < combo.n) { combo.sel = arg; combo.changed = 1; UI_GO(A_POP, 0); }
		break;
	case SCR_DEBUG:
		if (ev == EVT_LIST) {
			if (!ctx.in_game) quick_game();
			strcpy(ui_msg, "Test message");
			if (dbg_list[arg].id == SCR_SLIDESHOW) {   // конец игры: заставка, статистика, главное меню
				dbg_cut ^= 1;
				ST->ending = dbg_cut ? END_LOSE : END_WIN;
				cut_play(dbg_cut ? CUT_LOSE : CUT_WIN, SCR_STATISTICS);
			} else UI_GO(A_PUSH, dbg_list[arg].id);
		}
		break;
	}
	return 0;
}
