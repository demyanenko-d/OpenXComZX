// Видимость в бою (vision.c, банк 40): экран боя (банк 31) передаёт юнитов копией vu_t на стеке —
// его память банка отсюда не видна. Туман войны — там же (16 §5.2).
#ifndef VISION_H
#define VISION_H

#include <stdint.h>
#include "far.h"
#include "units.h"

#define VU_ALIVE 1
#define VU_BIG   2             // 2x2
#define VU_SEEN  4             // виден отряду
// Биты 3–7 — рост юнита в вокселях (стоя или на колене + парение, до 31): по нему глаза
// поднимаются на этаж (TileEngine::calculateFOV:255, getSightOriginVoxel:362)
#define VU_H(h) ((uint8_t)(((h) > 31 ? 31 : (h)) << 3))
typedef struct { uint8_t x, y, z, dir, flags; } vu_t;

// Маска обзора карты (байт на клетку) — в своих страницах пула; page — рабочая страница боя
// (свойства частей, loader.h / pathfind.h). 0 — страниц не нашлось (тогда видны все).
uint8_t vis_build(far_t cells, uint8_t sx, uint8_t sy, uint8_t sz, uint8_t page) __banked;
void vis_cell(far_t cells, uint8_t x, uint8_t y, uint8_t z, uint8_t page) __banked;   // клетка сменилась (дверь)
void vis_free(void) __banked;
// Пришельцы (u[nsold..n)), которых сейчас видит кто-то из отряда (u[0..nsold)): им ставится
// VU_SEEN, номера впервые замеченных — в list (до max); вернёт их число
uint8_t vis_seen(vu_t *u, uint8_t n, uint8_t nsold, uint8_t *list, uint8_t max) __banked;
// Кого из пришельцев видит юнит sel сейчас (getVisibleUnits): номера в list, вернёт их число
uint8_t vis_list(vu_t *u, uint8_t n, uint8_t nsold, uint8_t sel, uint8_t *list, uint8_t max) __banked;
// Видит ли кто-то из отряда (u[0..nsold)) юнита a сейчас — после шага и выстрела пришельца
uint8_t vis_by_xcom(vu_t *u, uint8_t nsold, uint8_t a) __banked;

// Туман войны (Tile::_discovered): байт на клетку в страницах vis_fog_page() подряд, номер клетки —
// (z * sy + y) * sx + x; PG_NONE — страниц не нашлось, видно всё. Сектора бойцов пересчитывает
// vis_seen (только сдвинувшихся и повернувшихся; после двери — всех).
#define FOG_W 1                // западная стена
#define FOG_N 2                // северная стена
#define FOG_C 4                // пол и содержимое (с ним — и обе стены)
#define FOG_L 8                // клетка уже отмечена линией (соседям стены выставлены) — виду не нужен
uint8_t vis_fog_page(void) __banked;
// Открытое с прошлого вызова: прямоугольник вида (мировые lo, hi, top, bot) — 1, ничего — 0
uint8_t vis_fog_rect(int16_t *r) __banked;
// Нарисованная область вида (r: lo, hi, top, bot; 0 — вся карта): вновь открытое вне её в
// прямоугольник не идёт — его нарисуют полосы, когда туда приедет камера
void vis_view(const int16_t *r) __banked;
// Открыть клетки прямоугольника на всех этажах (корабль отряда при высадке, BattlescapeGenerator)
void vis_reveal(uint8_t x0, uint8_t y0, uint8_t x1, uint8_t y1) __banked;
// revealedFloors блоков собранной карты (mapgen_reveal) — открыть сразу; звать после vis_build
void vis_blocks(void) __banked;
// Число открытых клеток для сценариев — слово в памяти банка 40: 0A:BCCE (vision.c vis_fog_n)

#endif
