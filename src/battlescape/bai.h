// Ход пришельцев (bai.c, банк 36): ИИ по тактам экрана боя.
#ifndef BAI_H
#define BAI_H

#include <stdint.h>
#include "far.h"
#include "units.h"

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

// Что пришельцы знают об остальных (BattleUnit::_turnsSinceSpotted): ходов с тех пор, как юнита
// видел кто-то из пришельцев (255 — никогда). ИИ целится только в тех, у кого счёт не больше
// его intelligence (AIModule::validTarget). Память банка 36 — только для кода банка (bact.c)
#define ai_tss ((uint8_t *)0xB040)   // MAX_BUNITS байт с #B040 (extern с __at SDCC не сводит)
void ai_tss_init(void) __banked;            // начало боя: никого не видели
// Начало хода X-COM (SavedBattleGame::endTurn:884-901): счёт +1, с хода cheatTurn (или его
// половины при двух пришельцах и меньше) пришельцы знают всех бойцов; и обзор пришельцев заново
void ai_round(const aictx_t *c, uint8_t turn) __banked;
// Юнит m (не пришелец) сдвинулся или повернулся: видит ли его кто-то из пришельцев (calculateFOV)
void ai_spot(const aictx_t *c, uint8_t m) __banked;
void ai_start(const aictx_t *c) __banked;   // начало хода пришельцев (их обзор заново)
uint8_t ai_tick(const aictx_t *c) __banked; // такт хода: AI_*
// Огонь в ответ на действие юнита m (его сторона ходит): AI_SEE | AI_HIT, если кто-то стрелял
uint8_t ai_react(const aictx_t *c, uint8_t m) __banked;

#endif
