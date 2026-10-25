// Вид наземного боя (bview.c, банк 28): холсты карты, дисплей-лист, камера, курсор клетки,
// стрелка над выбранным, фазы ходьбы. Логика боя (scr_battle.c, банк 31) говорит виду, что
// изменилось, а юнитов передаёт записями runit_t через рабочую страницу (loader.h TL_RUNITS).
#ifndef BVIEW_H
#define BVIEW_H

#include <stdint.h>
#include "far.h"

// Юнит для отрисовки (8 байт): клетка, направление, флаги, лист кадров
#define RU_ALIVE   1           // рисовать (жив и виден)
#define RU_KNEEL   2           // на колене
#define RU_BIG     4           // 2x2 клетки: x, y — северный угол, каждая клетка рисует свою четверть
typedef struct {
	uint8_t x, y, z, dir, flags;
	uint8_t sheet;             // 0 — лист бойца X-COM по частям (TL_SHEET), иначе собранный лист
	uint8_t pad[2];            //   (loader.h TL_ASH: кадры листа s — с записи (s - 1) * ASH_N)
} runit_t;

// Что нужно виду о миссии (заполняет логика после bat_load)
typedef struct {
	far_t cells;
	uint8_t sx, sy, sz;
	uint8_t page;              // рабочая страница боя (таблицы — во второй, loader.h)
} bvload_t;

void bv_open(void) __banked;                 // холсты, сплит, курсор боя
void bv_close(void) __banked;                // отдать список вида, вернуть экран
uint8_t bv_load(const bvload_t *q) __banked; // таблицы вида из рабочей страницы, камера в центр
void bv_sync(uint8_t n) __banked;            // юниты: n записей runit_t из TL_RUNITS; предметы на полу (TV_FLOOR)
void bv_tile(uint8_t part) __banked;         // запись части из TL_TILES (створки НЛО)
void bv_cell(uint8_t x, uint8_t y, uint8_t z) __banked;   // перерисовать клетку
void bv_draw(void) __banked;                 // EVT_DRAW: дорисовать или собрать вид заново
void bv_redraw(void) __banked;               // собрать вид заново при следующей отрисовке
void bv_suspend(void) __banked;              // поверх боя открывается окно (инвентарь)
// «Hidden Movement»: 1 — экран показывает основной холст (там рисуют сообщение) вместо карты,
// 0 — снова карта (она в своих холстах цела, без пересборки)
void bv_message(uint8_t on) __banked;
void bv_snapshot(void) __banked;             // то же, но карта остаётся картинкой под окном
void bv_tick(uint8_t sel) __banked;          // прокрутка, курсор клетки, стрелка над sel
void bv_center(uint8_t x, uint8_t y) __banked;   // камера на клетку (этаж — камеры)
uint8_t bv_level(void) __banked;
void bv_set_level(uint8_t l, uint8_t recenter) __banked;
void bv_all_levels(void) __banked;           // «1/2»: все этажи или только текущий
uint8_t bv_cursor(uint8_t *x, uint8_t *y) __banked;   // клетка под мышью; 1 — на поле
void bv_aim(uint8_t aim) __banked;           // курсор клетки — прицел (CT_AIM) или рамка
// Шаг идущего юнита i из (lx, ly) в (dx, dy) по направлению d: фазы ходьбы рисует вид
void bv_walk(uint8_t i, uint8_t lx, uint8_t ly, uint8_t dx, uint8_t dy, uint8_t z, uint8_t d) __banked;
// Такт ходьбы: 0 — идёт (или никто), 1 — прошёл середину (числится в новой клетке), 2 — дошёл
uint8_t bv_walk_tick(void) __banked;
void bv_walk_done(void) __banked;            // после 2: новый шаг не начат — нарисовать стойку
uint8_t bv_walking(void) __banked;           // кто идёт (#FF — никто)
// Для ходьбы (bwalk.c): перерисовать прямоугольник вида (мировые); идущий i — в клетке (x, y)
void bv_repaint(int16_t lo, int16_t hi, int16_t top, int16_t bot) __banked;
void bv_walk_mid(uint8_t i, uint8_t x, uint8_t y) __banked;
uint16_t bv_dl_n(void) __banked;             // команд в списке вида (отладка)
// Спрайты поверх вида (пуля, вспышка — bfx.c): n записей в третьей рабочей странице (loader.h
// TV_FX); перерисовать прямоугольник [lo, hi] x [top, bot] (мировые) — прежние и новые
void bv_fx(uint8_t n, int16_t lo, int16_t hi, int16_t top, int16_t bot) __banked;

#endif
