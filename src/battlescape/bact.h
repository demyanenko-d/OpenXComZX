// Действия юнитов в бою (bact.c, банк 36): то, что делают и боец по команде, и пришелец в свой
// ход (ИИ). Юниты — дальним доступом (units.h UNITS_FAR).
#ifndef BACT_H
#define BACT_H

#include <stdint.h>
#include "far.h"

// Выстрел (на стеке вызывающего): карта, кто стреляет и куда, действие и меткость, оружие и
// вставленная обойма (#FFFF — нет)
typedef struct {
	uint8_t page;
	far_t cells;
	uint8_t sx, sy, sz;
	uint8_t shooter, n;
	uint8_t tx, ty, tz;
	uint8_t action, acc;
	uint16_t weapon, ammo;
} bashot_t;

// Одна пуля (ProjectileFlyBState -> ExplosionBState -> TileEngine::hit): траектория, полёт и
// вспышка, урон и мораль; выбывший — со сцены, труп на пол. Вернёт номер раненого (#FF — промах)
uint8_t ba_shot(const bashot_t *q) __banked;

// Действие игрока предметом в руке по клетке (BattlescapeGame::primaryAction ->
// ProjectileFlyBState): выстрел (очередь — autoShots пуль) или бросок. Заполнить s (кроме weapon
// и ammo), руку и цену. Вернёт 0 — сделано, иначе строку предупреждения; #FFFF — в руке пусто
typedef struct {
	bashot_t s;
	uint8_t hand, tu;
} bafire_t;
uint16_t ba_fire(bafire_t *q) __banked;

#endif
