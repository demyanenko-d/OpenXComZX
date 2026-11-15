// Разбор боя (bdeb.c, банк 37): DebriefingState — очки миссии и оценка.
#ifndef BDEB_H
#define BDEB_H

#include <stdint.h>
#include "ui.h"

// Итог боя (на стеке вызывающего): юнитов всего и бойцов (идут первыми), прервали ли, цель — НЛО,
// корабль отряда (#FFFF — отладочный бой, кампании не касается)
typedef struct {
	uint8_t page, n, nsold, aborted, ufo;
	uint16_t craft;
} debreq_t;

// Подсчитать очки (prepareDebriefing) по юнитам и предметам боя и применить итог к кампании
void deb_start(const debreq_t *q) __banked;
// Конец фазы многофазной миссии: очки пришельцев и мирных фазы копятся до разбора (won — пришельцы
// фазы перебиты: трофеи — на склад сразу)
void deb_stage(const debreq_t *q, uint8_t won) __banked;
// Очки копятся с прошлых фаз (память банка 37; обнуляет ba_exp_reset — начало боя)
#define DEB_ACC (*(volatile uint8_t *)0xB047)

// Экран SCR_DEBRIEFING (раздаёт экран боя)
uint8_t deb_get(uint8_t id, sdef_t *s, wdef_t *w) __banked;
void deb_text(uint8_t id, uint8_t slot, uint8_t row, char *buf) __banked;
uint8_t deb_rows(uint8_t id, uint8_t slot) __banked;
uint8_t deb_event(uint8_t id, uint8_t ev, uint8_t arg) __banked;

#endif
