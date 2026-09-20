// Поиск пути по клеткам боя (pathfind.c, свой банк).
#ifndef PATHFIND_H
#define PATHFIND_H

#include <stdint.h>
#include "far.h"

// Смещения восьми направлений: 0 — север, дальше по часовой (как в оригинале)
extern const int8_t pf_dx[8], pf_dy[8];

typedef struct {
	far_t cells;            // клетки карты (4 байта на клетку)
	uint8_t sx, sy;         // размер карты в клетках
	uint8_t z;              // этаж
	uint8_t fx, fy;         // откуда
	uint8_t tx, ty;         // куда
	uint8_t budget;         // сколько единиц времени можно потратить
	uint8_t step;           // цена шага
	uint8_t page;           // страница под рабочие массивы (16 КБ)
} pf_req_t;

// Ищет путь; возвращает число шагов (0 — не дошли), направления пишет в path.
uint8_t pf_find(const pf_req_t *q, uint8_t *path, uint8_t maxlen) __banked;

#endif
