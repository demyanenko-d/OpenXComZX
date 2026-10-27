// Выстрел на экране (bfx.c, банк 32): полёт пули частицами и вспышка попадания.
#ifndef BFX_H
#define BFX_H

#include <stdint.h>

// Кадры частиц и вспышек для оружия в руках n первых юнитов (бойцы и пришельцы) — в страницу
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

// Гибель юнита unit: его предметы и труп (armors corpseBattle, у крупного — по четверти на клетку)
// на пол клеток (x, y, z) записями bitem_t (BI_DROP); виду — перечитать (bv_sync) и перерисовать
void fl_corpse(uint8_t page, uint8_t unit, uint8_t x, uint8_t y, uint8_t z, uint8_t size, uint16_t armor) __banked;

// Предмет — запись bitem_t i — на пол клетки (упал в бою: BI_DROP); виду — перечитать (bv_sync)
void fl_drop(uint8_t page, uint16_t i, uint8_t x, uint8_t y, uint8_t z) __banked;
// Предметы на полу заново по записям (что-то с земли исчезло); виду — перечитать (bv_sync)
void fl_rebuild(uint8_t page) __banked;

// Брошенный предмет (на стеке вызывающего): точка вылета и дуга (bfire.h throw_t), что летит
typedef struct {
	uint8_t page;
	int16_t ox, oy, oz, dx, dy, dz;
	uint16_t r16, z16, n, item;
} fxthrow_t;

// Полёт брошенного по дуге; возвращается, когда долетел
void fx_throw(const fxthrow_t *q) __banked;

#endif
