// Пси-атака пришельца (bpsi.c, банк 35).
#ifndef BPSI_H
#define BPSI_H

#include <stdint.h>
#include "bai.h"

// Пришелец a пробует пси-атаку (psiAction): 1 — атаковал (время списано)
uint8_t ai_psi(const aictx_t *c, uint8_t a) __banked;
// Пси-атака бойца a (act — BA_PANIC / BA_MINDCONTROL) по юниту в клетке: строка итога (InfoboxState),
// STR_NOT_ENOUGH_TIME_UNITS или 0
uint16_t psi_player(uint8_t a, uint8_t tx, uint8_t ty, uint8_t tz, uint8_t act, uint8_t tu, uint8_t n) __banked;

#endif
