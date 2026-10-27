// Выстрел (bfire.c, банк 35): траектория в вокселях и урон. Юниты — копией fu_t на стеке.
#ifndef BFIRE_H
#define BFIRE_H

#include <stdint.h>
#include "far.h"

// VoxelType (TileEngine.h)
#define V_EMPTY     -1
#define V_FLOOR      0
#define V_WESTWALL   1
#define V_NORTHWALL  2
#define V_OBJECT     3
#define V_UNIT       4
#define V_OUTOFBOUNDS 5

#define FU_ALIVE 1
#define FU_BIG   2
#define FU_KNEEL 4
typedef struct {
	uint8_t x, y, z, flags;
	uint8_t height;            // getHeight(): стоя или на колене
	uint16_t armor;            // правило armors: маски вокселей (loftempsSet), модификаторы урона
} fu_t;

typedef struct {
	uint8_t page;              // рабочая страница боя (LOFT частей — в третьей, loader.h TV_LOFT)
	far_t cells;
	uint8_t sx, sy, sz;
	uint8_t shooter, n;        // кто стреляет; юнитов в массиве
	uint8_t tx, ty, tz;        // клетка-цель
	uint8_t action;            // BA_* (bmenu.h)
	uint8_t acc;               // меткость, % (getFiringAccuracy)
	uint16_t weapon;           // правило items оружия
	uint8_t dir;               // направление стрелка на цель (getDirectionTo)
	// результат
	int8_t res;                // V_*: во что попали
	int16_t vx, vy, vz;        // воксель попадания
	int16_t ox, oy, oz;        // откуда летит (getOriginVoxel)
	uint8_t hit;               // юнит, в которого попали (#FF — нет)
} shot_t;

// Траектория выстрела (Projectile::calculateTrajectory): разброс по меткости и линия до первого
// непустого вокселя. Вернёт res.
int8_t fire_shot(shot_t *s, const fu_t *u) __banked;

// Для броска (bgren.c): таблицы z, маски юнитов и точка вылета (TileEngine::getOriginVoxel; у
// броска — на 3 ниже макушки, у выстрела — на 4) в s->ox..; 0 — нет масок LOFTEMPS. s и u
// должны жить до последнего fire_line
uint8_t fire_setup(shot_t *s, const fu_t *u) __banked;
// Projectile::applyAccuracy без поправки на дальность: разброс точки (*x, *y, *z) по меткости acc %
// и вектору (dx, dy, dz) от точки вылета
void fire_spread(int16_t acc, int16_t dx, int16_t dy, int16_t dz, int16_t *x, int16_t *y, int16_t *z) __banked;
// Отрезок дуги p[0..2] -> p[3..5]: линия с проверкой вокселей (calculateLine); first — первый
// отрезок дуги. Вернёт V_*; точка, куда упадёт предмет (основная точка линии перед ударом,
// Projectile::ItemDropVoxelOffset), — в land[0..2]
int8_t fire_line(const int16_t *p, uint8_t first, int16_t *land) __banked;

typedef struct {
	uint16_t power;            // мощность (до разброса DAMAGE_RANGE)
	uint8_t type;              // ItemDamageType
	int16_t rx, ry, rz;        // точка попадания относительно юнита (TileEngine::hit)
	uint8_t dir, height;       // направление юнита, его рост
	uint16_t armor_rule;       // armors: модификатор урона по типу
	uint8_t armor[5];          // текущая броня по сторонам (меняется)
	uint8_t hp, stun;          // меняются
} dmg_t;

// BattleUnit::damage с разбросом мощности TileEngine::hit; вернёт нанесённый урон
uint8_t fire_damage(dmg_t *d) __banked;

#endif
