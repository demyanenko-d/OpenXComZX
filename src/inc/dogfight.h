// Воздушный (подводный) бой: общие данные логики (src/geoscape/earth/dogfight.c, банк 22) и
// окон (src/geoscape/earth/scr_dogf.c, банк 23). Бои — в Win1 (не сохраняются, как в OpenXcom).
// Внешний интерфейс для геоскейпа и кораблей — game.h (df_*).
#ifndef DOGFIGHT_H
#define DOGFIGHT_H

#include <stdint.h>
#include "game.h"

#define STANDOFF    560
#define CWPT_BEAM   4                    // CWPT_LASER_BEAM и дальше — лучи (иначе ракеты)
#define CWPT_PLASMA 5
#define D_UP        1
#define D_DOWN      2
#define PF_MISSED   1
#define PF_REMOVE   2
#define NPROJ       8

typedef struct {
	uint8_t type;                        // CWPT_*, NONE8 — свободно
	uint8_t dir, flags, state;           // D_*, PF_*, луч: 8 -> 4 -> 2 -> 1
	int8_t hpos;                         // HP_LEFT -1 / HP_CENTER 0 / HP_RIGHT 1
	uint8_t speed, acc, range;
	int16_t pos;
	uint16_t covered, dmg;
} proj_t;

typedef struct {
	uint8_t range, acc, ptype, pspeed;
	uint8_t reload[3];                   // осторожный, стандартный, агрессивный
	uint8_t sprite;
	uint16_t damage;
} wrule_t;

#define DFF_MIN       0x0001             // свёрнут
#define DFF_END       0x0002             // _end
#define DFF_OVER      0x0004             // _endDogfight
#define DFF_KILLUFO   0x0008
#define DFF_KILLCRAFT 0x0010
#define DFF_BREAK     0x0020             // _ufoBreakingOff
#define DFF_W1OFF     0x0040             // оружие выключено (weapon1Click)
#define DFF_W2OFF     0x0080
#define DFF_ANIMHIT   0x0100
#define DFF_WAITPOLY  0x0200
#define DFF_WAITALT   0x0400
#define DFF_PREVIEW   0x0800             // вид НЛО спереди (btnUfoClick)

#define DR_BATTLE 0x01                   // перерисовать за тик
#define DR_DIST   0x02
#define DR_STATUS 0x04
#define DR_DAMAGE 0x08
#define DR_BTN    0x10
#define DR_AMMO   0x20                   // оружие и боезапас (выстрел, выключено)

typedef struct {
	uint8_t craft, ufo;                  // craft NONE8 — свободно
	uint8_t num;                         // _interceptionNumber 1..4
	uint8_t mode, omode;                 // режим 0..4 (standoff … disengage); нарисованный
	uint16_t flags;
	int16_t dist, target;
	uint8_t timeout;
	uint16_t status;                     // строка состояния, NOSTR — пусто
	uint8_t wcd[2], wint[2];             // отсчёт и интервал огня
	int8_t size;                         // _ufoSize (при крушении пятно тает)
	uint8_t hit;                         // hitFrame
	uint8_t height, dcolor, dtimer;      // силуэт повреждений: высота (NONE8 — не измерена), цвет, мигание
	uint8_t dirty;
	int16_t x, y;                        // окно
	uint8_t nweap, csprite;              // правила корабля и НЛО
	int8_t calt;                         // maxAltitude (> -1 — только под водой)
	uint16_t cdmax, csmax;
	uint16_t udmax, usmax, upower, ureload;
	uint8_t urange, usprite;
	int16_t uscore;
	wrule_t w[2];
	proj_t p[NPROJ];
} dogfight_t;

extern dogfight_t df[DF_MAX];
extern uint8_t df_ticks;                 // тиков в последнем df_run
// цвета элементов dogfight (interfaces.rul), tftd — раскладка TFTD
extern uint8_t c_craft0, c_craft1, c_radar0, c_radar1, c_dmg0, c_dmg1, c_blob, c_meter;
extern uint8_t c_disw, c_disr, c_disa, c_num, c_dist, c_text, c_minnum, c_btn, tftd;

// логика (банк 22) для окон
void df_press(uint8_t k, uint8_t m) __banked;      // кнопка режима 0..4
uint8_t df_minimize(uint8_t k) __banked;           // btnMinimizeClick: 1 — свёрнут
uint8_t df_maximize(uint8_t k) __banked;           // btnMinimizedIconClick: 1 — развёрнут, 2 — окно ошибки
void df_debug(void) __banked;                      // отладочный бой без настоящих объектов

#endif
