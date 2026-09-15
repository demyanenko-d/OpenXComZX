// Логика кампании: новая игра, экономика баз, время, сохранения (банки 7, 8).
// Состояние — state.h (ST). Все функции вызываются, когда STATE_PAGE
// подключена в Win3 (обработчики экранов — да, диспетчер подключает сам).
// Подробности — project_docs/12_game_state.md.
#ifndef GAME_H
#define GAME_H

#include <stdint.h>
#include "state.h"

// --- контекст интерфейса (не сохраняется; Win1, scr_geo.c)
typedef struct {
	uint8_t base, craft, ufo, soldier;   // выбранные объекты для открытых окон
	uint8_t item, facility;              // item — слот сохранения / строка; facility — тип или запись
	uint16_t globe_lon;                  // центр глобуса-заглушки (16-битный угол)
	int16_t globe_lat;
	geo_t pick;                          // точка, выбранная на глобусе
	uint8_t new_base;                    // строится новая база
	uint8_t in_game;                     // 1 — есть загруженная/новая игра
	uint8_t flag;                        // окно НЛО: UFW_*; окно корабля: GCW_*
	uint8_t tkind, tidx;                 // цель (TargetInfo, перехват, путевая точка): TGT_*, номер
} ctx_t;
#define UFW_HYPER    0x01                // UfoDetectedState: hyperwave
#define UFW_DETECTED 0x02                // только что обнаружено («DETECTED»)
#define GCW_LOST     0x04                // GeoscapeCraftState с путевой точкой (цель потеряна)
#define ICW_BASE     0x08                // InterceptState(base): корабли базы ctx.base, «Go to base»
#define TGT_NONE     0                   // = DK_* (state.h)
#define TGT_UFO      DK_UFO
#define TGT_WAYPOINT DK_WAYPOINT
#define TGT_SITE     DK_SITE
#define TGT_ABASE    DK_ALIENBASE
#define TGT_BASE     DK_BASE
#define TGT_CRAFT    6
extern ctx_t ctx;

// Цели под кликом по глобусу (Globe::getTargets, scr_geo.c) для MultipleTargets (scr_fly.c)
#define MT_MAX 8
typedef struct { uint8_t kind, idx; } mtarget_t;   // TGT_*, номер
extern mtarget_t mt_list[MT_MAX];
extern uint8_t mt_n, mt_craft;                     // число; корабль, для которого цель (NONE8 — окна целей)

// --- настройки (своё окно опций; не часть сохранения)
#define SND_OPL3_AY  0     // музыка OPL3, эффекты AY
#define SND_AY       1     // музыка и эффекты на одном AY
#define SND_2AY      2     // музыка на одном AY, эффекты на втором
#define SND_2YM      3     // 2 x YM2203: музыка на FM, эффекты на SSG
typedef struct { uint8_t sound, music, sfx; } options_t;
extern options_t opt;

// --- сохранения (save.c, банк 7)
#define SAVE_SLOTS     10
#define SAVE_OK        0
#define SAVE_EMPTY     1
#define SAVE_BAD       2     // не наш формат или испорчен
#define SAVE_VERSION   3
#define SAVE_OTHERGAME 4     // сохранение другой игры (UFO/TFTD)
#define SAVE_IOERR     5
#define SAVE_BADSLOT   6
void st_clear(void) __banked;
uint8_t st_save(uint8_t slot, const char *name) __banked;
uint8_t st_peek(uint8_t slot, st_header_t *h) __banked;
uint8_t st_load(uint8_t slot) __banked;
uint8_t st_delete(uint8_t slot) __banked;
uint8_t st_ironsave(void) __banked;      // Ironman: в свой слот (автосохранение, выход)

// --- глобальные значения правил (RES_RULE_VARS; newgame.c, не сохраняются)
typedef struct {
	uint32_t initial_funding;            // тыс. $
	uint32_t cost_engineer, cost_scientist;   // зарплата в месяц (покупка — вдвое)
	uint16_t time_personnel;             // часов доставки персонала
	uint16_t alien_fuel, alien_fuel_amount;
	uint8_t diff_coef[5];
	int32_t defeat_score, defeat_funds;  // difficulty.rul: −900, −1 000 000
} vars_t;
extern vars_t gv;
void vars_load(void) __banked;           // после загрузки сохранения тоже
void game_new(uint8_t difficulty, uint8_t ironman) __banked;

// --- экономика (econ.c, банк 7)
typedef struct {
	uint16_t quarters, labs, workshops, hangars, psi, aliens;
	uint32_t stores;                     // сотые единицы склада (размер предмета * 100)
} caps_t;
typedef struct { uint32_t crafts, soldiers, engineers, scientists, facilities, total; } costs_t;
#define BUY_SOLDIER   0
#define BUY_SCIENTIST 1
#define BUY_ENGINEER  2
#define BUY_CRAFT     3
#define BUY_ITEM      4
#define SELL_SOLDIER  5                  // what — запись солдата
#define SELL_CRAFT    6                  // what — запись корабля
#define FAC_OK        0
#define FAC_IN_USE    1
#define FAC_CUTS      2

void soldier_get(uint8_t i, soldier_t *s) __banked;
void soldier_put(uint8_t i, const soldier_t *s) __banked;
void soldiers_clear(void) __banked;
uint8_t soldiers_count(uint8_t b, uint8_t c, uint8_t mask) __banked;   // c: 0xFE — любой корабль
uint8_t soldier_nth(uint8_t b, uint8_t k) __banked;
uint8_t soldier_new(uint8_t base) __banked;
void soldier_delete(uint8_t i) __banked;
uint8_t cargo_alloc(void) __banked;
void cargo_add(uint8_t cg, uint8_t item, uint16_t qty) __banked;
void base_personnel(uint8_t b, uint16_t *sci, uint16_t *eng) __banked;
void base_caps(uint8_t b, caps_t *avail, caps_t *used) __banked;
void base_costs(uint8_t b, costs_t *c) __banked;
int32_t total_maintenance(void) __banked;
void funds_add(int32_t v) __banked;
uint8_t fac_at(uint8_t b, uint8_t x, uint8_t y) __banked;
uint8_t fac_can_place(uint8_t b, uint8_t type, uint8_t x, uint8_t y) __banked;
uint8_t fac_build(uint8_t b, uint8_t type, uint8_t x, uint8_t y) __banked;
uint8_t fac_can_dismantle(uint8_t b, uint8_t fi) __banked;
void fac_dismantle(uint8_t b, uint8_t fi) __banked;
void transfer_add(uint8_t b, uint8_t kind, uint8_t item, uint16_t qty, uint8_t hours) __banked;
int32_t price(uint8_t kind, uint8_t what, uint8_t sell) __banked;
void econ_buy(uint8_t b, uint8_t kind, uint8_t what, uint16_t qty) __banked;
void econ_sell(uint8_t b, uint8_t kind, uint8_t what, uint16_t qty) __banked;
uint8_t base_alloc(void) __banked;
uint8_t bases_count(void) __banked;
uint8_t craft_add(uint8_t b, uint8_t type, uint8_t transit) __banked;   // запись или NONE8
void craft_remove(uint8_t c) __banked;       // груз на склад, экипаж снят, запись свободна
void craft_lost(uint8_t c) __banked;         // сбит: экипаж погиб, груз потерян, запись свободна
void base_remove(uint8_t b) __banked;        // база уничтожена: корабли, солдаты, поставки, проекты
uint8_t base_defenders(uint8_t b) __banked;  // есть солдаты или техника для обороны

// --- время (gtime.c, банк 8)
// События хода времени для окон (геоскейп показывает их по очереди и стоит).
#define GE_ARRIVED    1      // поставка пришла: what — предмет или TK_*_ARRIVED, qty
#define GE_BUILT      2      // постройка готова: what — facilities
#define GE_RESEARCH   3      // исследование готово: what — research
#define GE_PRODUCTION 4      // производство завершено: what — manufacture, qty
#define GE_NO_MONEY   5      // производство остановлено: нет денег
#define GE_NO_AMMO    6      // нет боеприпасов: what — корабль, qty — предмет
#define GE_MONTH      7      // конец месяца: отчёт (report)
#define GE_NEWRES     8      // новые возможные исследования (lab_newres)
#define GE_NEWMAN     9      // новое возможное производство (lab_newman)
#define GE_RESREQ     10     // оружие без исследованных боеприпасов: what — предмет, qty — боеприпас
#define GE_NO_MATERIALS 11   // производство остановлено: нет материалов
#define GE_UFO_DETECTED 12   // НЛО обнаружено: what — НЛО, qty — 1 гиперволной
#define GE_UFO_LOST     13   // потеряно (есть преследователи): what — НЛО
#define GE_SITE         14   // место миссии: what — site
#define GE_ALIEN_BASE   15   // база пришельцев найдена: what — abase
#define GE_BASE_ATTACK  16   // штурм базы X-COM: base, qty — НЛО
#define GE_DOGFIGHT     17   // перехват: what — корабль, qty — НЛО
#define GE_LANDING      18   // высадка? what — корабль, qty — DK_* цели
#define GE_PATROL       19   // корабль у путевой точки: what — корабль, qty — точка
#define GE_LOW_FUEL     20   // мало топлива, домой: what — корабль
#define GE_TARGET_LOST  21   // цель пропала: what — корабль, qty — точка (WP_PENDING) или NONE8
#define GE_NO_FUEL      22   // нет топлива на заправку: what — корабль, qty — предмет
#define TK_SCI_ARRIVED     0xFFF0
#define TK_ENG_ARRIVED     0xFFF1
#define TK_CRAFT_ARRIVED   0xFFF2   // qty — корабль
#define TK_SOLDIER_ARRIVED 0xFFF3   // qty — солдат
#define GEV_MAX 16
typedef struct { uint8_t kind, base; uint16_t what, qty; } gevent_t;
extern gevent_t gev[GEV_MAX];
extern uint8_t gev_n;
extern gevent_t ev_cur;               // показываемое окном событие (screens.c)
extern gevent_t arrivals[GEV_MAX];    // поставки для окна ItemsArriving
extern uint8_t narr;
uint8_t game_advance(uint16_t sec, uint16_t min) __banked;   // 1 — есть события
void game_start(void) __banked;       // первая база названа: months = 0, аренда, миссии
void gev_push(uint8_t kind, uint8_t base, uint16_t what, uint16_t qty) __banked;

// --- пришельцы (alien.c, банк 18): стратегия и миссии
#define UR_ASSAULT 0xFE               // ufo_reached: штурм базы
void strategy_init(void) __banked;    // AlienStrategy::init (новая игра)
void alien_month(void) __banked;      // determineAlienMissions
void alien_30min(void) __banked;      // AlienMission::think, удаление законченных
uint8_t ufo_reached(uint8_t u) __banked;     // NONE8, место миссии или UR_ASSAULT
void ufo_lifting(uint8_t u) __banked;
void ufo_shot_down(uint8_t u) __banked;
void ufo_free(uint8_t u) __banked;
void mission_delay(uint8_t mi, uint16_t minutes) __banked;
void mission_supply(uint8_t type, uint8_t region, uint8_t race, uint8_t ab) __banked;
void mission_retaliation(uint8_t region, uint8_t race) __banked;   // после сбития НЛО
int16_t pact_score(void) __banked;    // очки пакта (случайная миссия INFILTRATION)
void retaliation_cancel(uint8_t region) __banked;   // база уничтожена: возмездие региона снять
int16_t weights_choose(const int16_t *pairs, uint8_t npairs) __banked;   // WeightedOptions::choose

// --- корабли X-COM в полёте (craft.c, банк 20)
void craft_checkup(uint8_t c) __banked;      // gtime.c: ремонт / перевооружение / заправка
void craft_set_dest(uint8_t c, uint8_t kind, uint8_t idx) __banked;   // DK_NONE — патруль
void craft_launch(uint8_t c, uint8_t kind, uint8_t idx) __banked;     // ConfirmDestination
void craft_return(uint8_t c) __banked;
uint8_t waypoint_new(const geo_t *p) __banked;   // WP_PENDING до waypoint_confirm
void waypoint_confirm(uint8_t w) __banked;
void waypoint_drop(uint8_t w) __banked;
uint16_t craft_limit(uint16_t k) __banked;
void craft_advance(uint16_t n) __banked;
void craft_step(void) __banked;
void craft_10min(void) __banked;
uint16_t craft_fuel_limit(uint8_t c) __banked;
void craft_destroyed(uint8_t c) __banked;    // сбит в бою: очки, экипаж, запись

// --- воздушный бой (scr_dogf.c, банк 22): DogfightState, до 4 боёв
// Развёрнутые — окна экрана SCR_DOGFIGHT (время стоит), свёрнутые — значки на глобусе.
#define DF_MAX 4
extern uint8_t df_count, df_nmax;            // боёв всего / развёрнутых (геоскейп — без вызова банка)
uint8_t df_start(uint8_t c, uint8_t u) __banked;   // 0 — мест нет; 1 — бой; 2/3 — свёрнут: глубина / не над водой
uint8_t df_run(void) __banked;               // тики 30 мс по кадрам; 1 — бои кончились/развернулись
void df_draw_icons(void) __banked;           // значки свёрнутых боёв (поверх глобуса)
uint8_t df_icon_click(void) __banked;        // клик ui_click_x/y по значку: 1 — развёрнут, 2 — окно ошибки
void df_reset(void) __banked;                // новая игра / загрузка: боёв нет
uint32_t geo_angle(const geo_t *a, const geo_t *b) __banked;   // geo.c: 2·asin(хорда/2)

// --- НЛО на глобусе (ufo.c, банк 19)
void ufo_set_course(uint8_t u, const geo_t *dst, uint16_t knots) __banked;
uint16_t ufo_limit(uint16_t k) __banked;     // макрошаг: событие не раньше k-го шага
void ufo_advance(uint16_t n) __banked;       // n шагов без событий
void ufo_step(void) __banked;                // time5Seconds (НЛО)
void ufo_30min(void) __banked;               // обломки
void ufo_detect_all(void) __banked;          // очки и обнаружение
void sites_30min(void) __banked;
void sites_hour(void) __banked;
void bases_10min(void) __banked;             // базы X-COM, замеченные возмездием
void abases_day(void) __banked;
void abases_month(void) __banked;

// --- конец месяца (month.c, банк 16)
#define RF_GAME_OVER 0x01             // «You have not succeeded» (рейтинг или долг), ending = END_LOSE
#define RF_DEBTS     0x02             // предупреждение о долге (STR_COUNCIL_REDUCE_DEBTS)
#define RF_PSI       0x04             // после отчёта — окно PsiTraining
typedef struct {
	int32_t rating, threshold;        // рейтинг месяца, порог (defeatScore + 100·coef)
	int32_t income, diff, maintenance; // финансирование на новый месяц, его изменение, содержание
	uint8_t flags;                    // RF_*
	uint16_t happy, sad, pact;        // страны (биты): довольны, недовольны, новые пакты
} report_t;
extern report_t report;               // итоги последнего месяца (MonthlyReportState)
void month_end(void) __banked;

// --- исследования и производство (lab.c, банк 8)
// GE_RESEARCH: what — тема (| GE_OLD — уже известна, статья не всплывает),
// qty — бонус getOneFree или NONE16.
#define GE_OLD   0x8000
#define LAB_LIST 32
extern uint8_t lab_newres[LAB_LIST], lab_nnewres;    // списки для окон «новые возможные»
extern uint8_t lab_newman[LAB_LIST], lab_nnewman;
uint8_t res_done(uint16_t topic) __banked;
uint8_t res_list_done(uint16_t table, uint16_t rec, uint8_t off) __banked;   // rlist записи исследован
uint8_t research_find(uint8_t b, uint8_t topic) __banked;
uint8_t research_avail(uint8_t b, uint8_t *out, uint8_t max) __banked;
uint8_t research_start(uint8_t b, uint8_t topic) __banked;   // проект или NONE8
void research_cancel(uint8_t slot) __banked;
void research_finish(uint8_t slot) __banked;
uint8_t research_progress(uint8_t slot) __banked;             // 0 нет, 1 ?, 2..5
uint8_t prod_find(uint8_t b, uint8_t manuf) __banked;
uint8_t manuf_avail(uint8_t b, uint8_t *out, uint8_t max) __banked;
uint16_t manuf_have(uint8_t b, uint16_t ref) __banked;
uint8_t manuf_can_start(uint8_t b, uint8_t manuf) __banked;
uint8_t prod_new(uint8_t b, uint8_t manuf) __banked;
void prod_confirm(uint8_t slot) __banked;                     // первая единица: деньги, материалы
void prod_stop(uint8_t slot) __banked;
uint16_t prod_done_units(uint8_t slot) __banked;
void prod_hour(void) __banked;

// --- мир (world.c, банк 17)
uint16_t rng_next(void) __banked;
uint16_t rng_range(uint16_t lo, uint16_t hi) __banked;
uint8_t rng_percent(uint8_t n) __banked;         // RNG::percent
uint8_t region_at(uint16_t lon, int16_t lat) __banked;
uint8_t country_at(uint16_t lon, int16_t lat) __banked;
uint8_t region_has(uint8_t r, uint16_t lon, int16_t lat) __banked;   // RuleRegion::insideRegion
void add_activity(const geo_t *p, int16_t pts, uint8_t xcom) __banked;   // регион и страна точки

// --- сфера (geo.c, банк 17). Точки — geo_t (state.h); указатели — на стек, Win1 или ST.
typedef struct { int32_t dlon, dlat; uint16_t steps; } geo_vel_t;   // за шаг 5 с; шагов до цели
int16_t isin(uint16_t a) __banked;              // 65536 = 360°, результат Q14
int16_t icos(uint16_t a) __banked;
uint16_t base_dist16(uint8_t a, uint8_t b) __banked;   // хорда между базами (r = 51.2) * 16
uint16_t geo_chord(const geo_t *a, const geo_t *b) __banked;   // Q14
int16_t geo_cos(const geo_t *a, const geo_t *b) __banked;      // cos угла, Q14
uint32_t geo_dist(const geo_t *a, const geo_t *b) __banked;    // двоичные единицы (не больше истинного)
uint32_t geo_speed(uint16_t knots) __banked;                   // единиц за шаг 5 с
void geo_aim(const geo_t *pos, const geo_t *dst, uint32_t speed, geo_vel_t *v) __banked;
uint8_t geo_heading(const geo_vel_t *v) __banked;              // 0 нет, 1..8 N, NE … NW
void geo_move(geo_t *p, const geo_vel_t *v, uint16_t n) __banked;
int8_t world_texture(uint16_t lon, int16_t lat) __banked;      // текстура полигона WORLD.DAT или -1
uint8_t inside_land(const geo_t *p) __banked;                  // Globe::insideLand
uint8_t zone_areas(uint8_t region, uint8_t zone, uint16_t *first) __banked;   // число областей
void area_point(uint16_t area, geo_t *out) __banked;
uint8_t area_is_point(uint16_t area) __banked;
uint8_t zone_point(uint8_t region, uint8_t zone, geo_t *out) __banked;       // getRandomPoint
uint8_t land_point(uint8_t region, uint8_t zone, geo_t *out) __banked;       // getLandPoint

// --- строки для окон (screens.c, общий код)
char *fmt_funds(char *buf, int32_t v) __banked; // "$1,234,567" (screens.c, банк 1)

#endif
