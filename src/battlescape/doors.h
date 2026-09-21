// Клетки карты боя: двери и цена прохода (doors.c, банк 30 — в банке боя места нет).
#ifndef DOORS_H
#define DOORS_H

#include <stdint.h>
#include "far.h"

// Карта миссии: где лежат клетки и её размеры. Свойства частей (цена прохода, замена
// для двери) лежат в странице поиска пути — pathfind.h, их заполняет экран боя.
typedef struct {
	far_t cells;
	uint8_t sx, sy;
	uint8_t page;
} dmap_t;

// Направление на клетку (BattleUnit::directionTo): 0 — север, дальше по часовой
uint8_t dir_to(int16_t dx, int16_t dy) __banked;

// Цена входа в клетку: пол плюс объект, наискось в полтора раза (Pathfinding::getTUCost).
// step — цена по умолчанию, когда у частей она не задана.
uint8_t cell_cost(const dmap_t *m, uint8_t x, uint8_t y, uint8_t z, uint8_t d, uint8_t step) __banked;

// Открыть дверь в направлении dir от клетки (x, y, z) — порт TileEngine::unitOpensDoor
// для бойца 1x1. rclick — правая кнопка (смотрит ещё и соседние стены, как поворот
// на месте в оригинале). Возвращает 0 — открыли, 1 — не хватило времени, 2 — двери нет.
// Потраченные единицы времени — в *spent, клетки к перерисовке (пары x, y) — в list.
uint8_t door_open(const dmap_t *m, uint8_t x, uint8_t y, uint8_t z, uint8_t dir, uint8_t rclick,
		  uint8_t tu, uint8_t *spent, uint8_t *list, uint8_t maxcells) __banked;

// Найти первую дверь на этаже (отладка боя, клавиша D): клетка и слот стены.
// 1 — нашли. Обход как у отрисовки: ряды Y, внутри ряда X.
uint8_t door_find(const dmap_t *m, uint8_t z, uint8_t *x, uint8_t *y, uint8_t *slot) __banked;

#endif
