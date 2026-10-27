// Гранаты (банк 38): окно таймера PrimeGrenadeState (REF/OpenXcom/src/Battlescape/PrimeGrenadeState.cpp),
// взвод (BattlescapeGame::handleNonTargetAction, BA_PRIME) и дуга броска (ниже). Окно: фон 192x93 в (65, 45) цветом
// grenadeBackground.color; рамка Frame 192x27 в (65, 37) толщиной 3 с контрастом (средняя линия —
// grenadeMenu.border, фон — color2); заголовок «SET TIMER» крупно по центру в (65, 44) 192x24;
// 24 кнопки 22x22 в (66 + i%8*24, 64 + i/8*25): рамка grenadeBackground.border, внутри color2,
// число 0..23 крупно по центру клетки 20x20 в (67 + i%8*24, 67 + i/8*25) цветом grenadeMenu.color.
// Таймер — в rounds предмета (bitems.h): 0 — не взведена, иначе таймер + 1.
#include <stdint.h>
#include <string.h>
#include <stddef.h>
#include "far.h"
#include "res.h"
#include "rules.h"
#include "str_ids.h"
#include "ui.h"
#include "ui_ids.h"
#include "gfx.h"
#include "text.h"
#include "loader.h"
#include "pathfind.h"
#include "bmenu.h"
#include "bitems.h"
#include "bfire.h"
#include "bgren.h"

// Frame::draw: от средней линии color на (1 + толщина) · mul / 2 светлее, к середине толщины — темнее
static void frame3(int16_t x, int16_t y, int16_t w, int16_t h, uint8_t color, uint8_t bg)
{
	uint8_t c = color + 4;                   // (1 + 3) · 2 / 2
	for (uint8_t i = 0; i < 3; i++) {
		gfx_fill(x, y, w, 1, c);
		gfx_fill(x, y + h - 1, w, 1, c);
		gfx_fill(x, y, 1, h, c);
		gfx_fill(x + w - 1, y, 1, h, c);
		if (i < 1) c -= 2; else c += 2;
		x++; y++; w -= 2; h -= 2;
	}
	gfx_fill(x, y, w, h, bg);
}

void gr_draw(void) __banked
{
	uint8_t ui = UI_SCR_BATTLESCAPE;
	uint8_t mc = ui_color_in(ui, UI_EL_GRENADEMENU, 0), mbg = ui_color_in(ui, UI_EL_GRENADEMENU, 1);
	uint8_t mbd = ui_color_in(ui, UI_EL_GRENADEMENU, 2);
	uint8_t bc = ui_color_in(ui, UI_EL_GRENADEBACKGROUND, 0), bc2 = ui_color_in(ui, UI_EL_GRENADEBACKGROUND, 1);
	uint8_t bbd = ui_color_in(ui, UI_EL_GRENADEBACKGROUND, 2);
	gfx_fill(65, 45, 192, 93, bc);
	frame3(65, 37, 192, 27, mbd, mbg);
	tbox_t tb = { 65, 44, 192, 24, FNT_BIG, mc, mc, TX_CENTER | TX_CONTRAST };
	text_draw(&tb, str_get(STR_SET_TIMER));
	tb.w = 20; tb.h = 20; tb.flags = TX_CENTER | TX_MIDDLE | TX_CONTRAST;
	for (uint8_t i = 0; i < 24; i++) {
		int16_t x = 66 + (i & 7) * 24, y = 64 + (i >> 3) * 25;
		gfx_fill(x, y, 22, 22, bbd);
		gfx_fill(x + 1, y + 1, 20, 20, bc2);
		char s[4];
		fmt_num(s, i, 0);
		tb.x = x + 1; tb.y = y + 3;
		text_draw(&tb, s);
	}
}

uint8_t gr_hit(int16_t x, int16_t y) __banked
{
	for (uint8_t i = 0; i < 24; i++) {
		int16_t bx = 66 + (i & 7) * 24, by = 64 + (i >> 3) * 25;
		if (x >= bx && x < bx + 22 && y >= by && y < by + 22) return i;
	}
	return 0xFF;
}

uint8_t gr_window(uint8_t page, uint8_t unit, uint8_t right) __banked
{
	bitem_t b;
	rtab_t t;
	if (!bi_hand(page, unit, right, &b) || !rtab_open(RES_RULE_ITEMS, &t) || b.item >= t.n) return 0;
	return (uint8_t)rtab_word(&t, b.item, offsetof(r_items_t, battle_type)) != BT_PROXIMITYGRENADE;
}

void gr_prime(uint8_t page, uint8_t unit, uint8_t right, uint8_t fuse) __banked
{
	bitem_t b;
	uint16_t i = bi_hand(page, unit, right, &b);
	if (!i) return;
	b.rounds = fuse + 1;
	far_write(FAR(page + 1, TL_BITEMS) + (i - 1) * sizeof b, &b, sizeof b);
}

// ---------------------------------------------------------------- бросок
// Projectile::calculateThrow (REF/OpenXcom/src/Battlescape/Projectile.cpp:216) с проверками
// ProjectileFlyBState::validThrowRange / getMaxThrowDistance (ProjectileFlyBState.cpp:660) и
// TileEngine::validateThrow (TileEngine.cpp:2989); отрезки дуги и разброс — bfire.c (банк 35)
static int16_t __at(0xB000) P_dx;
static int16_t __at(0xB002) P_dy;
static int16_t __at(0xB004) P_dz;
static uint16_t __at(0xB006) P_r;
static uint16_t __at(0xB008) P_z;
static shot_t * __at(0xB00A) S;          // бросок вызывающего (его стек)
static int16_t __at(0xB00C) land[3];     // точка падения — из fire_line

static far_t cell_at(uint8_t x, uint8_t y, uint8_t z)
{
	return S->cells + ((uint32_t)((uint16_t)z * S->sy + y) * S->sx + x) * 4;
}

static int8_t tlevel(const uint8_t *c)   // Tile::getTerrainLevel
{
	int8_t a = c[0] ? (int8_t)far_byte(FAR(S->page, PF_TLEVEL) + c[0]) : 0;
	if (c[3]) { int8_t b = (int8_t)far_byte(FAR(S->page, PF_TLEVEL) + c[3]); if (b < a) a = b; }
	return a;
}

static uint16_t isqrt(uint32_t v)
{
	uint16_t r = 0;
	for (uint16_t b = 0x8000; b; b >>= 1) {
		uint16_t q = r | b;
		if ((uint32_t)q * q <= v) r = q;
	}
	return r;
}

// ProjectileFlyBState::getMaxThrowDistance (вокселей): шаг 8, вертикальная скорость падает на
// 50 · вес / сила сотых за шаг; всё в сотых
static int16_t max_throw(uint16_t weight, uint8_t strength, int16_t level)
{
	int32_t cz = (int32_t)level * 100 + 50;
	int16_t dz = 100, dist = 0, dec = strength ? (int16_t)(50 * weight / strength) : 5000;
	while (dist < 4000) {
		dist += 8;
		if (dz < -100) cz -= 800; else cz += (int32_t)dz * 8;
		if (cz < 0 && dz < 0) {              // откат по горизонтали
			dist -= (int16_t)(cz / (dz < -100 ? -100 : dz));
			break;
		}
		dz -= dec;
		if (dz <= -200) break;
	}
	return dist;
}

// Дуга TileEngine::calculateParabola без тригонометрии: направление (P_dx..) длиной ro, точка i —
// o + i · направление / ro, вверх — 4 · zA · i · (ro - i) / ro², zA = sqrt(ro) · кривизна.
// P_r — ro · 16, P_z — zA · 16

static void para_at(uint16_t i, int16_t *x, int16_t *y, int16_t *z)
{
	int32_t q = (int32_t)i * ((int32_t)P_r - 16 * (int32_t)i) * 64 / P_r;
	*x = S->ox + (int16_t)((int32_t)P_dx * i * 16 / P_r);
	*y = S->oy + (int16_t)((int32_t)P_dy * i * 16 / P_r);
	*z = S->oz + (int16_t)((int32_t)P_dz * i * 16 / P_r) + (int16_t)((int32_t)P_z * q / (16 * (int32_t)P_r));
}

static void para_set(int16_t dx, int16_t dy, int16_t dz, uint16_t c100)
{
	P_dx = dx; P_dy = dy; P_dz = dz;
	P_r = isqrt(((uint32_t)((int32_t)dx * dx + (int32_t)dy * dy + (int32_t)dz * dz)) * 256);
	if (!P_r) P_r = 1;
	P_z = (uint16_t)((uint32_t)isqrt((uint32_t)P_r * 16) * c100 / 100);
}

// Малый угол a (радианы, Q12): sin и cos рядом Тейлора
static void sin_cos(int16_t a, int16_t *sn, int16_t *cs)
{
	int32_t a2 = ((int32_t)a * a) >> 12;
	*sn = a - (int16_t)(((a2 * a) >> 12) / 6);
	*cs = 4096 - (int16_t)(a2 / 2) + (int16_t)(((a2 * a2) >> 12) / 24);
}

static int16_t clampa(int32_t a) { return a > 6144 ? 6144 : a < -6144 ? -6144 : (int16_t)a; }

// Отклонение броска (calculateParabola, delta): поворот по горизонтали на delta.x / ro · π / 2,
// наклон на (delta.z + delta.y) / ro · π / 14 · кривизна; ro — прежнее
static void para_turn(int16_t ddx, int16_t ddyz, uint16_t c100)
{
	int16_t sa, ca, sb, cb;
	sin_cos(clampa((int32_t)ddx * 102944 / P_r), &sa, &ca);              // 4096 · π / 2 · 16
	sin_cos(clampa((int32_t)ddyz * c100 * 147 / P_r), &sb, &cb);         // 4096 · π / 14 / 100 · 16
	int16_t h = (int16_t)isqrt((uint32_t)((int32_t)P_dx * P_dx + (int32_t)P_dy * P_dy));
	int16_t x = (int16_t)(((int32_t)P_dx * ca - (int32_t)P_dy * sa) >> 12);
	int16_t y = (int16_t)(((int32_t)P_dx * sa + (int32_t)P_dy * ca) >> 12);
	if (!h) { x = 1; y = 0; h = 1; }         // atan2(0, 0) = 0
	int16_t h2 = (int16_t)(((int32_t)h * cb + (int32_t)P_dz * sb) >> 12);
	P_dz = (int16_t)(((int32_t)P_dz * cb - (int32_t)h * sb) >> 12);
	P_dx = (int16_t)((int32_t)x * h2 / h);
	P_dy = (int16_t)((int32_t)y * h2 / h);
}

// Дуга от точки вылета шагами по вокселю (первый отрезок — до i = 8), между точками — линия
// с проверкой вокселей; клетка падения — точка перед ударом (h_*[0]); шагов — в *n
static int8_t parabola(uint16_t *n)
{
	int16_t p[6], l[3];                      // на стеке: банк 35 не видит память этого банка
	p[0] = S->ox; p[1] = S->oy; p[2] = p[5] = S->oz;
	int8_t r = V_EMPTY;
	uint16_t i = 8;
	while (p[5] > 0 && i < 1000) {
		para_at(i, &p[3], &p[4], &p[5]);
		r = fire_line(p, i == 8, l);
		if (r != V_EMPTY) break;
		p[0] = p[3]; p[1] = p[4]; p[2] = p[5];
		i++;
	}
	*n = i;
	memcpy(land, l, sizeof l);
	return r;
}

// Объект клетки не даёт положить предмет (непроходим и не большая стена 4..)
static uint8_t blocks(uint8_t obj)
{
	return obj && far_byte(FAR(S->page, PF_TU) + obj) == 255 && far_byte(FAR(S->page + 1, TL_BIGW) + obj) <= 3;
}

// Клетка падения (#FF по x — вне карты)
static void land_cell(uint8_t *c)
{
	c[0] = land[0] < 0 || land[1] < 0 || land[2] < 0 ? 0xFF : (uint8_t)(land[0] >> 4);
	c[1] = (uint8_t)(land[1] >> 4); c[2] = (uint8_t)(land[2] / 24);
	if (c[0] >= S->sx || c[1] >= S->sy || c[2] >= S->sz) c[0] = 0xFF;
}

int8_t gr_throw(shot_t *s, const fu_t *u, throw_t *t) __banked
{
	uint8_t c[4], lc[3];
	uint16_t n;
	S = s;
	if (!fire_setup(s, u)) return 2;
	far_read(cell_at(s->tx, s->ty, s->tz), c, 4);
	int8_t tl = tlevel(c);
	// validThrowRange: дальность по силе и весу (с обоймой) с поправкой на перепад высоты
	int16_t mx = max_throw(t->wtotal, t->strength, s->oz - ((int16_t)s->tz * 24 + 2 - tl)) + 8;
	int16_t dx = (int16_t)s->tx - u[s->shooter].x, dy = (int16_t)s->ty - u[s->shooter].y;
	if (mx < 0 || (int32_t)(dx * dx + dy * dy) * 256 > (int32_t)mx * mx) return 1;
	if (blocks(c[3])) return 2;
	// validateThrow: кривизна от силы и веса, +0.5, пока предмет не упадёт в целевую клетку
	int16_t tx = (int16_t)s->tx * 16 + 8, ty = (int16_t)s->ty * 16 + 8, tz = (int16_t)s->tz * 24 + 1 - tl;
	uint32_t v = (uint32_t)(t->weight ? t->weight : 1) * 65536 / (t->strength ? t->strength : 1);
	uint16_t cv = (uint16_t)((uint32_t)173 * isqrt((uint32_t)isqrt(v) * 256) / 256) + (t->kneel ? 10 : 0);
	if (cv < 48) cv = 48;
	for (;; cv += 50) {
		if (cv >= 500) return 2;
		para_set(tx - s->ox, ty - s->oy, tz - s->oz, cv);
		if (parabola(&n) == V_OUTOFBOUNDS) continue;
		land_cell(lc);
		if (lc[0] == s->tx && lc[1] == s->ty && lc[2] == s->tz) break;
	}
	// calculateThrow: разброс по меткости (applyAccuracy) поворачивает дугу; за карту или на
	// непроходимый объект — заново (после 16 попыток — проверенная дуга без разброса)
	for (uint8_t k = 0;; k++) {
		para_set(tx - s->ox, ty - s->oy, tz - s->oz, cv);
		if (k == 16) { parabola(&n); break; }
		int16_t x = tx, y = ty, z = tz;
		fire_spread(t->acc, tx - s->ox, ty - s->oy, tz - s->oz, &x, &y, &z);
		para_turn(x - tx, (z - tz) + (y - ty), cv);
		if (parabola(&n) == V_OUTOFBOUNDS) continue;
		land_cell(lc);
		if (lc[0] == 0xFF) continue;
		far_read(cell_at(lc[0], lc[1], lc[2]), c, 4);
		if (!blocks(c[3])) break;
	}
	land_cell(lc);
	if (lc[0] == 0xFF) return 2;
	t->lx = lc[0]; t->ly = lc[1]; t->lz = lc[2];
	t->dx = P_dx; t->dy = P_dy; t->dz = P_dz; t->r16 = P_r; t->z16 = P_z; t->n = n;
	return 0;
}

