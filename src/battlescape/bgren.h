// Гранаты (bgren.c, банк 38): окно таймера, взвод.
#ifndef BGREN_H
#define BGREN_H

#include <stdint.h>

void gr_draw(void) __banked;                       // окно таймера PrimeGrenadeState целиком
uint8_t gr_hit(int16_t x, int16_t y) __banked;     // кнопка таймера под точкой (#FF — мимо)
// Граната в руке взводится через окно таймера (0 — бесконтактная: сразу таймер 0)
uint8_t gr_window(uint8_t page, uint8_t unit, uint8_t right) __banked;
void gr_prime(uint8_t page, uint8_t unit, uint8_t right, uint8_t fuse) __banked;   // setFuseTimer

#endif
