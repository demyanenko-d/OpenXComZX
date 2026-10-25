// Выстрел на экране (bfx.c, банк 32): полёт пули частицами и вспышка попадания.
#ifndef BFX_H
#define BFX_H

#include <stdint.h>

// Кадры частиц и вспышек для оружия в руках отряда (n бойцов, предметы — bitems.c) — в страницу
// TV_FXPG; зовётся при загрузке миссии после раскладки снаряжения
void fx_build(uint8_t page, uint8_t n) __banked;

// Выстрел (на стеке вызывающего): откуда и куда (воксели), оружие и обойма (#FFFF — нет)
typedef struct {
	uint8_t page;
	int16_t ox, oy, oz, ex, ey, ez;
	uint16_t weapon, ammo;
} fxshot_t;

// Пуля летит от ox.. до ex.. (Projectile::move, Map::drawTerrain), потом вспышка попадания
// (ExplosionBState, SMOKE.PCK); возвращается, когда анимация кончилась
void fx_shot(const fxshot_t *q) __banked;

#endif
