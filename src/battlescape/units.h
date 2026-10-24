// Бойцы миссии из состояния кампании (units.c, банк поиска пути: банк боя переполнен).
#ifndef BAT_UNITS_H
#define BAT_UNITS_H

#include <stdint.h>
#include "far.h"

// Боец на карте: клетка и показатели, как их берёт BattleUnit оригинала из солдата базы
typedef struct { uint8_t x, y, z, tu, hp, en, mor; } cunit_t;

// Боец экрана боя (scr_battle.c, память банка 28): клетка, направление, жив, на колене, показатели
#define MAX_UNITS 12            // бойцов X-COM
#define MAX_BUNITS 40           // юнитов миссии всего (бойцы, пришельцы, мирные)
#define FACTION_PLAYER  0
#define FACTION_HOSTILE 1
#define FACTION_NEUTRAL 2
typedef struct {
	uint8_t x, y, z, dir, alive, kneel;
	uint8_t tu, tu_max, hp, hp_max, en, mor;
	uint8_t faction;           // FACTION_* (BattleUnit::_faction)
	uint8_t sheet;             // 0 — лист бойца X-COM по частям, иначе собранный лист (loader.h TL_ASH)
	uint8_t size;              // клеток по стороне (Armor::getSize)
	uint8_t rank;              // ранг пришельца (alienRank) — по нему узлы расстановки
	uint8_t en_max, stun;
	uint8_t armor[5];          // броня: спереди, слева, справа, сзади, снизу (BattleUnit::_currentArmor)
	uint8_t reactions, firing, throwing, strength, bravery, psi_str, psi_skill, melee;
	uint8_t fly;               // movementType: 1 — летает (MT_FLY)
	uint16_t type;             // units (пришельцы, мирные), #FFFF — боец X-COM
	uint8_t seen;              // виден стороне X-COM
	uint16_t node;             // узел маршрута, на котором поставлен (#FFFF — нет)
	uint8_t en_rec;            // энергии за ход (BattleUnit::recoverTimeUnits)
} unit_t;

// Загрузка миссии для экрана боя (bload.c): генератор карты, наборы частей, листы бойца и
// курсора, отряд, стрелка выбранного. Таблицы — в рабочей странице (loader.h TL_*), экран
// переносит их к себе. Банк боя 28 переполнен — загрузка живёт здесь.
typedef struct {
	uint16_t terrain, deploy, ufo, craft, crew;   // миссия (#FFFF — не задано: бой отладочный)
	uint8_t page;                                 // рабочая страница боя (вторая — page + 1)
	uint8_t sx, sy, sz, nparts, ndoors, nunits;   // результат
	far_t cells;
} bload_t;
uint8_t bat_load(bload_t *q) __banked;          // 1 — карта собрана
void bat_unload(void) __banked;                 // отдать страницы наборов, листов и генератора

// Что нужно знать о миссии, чтобы высадить отряд
typedef struct {
	far_t cells;            // клетки карты (4 байта на клетку)
	uint8_t sx, sy;         // размер карты в клетках
	uint8_t cx, cy, cw, cl; // прямоугольник корабля на карте (cw == 0 — корабля нет)
	uint8_t part_min;       // с какого номера идут части корабля: по его полу ищутся места
	uint16_t craft;         // корабль миссии (#FFFF — бой отладочный, экипажа нет)
	uint8_t debug_n;        // сколько фигур ставить в отладочном бою
} crew_req_t;

// Высадить отряд: клетки и показатели в out, вернёт число бойцов.
// out — буфер вызывающего на стеке: страница банка вызывающего при вызове отключена.
uint8_t crew_deploy(const crew_req_t *q, cunit_t *out, uint8_t max) __banked;

// Панель боя (BattlescapeState.cpp:147-155): картинка ICONS и показатели выбранного
// бойца. Рисуется здесь, а не в банке боя, — тот забит отрисовкой карты. n == 0 —
// отряда нет, рисуется одна панель.
typedef struct { uint8_t n, tu, tu_max, en, hp, hp_max, mor, level; } bpanel_t;
void bat_panel(const bpanel_t *p) __banked;

#endif
