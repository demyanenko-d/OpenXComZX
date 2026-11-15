// Пси-атака пришельца (bpsi.c, банк 35).
#ifndef BPSI_H
#define BPSI_H

#include <stdint.h>
#include "bai.h"

// Пришелец a пробует пси-атаку (psiAction): 1 — атаковал (время списано)
uint8_t ai_psi(const aictx_t *c, uint8_t a) __banked;

#endif
