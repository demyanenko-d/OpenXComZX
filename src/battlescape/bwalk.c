// Ходьба юнита по фазам (банк 32; вынесена из вида bview.c — его банк 28 полон). Фазы и смещение
// спрайта — BattleUnit::keepWalking и Map::calculateWalkingOffset; перерисовка клеток шага — вид
// (bv_repaint), рисует идущего вид же по состоянию wk_* (bwalk.h).
#include <stdint.h>
#include "bview.h"
#include "bwalk.h"

extern volatile uint16_t frames;         // crt0.s

uint8_t wk_u = 0xFF;
uint8_t wk_ph, wk_mid, wk_end;
uint8_t wk_lx, wk_ly, wk_dx, wk_dy, wk_z;
int16_t wk_ox, wk_oy;
static uint8_t wk_d;                     // направление шага
static uint8_t wk_acc;                   // мс к следующей фазе
static uint16_t wk_fr;                   // кадр прошлого такта
// Шаг кончился, а стойку ещё не рисовали: следующий шаг пути начинается в том же такте
// (UnitWalkBState::think), и кадр стойки между клетками мелькал бы рывком рук и ног
static uint8_t wk_pend, wk_plx, wk_ply;  // прежняя клетка законченного шага — перерисовать с новым

#define TILE_W 32
#define TILE_H 40

// Смещение спрайта на фазе (Map::calculateWalkingOffset, юниты 1x1, без смены высоты пола):
// до середины — от прежней клетки к новой, после — от новой, где он уже числится
static const int8_t wo_x[8] = { 1, 1, 1, 0, -1, -1, -1, 0 };
static const int8_t wo_y[8] = { 1, 0, -1, -1, -1, 0, 1, 1 };

// Перерисовать прямоугольник обеих клеток шага (и строку-другую на качание торса)
static void walk_paint(void)
{
	if (wk_u != 0xFF) {
		int8_t k = (int8_t)(wk_ph < wk_mid ? wk_ph : wk_ph - wk_end);
		wk_ox = k * 2 * wo_x[wk_d];
		wk_oy = -k * wo_y[wk_d];
	}
	int16_t lx = ((int16_t)wk_lx - wk_ly) * 16, dx = ((int16_t)wk_dx - wk_dy) * 16;
	int16_t ly = ((int16_t)wk_lx + wk_ly) * 8, dy = ((int16_t)wk_dx + wk_dy) * 8;
	int16_t b = -(int16_t)wk_z * 24;
	bv_repaint(lx < dx ? lx : dx, (lx > dx ? lx : dx) + TILE_W - 1,
		(ly < dy ? ly : dy) + b - 2, (ly > dy ? ly : dy) + b + TILE_H + 1);
}

void bv_walk(uint8_t i, uint8_t lx, uint8_t ly, uint8_t dx, uint8_t dy, uint8_t z, uint8_t d) __banked
{
	wk_u = i;
	wk_z = z;
	wk_d = d & 7;
	wk_mid = 4 + 4 * (d & 1);
	wk_end = 8 + 8 * (d & 1);
	wk_ph = 0;
	wk_acc = 0;
	wk_fr = frames;
	if (wk_pend) {                       // хвост прошлого шага (прежняя клетка) — уже с фазой 0
		wk_pend = 0;
		wk_lx = wk_plx; wk_ly = wk_ply;
		wk_dx = lx; wk_dy = ly;
		walk_paint();
	}
	wk_lx = lx; wk_ly = ly;
	wk_dx = dx; wk_dy = dy;
	walk_paint();
}

// Шаг кончился, нового нет (путь кончился, поворот, дверь): стойка в клетках шага
void bv_walk_done(void) __banked
{
	if (!wk_pend) return;
	wk_pend = 0;
	walk_paint();
}

// Фаза раз в 30 мс, кадр — 20 мс; отставшая перерисовка не копит фазы — не больше одной за такт.
// На середине юнит переходит в новую клетку, на последней — уже стоит в ней.
uint8_t bv_walk_tick(void) __banked
{
	if (wk_u == 0xFF) return 0;
	uint8_t r = 0;
	uint16_t f = frames;
	uint16_t a = wk_acc + (uint16_t)(f - wk_fr) * 20;
	wk_fr = f;
	if (a >= 30) {
		a -= 30;
		if (a >= 30) a = 29;
		wk_ph++;
		uint8_t ph = wk_ph, mid = wk_mid;    // сравнения — с копиями (SDCC 4.5)
		if (ph == mid) { bv_walk_mid(wk_u, wk_dx, wk_dy); r = 1; }
		if (ph >= wk_end) {                  // стойку не рисуем: дальше bv_walk или bv_walk_done
			wk_u = 0xFF;
			wk_pend = 1;
			wk_plx = wk_lx; wk_ply = wk_ly;
			wk_acc = 0;
			return 2;
		}
		walk_paint();
	}
	wk_acc = (uint8_t)a;
	return r;
}

uint8_t bv_walking(void) __banked
{
	return wk_u;
}
