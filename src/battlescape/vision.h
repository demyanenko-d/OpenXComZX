// Видимость в бою (vision.c, банк 32): экран боя (банк 31) передаёт юнитов копией vu_t на стеке —
// его память банка отсюда не видна.
#ifndef VISION_H
#define VISION_H

#include <stdint.h>
#include "far.h"
#include "units.h"

#define VU_ALIVE 1
#define VU_BIG   2             // 2x2
#define VU_SEEN  4             // виден отряду
typedef struct { uint8_t x, y, z, dir, flags; } vu_t;

// Маска обзора карты (байт на клетку) — в своих страницах пула; page — рабочая страница боя
// (свойства частей, loader.h / pathfind.h). 0 — страниц не нашлось (тогда видны все).
uint8_t vis_build(far_t cells, uint8_t sx, uint8_t sy, uint8_t sz, uint8_t page) __banked;
void vis_cell(far_t cells, uint8_t x, uint8_t y, uint8_t z, uint8_t page) __banked;   // клетка сменилась (дверь)
void vis_free(void) __banked;
// Пришельцы (u[nsold..n)), которых сейчас видит кто-то из отряда (u[0..nsold)): им ставится
// VU_SEEN, номера впервые замеченных — в list (до max); вернёт их число
uint8_t vis_seen(vu_t *u, uint8_t n, uint8_t nsold, uint8_t *list, uint8_t max) __banked;

#endif
