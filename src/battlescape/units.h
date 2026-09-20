// Бойцы миссии из состояния кампании (units.c, банк поиска пути: банк боя переполнен).
#ifndef BAT_UNITS_H
#define BAT_UNITS_H

#include <stdint.h>
#include "far.h"

// Боец на карте: клетка и показатели, как их берёт BattleUnit оригинала из солдата базы
typedef struct { uint8_t x, y, z, tu, hp, en, mor; } cunit_t;

// Что нужно знать о миссии, чтобы высадить отряд
typedef struct {
	far_t cells;            // клетки карты (4 байта на клетку)
	uint8_t sx, sy;         // размер карты в клетках
	uint8_t cx, cy, cw, cl; // прямоугольник корабля на карте (cw == 0 — корабля нет)
	uint8_t part_min;       // с какого номера идут части корабля: по его полу ищутся места
	uint16_t craft;         // корабль миссии (#FFFF — бой отладочный, экипажа нет)
	uint8_t debug_n;        // сколько фигур ставить в отладочном бою
} crew_req_t;

// Высадить отряд: клетки и показатели в out, вернёт число бойцов.
// out — буфер вызывающего на стеке: страница банка вызывающего при вызове отключена.
uint8_t crew_deploy(const crew_req_t *q, cunit_t *out, uint8_t max) __banked;

#endif
