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
#define AI_HIDDEN 4            // действует пришелец, которого отряд не видит: «Hidden Movement»
#define AI_HIT  8              // стрелял: панели — показатели бойца заново

void ai_start(void) __banked;               // начало хода пришельцев
uint8_t ai_tick(const aictx_t *c) __banked; // такт хода: AI_*
// Огонь в ответ на действие юнита m (его сторона ходит): AI_SEE | AI_HIT, если кто-то стрелял
uint8_t ai_react(const aictx_t *c, uint8_t m) __banked;

#endif
