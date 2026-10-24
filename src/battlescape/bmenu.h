// Меню действий с предметом в руке (bmenu.c, банк 33): ActionMenuState / ActionMenuItem.
// Пункты меню живут в памяти банка 33 — снаружи их видно только через функции.
#ifndef BMENU_H
#define BMENU_H

#include <stdint.h>

// BattleActionType (BattlescapeGame.h)
#define BA_NONE      0
#define BA_PRIME     3
#define BA_THROW     4
#define BA_AUTOSHOT  5
#define BA_SNAPSHOT  6
#define BA_AIMEDSHOT 7
#define BA_HIT       8
#define BA_USE       9
#define BA_LAUNCH    10
#define BA_MINDCONTROL 11
#define BA_PANIC     12

// Кто действует: показатели для меткости и цены (BattleUnit::getFiringAccuracy, getActionTUs)
typedef struct {
	uint8_t unit, right;       // юнит и рука
	uint8_t tu_max, firing, throwing, melee, psi_skill;
	uint8_t hp, hp_max, kneel;
	uint8_t page;              // рабочая страница боя (предметы)
} amreq_t;

uint8_t am_build(const amreq_t *r) __banked;       // собрать пункты; 0 — в руке пусто
void am_draw(uint8_t hl) __banked;                 // все пункты; hl — под курсором (#FF — нет)
void am_hover(uint8_t old, uint8_t now) __banked;  // перерисовать два пункта: покинутый и новый
uint8_t am_hit(int16_t x, int16_t y) __banked;     // пункт под точкой (#FF — мимо)
uint8_t am_action(uint8_t i, uint8_t *tu) __banked;   // действие пункта (BA_*) и его цена

#endif
