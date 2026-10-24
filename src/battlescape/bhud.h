// Панель боя (bhud.c, банк 33): ICONS и всё, что BattlescapeState::updateSoldierInfo рисует поверх.
#ifndef BHUD_H
#define BHUD_H

#include <stdint.h>

#define RES_NONE      0        // reserve: BA_NONE — не резервировать время
#define RES_SNAP      1
#define RES_AUTO      2
#define RES_AIMED     3

typedef struct {
	uint8_t on;                // выбран свой боец (playableUnitSelected): иначе — только панель
	uint8_t level;             // этаж камеры (numLayers)
	uint8_t tu, tu_max, en, en_max, hp, hp_max, stun, mor;
	uint8_t sol;               // запись солдата кампании: имя и звание (#FF — нет)
	uint8_t unit;              // юнит: предметы в руках (bitems.h)
	uint8_t kneel;             // на колене: кнопка «присесть» нажата (toggleKneelButton)
	uint8_t reserve, res_kneel;   // резерв времени (RES_*) и «на колено» — нажатые кнопки
	uint8_t page;              // рабочая страница боя
} bhud_t;

void hud_draw(const bhud_t *h) __banked;

// WarningMessage: плашка 224x24 в (48, 176) поверх панели, через 1.2 с пропадает
void hud_warn(uint16_t str) __banked;
uint8_t hud_warn_tick(void) __banked;      // 1 — плашка исчезла: перерисовать панель

// BattlescapeGame::checkReservedTU (сторона игрока): хватит ли времени на ход ценой tu, не тратя
// резерв на выстрел оружием из рук и на приседание. 0 — нельзя (плашка уже показана).
typedef struct {
	uint8_t unit, page, tu_max, tu, kneeled, reserve, res_kneel, hand;
} rsreq_t;
uint8_t hud_reserve(const rsreq_t *r, uint8_t cost) __banked;

#endif
