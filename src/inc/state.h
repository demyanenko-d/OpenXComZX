// Состояние кампании (аналог SavedGame OpenXcom) — фиксированные массивы в
// дальней памяти, сохраняется как есть. Спецификация полей и формул —
// tmp/state_model.md (разбор SavedGame.cpp и др.), итог — 12_game_state.md.
//
//   STATE_PAGE     (#06)  state_t — ядро: время, деньги, базы, корабли, страны…
//   STATE_PAGE + 1 (#07)  soldier_t[MAX_SOLDIERS] — солдаты
//
// Доступ: на время вызовов экранов (scr_*) диспетчер подключает STATE_PAGE в
// Win3, указатель ST действителен. Нельзя: держать указатели в ST через gfx_map
// (он меняет Win3) и передавать указатели внутрь ST приёмником far_read /
// str_copy (они сами подключают Win3). Солдаты — копией: soldier_get/put.
//
// Ссылки — номера записей таблиц правил (rules.h) или пулов ниже, NONE8 — нет.
// Координаты: geo_t — «двоичные углы» int32: 2^32 = 360°; широта минус — север
// (как в OpenXcom). 16 бит мало: медленный корабль за 5 с сдвигается на ~0.02°.
#ifndef STATE_H
#define STATE_H

#include <stdint.h>
#include "memmap.h"
#include "rules.h"

#define ST_MAGIC        0x535A584Ful     // "OXZS"
#define ST_VERSION      1

#define NAME_LEN        16
#define SOLDIER_NAME    22
#define SAVE_NAME       24
#define HIST            12      // месяцев истории (графики, отчёт)

#define MAX_BASES       8       // MiniBaseView::MAX_BASES
#define MAX_FACILITIES  36      // сетка 6x6; ангар 2x2 — одна запись
#define BASE_SIZE       6
#define MAX_ITEMS       160     // записей items (TFTD 150, UFO 145)
#define RES_BITS        20      // битовые карты research (TFTD 133, UFO 137)
#define MANUF_BITS      8       // manufacture (TFTD 42, UFO 35)
#define MAX_COUNTRIES   16
#define MAX_REGIONS     17
#define MAX_SOLDIERS    250     // страница солдат; в оригинале тоже 250
#define MAX_CRAFTS      32
#define MAX_CARGO       16      // трюмы транспортов
#define CARGO_ITEMS     32
#define CARGO_VEH       4
#define MAX_TRANSFERS   48      // только предметы и персонал
#define MAX_RESEARCH    48      // пул на все базы
#define MAX_PRODS       32
#define MAX_UFOS        16
#define MAX_MISSIONS    32
#define MAX_ALIEN_BASES 16
#define MAX_SITES       8
#define MAX_WAYPOINTS   16
#define MAX_AMT         10      // строк alienMissions (8/10)
#define NVARS           2       // missionScripts varNames (TFTD: artifacts, shippingLanes)
#define VARLOC          12
#define NONE8           0xFF
#define NONE16          0xFFFF

typedef struct { int32_t lon, lat; } geo_t;

// Заголовок сохранения (первые байты состояния; список слотов читает только его)
typedef struct {
	uint32_t magic;
	uint8_t version, game;          // game: 1 — UFO, 2 — TFTD (res_game)
	uint8_t difficulty, ironman;
	char name[SAVE_NAME];           // имя сохранения
	uint16_t year;
	uint8_t month, day, hour, minute;
	uint16_t size;                  // байт в сохранении
	uint16_t sum;                   // контрольная сумма всего после заголовка
	uint8_t pad[2];
} st_header_t;                      // 44 байта

// --- финансы: история по месяцам, [hist_len - 1] — текущий
typedef struct {
	int32_t balance[HIST], maintenance[HIST], income[HIST], expenditure[HIST];
	int16_t research[HIST];         // очки исследований
} finance_t;

#define CF_PACT     0x01
#define CF_NEWPACT  0x02
typedef struct {
	uint16_t funding[HIST];         // тыс. $
	int16_t act_xcom[HIST], act_alien[HIST];
	uint8_t flags;                  // CF_*
} country_t;

typedef struct { int16_t act_xcom[HIST], act_alien[HIST]; } region_t;

typedef struct {                    // AlienStrategy
	uint8_t region_w[MAX_REGIONS];
	uint8_t mission_w[MAX_REGIONS][MAX_AMT];   // 0 — вычеркнута
	struct { uint8_t region, zone; } loc[NVARS][VARLOC];
	uint8_t loc_n[NVARS], runs[NVARS];
} strategy_t;

// --- базы. Постройка: type — facilities (NONE8 — пусто), xy = x | y << 4,
// days — дней до готовности (0 — готова).
typedef struct { uint8_t type, xy, days; } facility_t;

#define BF_BATTLE       0x01
#define BF_RETALIATION  0x02        // пришельцы знают о базе
typedef struct {
	char name[NAME_LEN];            // name[0] == 0 — запись свободна
	geo_t pos;
	facility_t fac[MAX_FACILITIES];
	uint16_t items[MAX_ITEMS];      // склад, по номерам items
	uint16_t scientists, engineers; // свободные (в работе — в проектах)
	uint8_t flags;                  // BF_*
} base_t;

// --- корабли (пул). status — CS_*; dest_kind — DK_*.
#define CS_READY     0
#define CS_OUT       1
#define CS_REPAIR    2
#define CS_REFUEL    3
#define CS_REARM     4
#define DK_NONE      0
#define DK_BASE      1
#define DK_UFO       2
#define DK_WAYPOINT  3
#define DK_SITE      4
#define DK_ALIENBASE 5
#define CRF_LOWFUEL  0x01
#define CRF_MISSION  0x02
#define CRF_BATTLE   0x04
typedef struct { uint8_t type; uint16_t ammo; uint8_t rearming; } cweapon_t;   // type — craftWeapons
typedef struct {
	uint8_t type;                   // crafts, NONE8 — свободно
	uint8_t base;                   // владелец (в пути — куда)
	uint16_t num;                   // номер своего типа: TRITON-1
	geo_t pos;
	uint8_t dest_kind, dest;
	uint16_t speed, fuel, damage;
	cweapon_t weap[2];
	uint8_t status, flags, order, takeoff;   // takeoff — шагов 5 с до начала движения
	uint8_t transit;                // часов до прибытия, 0 — на базе
	uint8_t cargo;                  // трюм или NONE8
	int32_t vlon, vlat;             // полёт (craft.c): вектор за шаг 5 с
	uint16_t steps;                 // целых шагов до цели
	uint8_t aim_n;                  // макрошагов с пересчёта курса
} craft_t;

typedef struct {
	struct { uint8_t item, qty; } it[CARGO_ITEMS];       // item NONE8 — пусто
	struct { uint8_t type; int16_t ammo; } veh[CARGO_VEH];
} cargo_t;

// --- поставки: предметы и персонал (солдаты/корабли — поле transit у себя)
#define TK_ITEM      1
#define TK_SCIENTIST 2
#define TK_ENGINEER  3
typedef struct { uint8_t base, hours, kind, item; uint16_t qty; } transfer_t;   // base NONE8 — свободно

typedef struct { uint8_t base, topic; uint16_t assigned, spent, cost; } research_t;
#define PF_INFINITE 0x01
#define PF_SELL     0x02
typedef struct { uint8_t base, manuf; uint16_t engineers; uint32_t spent; uint16_t amount; uint8_t flags; } prod_t;

// --- пришельцы (src/game/alien.c, ufo.c; 14_todo §3.6–3.9)
#define US_FLYING    0
#define US_LANDED    1
#define US_CRASHED   2
#define US_DESTROYED 3
#define UF_DETECTED  0x01
#define UF_HYPER     0x02
#define UF_BATTLE    0x04
#define ALT_GROUND   0               // Ufo::ALTITUDE_STRING: GROUND, VERY_LOW, LOW, HIGH, VERY_HIGH
typedef struct {
	uint8_t type, status;           // type — ufos, NONE8 — свободно; status — US_*
	uint16_t id, land_id, crash_id; // номера имён: UFO-n, Landing Site-n, Crash Site-n
	geo_t pos, dest;
	int32_t vlon, vlat;             // вектор за шаг 5 с (geo_aim)
	uint16_t steps;                 // целых шагов до цели, затем шаг прибытия
	uint16_t speed, damage;         // скорость — узлы
	uint8_t altitude, flags, dir;   // высота ALT_*, UF_*, курс 0..8 (geo_heading)
	uint32_t secs;                  // секунд на земле / до исчезновения обломков
	uint8_t mission, traj, traj_pt; // миссия (пул), траектория (ufoTrajectories), её точка
	uint8_t dest_base;              // штурм базы X-COM (__RETALIATION_ASSAULT_RUN) или NONE8
	uint8_t shot_by;                // корабль, сбивший НЛО (shotDownByCraftId), NONE8
	uint16_t fire_cd, escape_cd;
	uint8_t aim_n;                  // макрошагов с последнего пересчёта курса (ufo.c)
} ufo_t;

typedef struct {
	uint8_t type, region, race, next_wave, next_ufo;   // type (alienMissions) NONE8 — свободно
	uint16_t spawn_cd;              // минут до следующего НЛО (кратно 30)
	uint8_t live_ufos;
	uint16_t uid;
	uint8_t abase;                  // база пришельцев (SUPPLY) или NONE8
	int8_t site_zone;               // область зоны spawnZone для места миссии, -1 — нет
} mission_t;

#define AB_DISCOVERED 0x01
typedef struct { geo_t pos; uint16_t id; uint8_t race, deployment, flags; } alienbase_t;   // id 0 — свободно
#define SITE_DETECTED 0x01
typedef struct {
	geo_t pos; uint16_t id;         // id 0 — свободно
	uint8_t mission, deployment;    // тип миссии (alienMissions), развёртывание
	int8_t texture; uint16_t city;  // текстура области, город (строка) или NONE16
	uint32_t secs;                  // до исчезновения
	uint8_t race, flags;            // SITE_*
} site_t;
#define WP_PENDING 0xFFFF           // путевая точка ждёт подтверждения цели (ConfirmDestination)
typedef struct { geo_t pos; uint16_t id; } waypoint_t;   // id 0 — свободно

// Счётчики номеров объектов (_ids SavedGame)
#define ID_UFO        0
#define ID_WAYPOINT   1
#define ID_TERROR     2
#define ID_ALIENBASE  3
#define ID_SOLDIER    4
#define ID_MISSION    5
#define ID_CRASH      6
#define ID_LANDING    7
#define ID_ARTIFACT   8
#define ID_CRAFT      9             // + тип корабля (до 7 типов)
#define NIDS          16

#define END_NONE 0
#define END_WIN  1
#define END_LOSE 2

typedef struct {
	st_header_t hdr;
	uint8_t difficulty, ironman, ending, warned;
	int16_t months;                 // прошло месяцев; -1 — до первой базы
	uint32_t rng;
	uint8_t second, minute, hour, weekday, day, month;   // weekday 1..7 (воскресенье — 1)
	uint16_t year;
	uint8_t speed;                  // скорость времени 0..5 (интерфейс)
	uint8_t hist_len;               // 1..12 — длина всех историй
	geo_t globe;                    // центр глобуса (интерфейс)
	uint8_t zoom, sel_base;
	int32_t funds;
	uint16_t ids[NIDS];
	uint8_t discovered[RES_BITS];   // исследовано
	uint8_t popped[RES_BITS];       // «можно исследовать» уже показано
	uint8_t manuf_seen[MANUF_BITS];
	finance_t fin;
	country_t country[MAX_COUNTRIES];
	region_t region[MAX_REGIONS];
	strategy_t strategy;
	base_t base[MAX_BASES];
	craft_t craft[MAX_CRAFTS];
	cargo_t cargo[MAX_CARGO];
	transfer_t transfer[MAX_TRANSFERS];
	research_t research[MAX_RESEARCH];
	prod_t prod[MAX_PRODS];
	ufo_t ufo[MAX_UFOS];
	mission_t mission[MAX_MISSIONS];
	alienbase_t abase[MAX_ALIEN_BASES];
	site_t site[MAX_SITES];
	waypoint_t waypoint[MAX_WAYPOINTS];
	uint8_t nsoldiers;              // записей в странице солдат (с дырами)
	uint32_t graph_tgl[3];          // нажатые ряды графиков: регионы, страны, финансы (бит 31 — «Итого»)
	uint8_t nufos, nmissions, nsites;   // занятых записей ufo[] / mission[] / site[] (пустой мир — без циклов)
	uint8_t ncraft_out;             // кораблей со статусом CS_OUT (craft.c)
	uint8_t iron_slot;              // Ironman: слот сохранения (st_save запоминает; имя — hdr.name)
} state_t;

// --- солдат (страница STATE_PAGE + 1). base NONE8 — свободная запись.
#define SF_PSI      0x01            // на пси-тренировке
#define SF_FEMALE   0x80            // в look
typedef struct {
	char name[SOLDIER_NAME];
	uint16_t id;
	uint8_t base, craft;            // base — владелец (в пути — куда), craft — пул или NONE8
	uint8_t rank, armor, look, recovery;   // look: 0..3 | SF_FEMALE; recovery — дней лечения
	rstats_t init, cur;             // при найме / текущие (порядок rules.h)
	uint16_t missions, kills;
	uint8_t transit, flags, psi_improve, type;
	uint8_t pad[4];
} soldier_t;                        // 64 байта

_Static_assert(sizeof(st_header_t) == 44, "st_header_t");
_Static_assert(sizeof(state_t) <= 16384, "state_t must fit one page");
_Static_assert(sizeof(soldier_t) == 64, "soldier_t");
_Static_assert(sizeof(soldier_t) * MAX_SOLDIERS <= 16384, "soldiers must fit one page");

// При подключённой STATE_PAGE (см. выше). Переменная по абсолютному адресу
// (__at(0xC000), определена в screens.c), а не литеральный указатель
// (state_t *)0xC000: на нём SDCC 4.5 падает (validateOpType, SDCCopt.c) при
// любом обращении к полю-массиву с константным индексом (ST->base[0].flags).
extern __at(0xC000) state_t st_;
#define ST (&st_)
#define SOLDIER_PAGE (STATE_PAGE + 1)

#endif
