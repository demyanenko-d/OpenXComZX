// Номера экранов и функции банков экранов.
// Банк 2 (scr_menu.c) — 1..31, банк 3 (scr_geo.c) — 32..63, банк 4 (scr_geo2.c) — 64..95.
#ifndef SCREENS_H
#define SCREENS_H

#include <stdint.h>
#include "ui.h"

// --- меню, Уфопедия (банк 2)
#define SCR_MAIN_MENU        1
#define SCR_NEW_GAME         2
#define SCR_LOAD             3
#define SCR_SAVE             4
#define SCR_PAUSE            5   // «Options» геоскейпа: Load / Save / Abandon / Cancel
#define SCR_ABANDON          6
#define SCR_ERROR            7
#define SCR_OPTIONS          8   // своё окно опций
#define SCR_UFOPAEDIA        9
#define SCR_UFOP_SELECT     10
#define SCR_DEBUG           11   // отладочное меню: открыть любое окно
#define SCR_COMBO           12   // выпадающий список ComboBox (combo, screens.c)
#define SCR_DELETE_SAVE     13   // подтверждение удаления сохранения (ctx.item — слот)

// --- концовки (банк 2, scr_end.c); группа 0 делится с меню: 24.. — сюда
#define SCR_SLIDESHOW       24   // SlideshowState: заставка cut_play
#define SCR_STATISTICS      25   // StatisticsState
#define CUT_INTRO 0              // заставки: номера записей таблицы CUTSCENES
#define CUT_WIN   1
#define CUT_LOSE  2
#define CUT_TLETH 3

// --- геоскейп (банк 3)
#define SCR_GEOSCAPE        32
#define SCR_UFO_DETECTED    36
#define SCR_UFO_LOST        37
#define SCR_MISSION_DETECTED 38
#define SCR_FUNDING         39
#define SCR_MONTHLY_REPORT  40
#define SCR_BASE_NAME       42
#define SCR_BUILD_NEW_BASE  43
#define SCR_CONFIRM_NEW_BASE 44
#define SCR_SELECT_DEST     49
#define SCR_REPORT_FAILED   51   // отчёт месяца: «You have failed» (поражение)

// --- события геоскейпа (банк 4)
#define SCR_RESEARCH_COMPLETE 64
#define SCR_NEW_POSS_RESEARCH 65
#define SCR_NEW_POSS_MANUF    66
#define SCR_PRODUCTION_DONE   67
#define SCR_ITEMS_ARRIVING    68
#define SCR_ALIEN_BASE        69
#define SCR_BASE_DEFENSE      70
#define SCR_BASE_DESTROYED    71
#define SCR_CONFIRM_LANDING   72
#define SCR_CONFIRM_CYDONIA   73
#define SCR_PSI_TRAINING      74
#define SCR_ALLOC_PSI         75
#define SCR_RESEARCH_REQUIRED 76

// --- база (банк 5, scr_base.c)
#define SCR_BASESCAPE        96
#define SCR_BASE_INFO        97
#define SCR_STORES           98
#define SCR_MONTHLY_COSTS    99
#define SCR_BUILD_FACILITIES 100
#define SCR_PLACE_FACILITY   101
#define SCR_DISMANTLE        102
#define SCR_CRAFTS           103
#define SCR_SOLDIERS         104
#define SCR_SOLDIER_INFO     105
#define SCR_SACK_SOLDIER     106
#define SCR_SAVE_NAME        107   // имя сохранения (своё окно ввода)
#define SCR_PLACE_LIFT       108

// --- база: покупка, продажа (банк 6, scr_base2.c)
#define SCR_PURCHASE         128
#define SCR_SELL             129
#define SCR_TRANSFER_BASE    130
#define SCR_TRANSFER_ITEMS   131
#define SCR_TRANSFER_CONFIRM 132
#define SCR_TRANSFERS        133   // поставки на базу (из BaseInfo)

// --- исследования, производство (банк 9, scr_lab.c)
#define SCR_RESEARCH         160
#define SCR_NEW_RESEARCH     161
#define SCR_RESEARCH_INFO    162
#define SCR_MANUFACTURE      163
#define SCR_NEW_MANUF        164
#define SCR_MANUF_START      165
#define SCR_MANUF_INFO       166

// --- корабль, снаряжение (банк 10, scr_craft.c)
#define SCR_CRAFT_INFO       192
#define SCR_CRAFT_SOLDIERS   193
#define SCR_CRAFT_WEAPONS    194
#define SCR_CRAFT_EQUIP      195
#define SCR_CRAFT_ARMOR      196
#define SCR_SOLDIER_ARMOR    197
#define SCR_INVENTORY        198

// --- полёты кораблей (банк 21, scr_fly.c); группа 6 делится с кораблём: 200.. — сюда
#define SCR_INTERCEPT        200
#define SCR_GEO_CRAFT        201
#define SCR_TARGET_INFO      202
#define SCR_CONFIRM_DEST     203
#define SCR_CRAFT_PATROL     204
#define SCR_LOW_FUEL         205
#define SCR_CRAFT_ERROR      206
#define SCR_MULTI_TARGETS    207

// --- воздушный бой (банк 22, scr_dogf.c); 216.. — сюда
#define SCR_DOGFIGHT         216   // окна развёрнутых боёв поверх геоскейпа
#define SCR_DOGFIGHT_ERROR   217   // DogfightErrorState (ctx.craft, ctx.item: 0 глубина, 1 не над водой)

// --- Уфопедия: статья (банк 13, scr_ufop.c)
#define SCR_ARTICLE          224

// --- графики (банк 4, scr_graph.c); группа 7 делится с Уфопедией: 240.. — сюда
#define SCR_GRAPHS           240

#define SCR_LAST             255

uint8_t menu_get(uint8_t id, sdef_t *s, wdef_t *w) __banked;
void menu_text(uint8_t id, uint8_t slot, uint8_t row, char *buf) __banked;
uint8_t menu_rows(uint8_t id, uint8_t slot) __banked;
uint8_t menu_event(uint8_t id, uint8_t ev, uint8_t arg) __banked;

uint8_t end_get(uint8_t id, sdef_t *s, wdef_t *w) __banked;
void end_text(uint8_t id, uint8_t slot, uint8_t row, char *buf) __banked;
uint8_t end_rows(uint8_t id, uint8_t slot) __banked;
uint8_t end_event(uint8_t id, uint8_t ev, uint8_t arg) __banked;
void cut_play(uint8_t type, uint8_t next) __banked;   // CutsceneState: заставка CUT_*, потом экран next
uint8_t cut_start(uint8_t type, uint8_t next) __banked;   // то же без перехода: первый экран (SCR_SLIDESHOW или next)

uint8_t geo_get(uint8_t id, sdef_t *s, wdef_t *w) __banked;
void geo_text(uint8_t id, uint8_t slot, uint8_t row, char *buf) __banked;
uint8_t geo_rows(uint8_t id, uint8_t slot) __banked;
uint8_t geo_event(uint8_t id, uint8_t ev, uint8_t arg) __banked;

uint8_t geo2_get(uint8_t id, sdef_t *s, wdef_t *w) __banked;
void geo2_text(uint8_t id, uint8_t slot, uint8_t row, char *buf) __banked;
uint8_t geo2_rows(uint8_t id, uint8_t slot) __banked;
uint8_t geo2_event(uint8_t id, uint8_t ev, uint8_t arg) __banked;
void geo_base_attack(void) __banked;               // штурм базы ctx.base НЛО ctx.ufo (scr_geo2.c)

uint8_t base_get(uint8_t id, sdef_t *s, wdef_t *w) __banked;
void base_text(uint8_t id, uint8_t slot, uint8_t row, char *buf) __banked;
uint8_t base_rows(uint8_t id, uint8_t slot) __banked;
uint8_t base_event(uint8_t id, uint8_t ev, uint8_t arg) __banked;

uint8_t base2_get(uint8_t id, sdef_t *s, wdef_t *w) __banked;
void base2_text(uint8_t id, uint8_t slot, uint8_t row, char *buf) __banked;
uint8_t base2_rows(uint8_t id, uint8_t slot) __banked;
uint8_t base2_event(uint8_t id, uint8_t ev, uint8_t arg) __banked;

uint8_t lab_get(uint8_t id, sdef_t *s, wdef_t *w) __banked;
void lab_text(uint8_t id, uint8_t slot, uint8_t row, char *buf) __banked;
uint8_t lab_rows(uint8_t id, uint8_t slot) __banked;
uint8_t lab_event(uint8_t id, uint8_t ev, uint8_t arg) __banked;

uint8_t ship_get(uint8_t id, sdef_t *s, wdef_t *w) __banked;
void ship_text(uint8_t id, uint8_t slot, uint8_t row, char *buf) __banked;
uint8_t ship_rows(uint8_t id, uint8_t slot) __banked;
uint8_t ship_event(uint8_t id, uint8_t ev, uint8_t arg) __banked;

uint8_t ufop_get(uint8_t id, sdef_t *s, wdef_t *w) __banked;
void ufop_text(uint8_t id, uint8_t slot, uint8_t row, char *buf) __banked;
uint8_t ufop_rows(uint8_t id, uint8_t slot) __banked;
uint8_t ufop_event(uint8_t id, uint8_t ev, uint8_t arg) __banked;
uint8_t ufop_visible(uint8_t rec) __banked;        // статья видна (requires, раздел)
uint8_t ufop_find_topic(uint8_t topic) __banked;   // статья темы исследования или NONE8
void ufop_open(uint8_t rec) __banked;              // статья для SCR_ARTICLE
extern uint8_t ufop_next;                          // статья после закрытия текущей (бонус «Отчётов») или NONE8

uint8_t fly_get(uint8_t id, sdef_t *s, wdef_t *w) __banked;
void fly_text(uint8_t id, uint8_t slot, uint8_t row, char *buf) __banked;
uint8_t fly_rows(uint8_t id, uint8_t slot) __banked;
uint8_t fly_event(uint8_t id, uint8_t ev, uint8_t arg) __banked;
void target_name(uint8_t kind, uint8_t idx, char *buf) __banked;   // имя цели TGT_* (scr_fly.c)
void mt_popup(uint8_t i, uint8_t op) __banked;     // MultipleTargetsState::popupTarget(mt_list[i]), op — A_PUSH / A_POP_PUSH

uint8_t dogf_get(uint8_t id, sdef_t *s, wdef_t *w) __banked;
void dogf_text(uint8_t id, uint8_t slot, uint8_t row, char *buf) __banked;
uint8_t dogf_rows(uint8_t id, uint8_t slot) __banked;
uint8_t dogf_event(uint8_t id, uint8_t ev, uint8_t arg) __banked;

uint8_t graph_get(uint8_t id, sdef_t *s, wdef_t *w) __banked;
void graph_text(uint8_t id, uint8_t slot, uint8_t row, char *buf) __banked;
uint8_t graph_rows(uint8_t id, uint8_t slot) __banked;
uint8_t graph_event(uint8_t id, uint8_t ev, uint8_t arg) __banked;

// Общие помощники экранов (screens.c, банк 1): имя корабля «TRITON-1», имя НЛО
// «ALIEN SUB-12». buf — в Win1 или на стеке (не константы/переменные банка).
void craft_name(uint8_t c, char *buf) __banked;
void ufo_name(uint8_t u, char *buf) __banked;

// ComboBox: экран-владелец заполняет combo (геометрия кнопки, фон, цвета, пункты)
// и открывает SCR_COMBO; выбор — combo.sel, combo.changed = 1 (владелец сбрасывает).
#define COMBO_MAX 16
typedef struct {
	int16_t x, y, w, h;          // кнопка ComboBox
	uint16_t bg;                 // фон окна списка (фон владельца)
	uint8_t ui, el;              // категория и элемент цвета владельца
	uint8_t n, sel, changed;
	uint16_t item[COMBO_MAX];    // STR_* пунктов
} combo_t;
extern combo_t combo;

#endif
