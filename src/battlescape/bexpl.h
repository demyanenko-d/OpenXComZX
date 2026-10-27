// Взрывы (bexpl.c, банк 39): гранаты в конце хода, площадной взрыв на экране и урон по площади.
#ifndef BEXPL_H
#define BEXPL_H

#include <stdint.h>
#include "far.h"

// Карта боя и юниты (на стеке вызывающего)
typedef struct {
	uint8_t page;              // рабочая страница боя
	far_t cells;
	uint8_t sx, sy, sz;
	uint8_t n;                 // юнитов (units.h UNITS_FAR)
} exreq_t;

// BattlescapeGame::endTurn: гранаты на земле с таймером 0 взрываются (по одной, камера на
// взрыв), таймеры остальных гранат — на ход меньше. 1 — что-то взорвалось (обзор и конец боя —
// заново)
uint8_t ex_turn(const exreq_t *q) __banked;

#endif
