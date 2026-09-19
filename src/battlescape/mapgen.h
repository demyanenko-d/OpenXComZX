// Генератор наземной миссии (mapgen.c): собирает карту из блоков террейна по mapScript.
#ifndef MAPGEN_H
#define MAPGEN_H

#include <stdint.h>
#include "far.h"

// Собрать карту: terrain — номер в таблице TERRAINS, mods — сторона поля в модулях 10x10,
// levels — этажей. 1 — получилось; карта лежит в выделенных страницах (mapgen_cells).
uint8_t mapgen_run(uint16_t terrain, uint8_t mods, uint8_t levels) __banked;

uint16_t mapgen_sx(void) __banked;
uint16_t mapgen_filled(void) __banked;   // непустых клеток (проверка укладки)
uint16_t mapgen_sy(void) __banked;
uint8_t mapgen_sz(void) __banked;
far_t mapgen_cells(void) __banked;
// Наборы MCD террейна по порядку (нужны, чтобы собрать таблицу частей и тайлсет)
uint8_t mapgen_sets(uint16_t *set, uint8_t *count) __banked;

#endif
