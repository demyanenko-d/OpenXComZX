// Ход пришельцев (bai.c, банк 36): ИИ по тактам экрана боя.
#ifndef BAI_H
#define BAI_H

#include <stdint.h>
#include "far.h"

// Миссия для ИИ (на стеке вызывающего): карта, юнитов всего и бойцов X-COM (идут первыми)
typedef struct {
	uint8_t page;
	far_t cells;
	uint8_t sx, sy, sz, n, nsold;
} aictx_t;

#define AI_DONE 1              // все пришельцы походили — ход X-COM
#define AI_SEE  2              // пришелец сдвинулся или стрелял — пересчитать обзор отряда

void ai_start(void) __banked;               // начало хода пришельцев
uint8_t ai_tick(const aictx_t *c) __banked; // такт хода: AI_*

#endif
