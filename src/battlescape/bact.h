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

// Юнит i выбыл (alive уже 0): со сцены, его вещи и труп на пол (bfx.c), клетки перерисовать;
// n — юнитов всего
void ba_kill(uint8_t page, uint8_t i, uint8_t n) __banked;
// Мораль после гибели victim (checkForCasualties): убийца m (#FF — неизвестен), его и обе стороны
void ba_morale(uint8_t victim, uint8_t m, uint8_t n) __banked;

// Действие игрока предметом в руке по клетке (BattlescapeGame::primaryAction ->
// ProjectileFlyBState): выстрел (очередь — autoShots пуль) или бросок. Заполнить s (кроме weapon
// и ammo), руку и цену. Вернёт 0 — сделано, иначе строку предупреждения; #FFFF — в руке пусто
typedef struct {
	bashot_t s;
	uint8_t hand, tu;
} bafire_t;
uint16_t ba_fire(bafire_t *q) __banked;
// Опыт бойцов за бой (BattleUnit::_expFiring и т. д., bact2.c): счётчики бойца unit (< MAX_UNITS,
// только X-COM) в памяти банка 37 — читает разбор (bdeb.c, тот же банк)
#define EXP_FIRE   0           // попал по пришельцу (ProjectileFlyBState)
#define EXP_THROW  1           // бросил (addThrowingExp)
#define EXP_MELEE  2           // ударил пришельца (MeleeAttackBState)
#define EXP_REACT  3           // огонь в ответ (tryReaction)
#define EXP_BRAVE  4           // устоял от паники (prepareNewTurn)
#define EXP_KILLS  5           // убил (updateGeoscapeStats: kills)
#define EXP_N      6
#define BA_XP ((uint8_t *)0xB100)   // MAX_UNITS x EXP_N — только код банка 37
void ba_exp(uint8_t unit, uint8_t kind) __banked;
void ba_exp_reset(void) __banked;
// Отряд в корабле / снаружи (AbortMissionState): вернёт «в корабле», снаружи — в *out
uint8_t ba_in_craft(uint8_t nsold, uint8_t *out) __banked;
// Бегство паникующего i: путь (до max шагов) в path — Win1 или стек; вернёт число шагов
uint8_t ba_flee(uint8_t page, uint8_t i, uint8_t *path, uint8_t max) __banked;
// Паника в начале хода стороны side (bact2.c): кто запаниковал — номера в list (бит 7 — берсерк,
// бит 6 — бросил оружие и бежит)
uint8_t ba_panic(uint8_t page, uint8_t side, uint8_t n, uint8_t *list, uint8_t max) __banked;
// Начало хода стороны side: оглушение −1, очнувшиеся встают (bact2.c). Вернёт, сколько встало
uint8_t ba_stun(uint8_t page, uint8_t side, uint8_t n) __banked;
// Выстрел берсерка s.shooter (карта и n в s): 1 — выстрелил, 0 — больше не может
uint8_t ba_berserk(bafire_t *q) __banked;

#endif
