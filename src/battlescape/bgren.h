// Гранаты (bgren.c, банк 38): окно таймера, взвод, дуга броска.
#ifndef BGREN_H
#define BGREN_H

#include <stdint.h>
#include "bfire.h"

void gr_draw(void) __banked;                       // окно таймера PrimeGrenadeState целиком
uint8_t gr_hit(int16_t x, int16_t y) __banked;     // кнопка таймера под точкой (#FF — мимо)
// Граната в руке взводится через окно таймера (0 — бесконтактная: сразу таймер 0)
uint8_t gr_window(uint8_t page, uint8_t unit, uint8_t right) __banked;
void gr_prime(uint8_t page, uint8_t unit, uint8_t right, uint8_t fuse) __banked;   // setFuseTimer

// Бросок (на стеке вызывающего): сила бросающего, на колене ли, вес предмета (кривизна) и вес с
// обоймой (дальность), меткость броска %. Результат: клетка, куда упадёт, и дуга для полёта на
// экране — точка i = o + i · (dx, dy, dz) · 16 / r16 + подъём 4 · zA · i · (ro - i) / ro²
// (zA = z16 / 16, ro = r16 / 16), n шагов; o — точка вылета (shot_t ox..)
typedef struct {
	uint8_t strength, kneel, weight, wtotal, acc;
	uint8_t lx, ly, lz;
	int16_t dx, dy, dz;
	uint16_t r16, z16, n;
} throw_t;

// Projectile::calculateThrow с проверками ProjectileFlyBState::validThrowRange и
// TileEngine::validateThrow. Вернёт 0 — бросок есть, 1 — далеко (STR_OUT_OF_RANGE), 2 — некуда
// (STR_UNABLE_TO_THROW_HERE)
int8_t gr_throw(shot_t *s, const fu_t *u, throw_t *t) __banked;

#endif
