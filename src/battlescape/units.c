// Бойцы миссии: экипаж корабля из состояния кампании и его высадка (16 §5). Лежит в банке
// поиска пути — в банке боя (28) места уже нет, а работа эта разовая, на вход в бой.
//
// Показатели берутся так же, как BattleUnit(Soldier) оригинала: время — tu, здоровье —
// health, выносливость — stamina, боевой дух — 100. Прибавки брони (Armor::getStats) и
// снаряжение будут вместе с инвентарём боя (14 §todo).
//
// Места высадки: пока это свободные клетки внутри корабля (пол есть, объекта нет), а без
// корабля — у середины поля. В оригинале места задают узлы RMP карты корабля — их мы ещё
// не грузим.
#include <stdint.h>
#include "far.h"
#include "pages.h"
#include "state.h"
#include "units.h"
#include "gfx.h"
#include "text.h"
#include "res_ids.h"

#define PANEL_Y 144                      // окно карты выше, ниже — панель ICONS

// Свободна ли клетка под бойца: нужен пол и пустой объект. min — когда высадка идёт
// в корабль: пол должен быть его частью, иначе отряд встаёт на землю рядом с трапом.
static uint8_t cell_free(far_t cells, uint8_t sx, uint8_t sy, uint8_t x, uint8_t y, uint8_t min)
{
	if (x >= sx || y >= sy) return 0;
	uint8_t c[4];
	far_read(cells + ((uint32_t)y * sx + x) * 4, c, 4);
	return c[0] >= min && c[0] && !c[3];
}

uint8_t crew_deploy(const crew_req_t *q, cunit_t *out, uint8_t max) __banked
{
	// Показатели экипажа: сколько бойцов назначено на корабль (кроме лечащихся)
	uint8_t n = 0;
	if (q->craft != 0xFFFF) {
		uint8_t old = pg_map3(STATE_PAGE);
		uint8_t ns = ST->nsoldiers;
		pg_map3(old);
		for (uint8_t i = 0; i < ns && n < max; i++) {
			soldier_t s;
			far_read(FAR(SOLDIER_PAGE, 0) + (uint16_t)i * sizeof(soldier_t), &s, sizeof s);
			if (s.base == NONE8 || s.craft != (uint8_t)q->craft || s.recovery) continue;
			out[n].tu = s.cur.tu;
			out[n].hp = s.cur.health;
			out[n].en = s.cur.stamina;
			out[n].mor = 100;
			n++;
		}
	}
	if (!n) {                            // отладочный бой: фигуры с одинаковыми показателями
		n = q->debug_n;
		if (n > max) n = max;
		for (uint8_t i = 0; i < n; i++) {
			out[i].tu = 60; out[i].hp = 40; out[i].en = 60; out[i].mor = 100;
		}
	}
	// Клетки: подряд по рядам корабля (или середины поля), через одну — не вплотную
	uint8_t y0 = q->cw ? q->cy : (uint8_t)(q->sy / 2), y1 = q->cw ? (uint8_t)(q->cy + q->cl) : q->sy;
	uint8_t x0 = q->cw ? q->cx : 0, x1 = q->cw ? (uint8_t)(q->cx + q->cw) : q->sx;
	if (y1 > q->sy) y1 = q->sy;
	if (x1 > q->sx) x1 = q->sx;
	uint8_t k = 0;
	uint8_t min = q->cw ? q->part_min : 0;
	for (uint8_t pass = 0; pass < 2 && k < n; pass++, min = 0)   // второй заход — без корабля
	for (uint8_t y = y0; y < y1 && k < n; y++)
		for (uint8_t x = x0; x < x1 && k < n; x++) {
			if (!cell_free(q->cells, q->sx, q->sy, x, y, min)) continue;
			if (pass) {                       // на втором заходе клетка могла уже уйти
				uint8_t busy = 0;
				for (uint8_t i = 0; i < k; i++)
					if (out[i].x == x && out[i].y == y) { busy = 1; break; }
				if (busy) continue;
			}
			out[k].x = x; out[k].y = y; out[k].z = 0;
			k++;
			x++;
		}
	return k;
}

// ---------------------------------------------------------------- панель боя
// Полоска показателя — как Bar в оригинале: 102 пикселя на полную величину
static void bar(int16_t y, uint8_t v, uint8_t max, uint8_t color)
{
	uint8_t w = max ? (uint8_t)((uint16_t)v * 102 / max) : 0;
	gfx_fill(170, y, 102, 3, 0);
	if (w) gfx_fill(170, y, w, 3, color);
}

void bat_panel(const bpanel_t *p) __banked
{
	gfx_blit(RES_ICONS_PCK, 0, PANEL_Y, 0, PANEL_Y, 320, 200 - PANEL_Y);
	if (!p->n) return;
	// цвета полосок — из interfaces.rul боя (barTUs 148, barEnergy 160, barHealth 9,
	// barMorale 157)
	bar(185, p->tu, p->tu_max, 148);
	bar(189, p->en, p->tu_max, 160);
	bar(193, p->hp, p->hp_max, 9);
	bar(197, p->mor, 100, 157);
	char b[8];
	tbox_t t = { 136, 184, 24, 8, 0, 15, 15, 0 };
	fmt_num(b, p->tu, 0);
	text_draw(&t, b);                    // осталось единиц времени
	t.x = 228; t.y = 148;
	fmt_num(b, p->level + 1, 0);
	text_draw(&t, b);                    // этаж камеры
}
