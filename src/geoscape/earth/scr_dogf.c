// Банк 23: окна воздушного (подводного) боя — DogfightState (рисование, кнопки) и
// DogfightErrorState OpenXcom (REF/OpenXcom/src/Geoscape/DogfightState.cpp,
// DogfightErrorState.cpp); логика — src/geoscape/earth/dogfight.c (банк 22), данные — dogfight.h.
//
// SCR_DOGFIGHT — окна 160x96 всех развёрнутых боёв поверх геоскейпа и область значков
// свёрнутых (их рисует глобус). Окно рисует себя само (W_CUSTOM на всё окно): картинка
// INTERWIN, поле боя 77x74, оружие, шкалы, тексты; за тик — только изменившееся.
// Отличия: радар — циклом 16 цветов палитры (radarRange), а не сдвигом цветов пикселей
// окна; пятна и снаряды темнее пикселей экрана под ними (в OpenXcom — картинки окна);
// звуков GEO.CAT нет (14_todo §3.16); свои клавиши 1..5 — режимы первого окна.
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "tsconf.h"
#include "memmap.h"
#include "res.h"
#include "rules.h"
#include "text.h"
#include "gfx.h"
#include "pages.h"
#include "state.h"
#include "game.h"
#include "scrdef.h"
#include "dogfight.h"

extern volatile uint16_t frames;         // crt0.s

static uint8_t df_wid[SDEF_MAXW];        // виджет экрана -> бой
static uint8_t row[48];
static char tbuf[16];
static uint8_t pal_phase;                // сдвигов цикла палитры радара с её загрузки

// Кнопки режимов 0..4 и «НЛО» (ImageButton поверх картинки окна): UFO — DogfightState,
// TFTD — interfaces.rul (pos/size от окна). Вид НЛО: previewTop / Bot / Mid (y, высота).
static const uint8_t btn_pos[2][6][4] = {
	{ { 83, 4, 36, 15 }, { 120, 4, 36, 15 }, { 83, 20, 36, 15 }, { 120, 20, 36, 15 }, { 120, 36, 36, 15 }, { 120, 52, 36, 17 } },
	{ { 84, 4, 35, 14 }, { 121, 4, 35, 14 }, { 84, 20, 35, 14 }, { 121, 20, 35, 14 }, { 121, 36, 35, 14 }, { 121, 52, 35, 14 } },
};
static const uint8_t prev_pos[2][3][2] = { { { 96, 15 }, { 111, 29 }, { 140, 52 } }, { { 96, 17 }, { 113, 21 }, { 134, 58 } } };
static const uint8_t dist_pos[2][2] = { { 116, 72 }, { 123, 73 } };

// _ufoBlobs: пятно НЛО 13x13 по размеру (+ кадр попадания) — затемнение пикселя радара
static const uint8_t blobs[8][13][13] = {
	{ {0,0,0,0,0,0,0,0,0,0,0,0,0}, {0,0,0,0,0,0,0,0,0,0,0,0,0}, {0,0,0,0,0,0,0,0,0,0,0,0,0}, {0,0,0,0,0,0,0,0,0,0,0,0,0},
	  {0,0,0,0,0,1,1,1,0,0,0,0,0}, {0,0,0,0,1,2,3,2,1,0,0,0,0}, {0,0,0,0,1,3,5,3,1,0,0,0,0}, {0,0,0,0,1,2,3,2,1,0,0,0,0},
	  {0,0,0,0,0,1,1,1,0,0,0,0,0}, {0,0,0,0,0,0,0,0,0,0,0,0,0}, {0,0,0,0,0,0,0,0,0,0,0,0,0}, {0,0,0,0,0,0,0,0,0,0,0,0,0},
	  {0,0,0,0,0,0,0,0,0,0,0,0,0} },
	{ {0,0,0,0,0,0,0,0,0,0,0,0,0}, {0,0,0,0,0,0,0,0,0,0,0,0,0}, {0,0,0,0,0,0,0,0,0,0,0,0,0}, {0,0,0,0,0,1,1,1,0,0,0,0,0},
	  {0,0,0,0,1,2,2,2,1,0,0,0,0}, {0,0,0,1,2,3,4,3,2,1,0,0,0}, {0,0,0,1,2,4,5,4,2,1,0,0,0}, {0,0,0,1,2,3,4,3,2,1,0,0,0},
	  {0,0,0,0,1,2,2,2,1,0,0,0,0}, {0,0,0,0,0,1,1,1,0,0,0,0,0}, {0,0,0,0,0,0,0,0,0,0,0,0,0}, {0,0,0,0,0,0,0,0,0,0,0,0,0},
	  {0,0,0,0,0,0,0,0,0,0,0,0,0} },
	{ {0,0,0,0,0,0,0,0,0,0,0,0,0}, {0,0,0,0,0,0,0,0,0,0,0,0,0}, {0,0,0,0,0,1,1,1,0,0,0,0,0}, {0,0,0,1,1,2,2,2,1,1,0,0,0},
	  {0,0,0,1,2,3,3,3,2,1,0,0,0}, {0,0,1,2,3,4,5,4,3,2,1,0,0}, {0,0,1,2,3,5,5,5,3,2,1,0,0}, {0,0,1,2,3,4,5,4,3,2,1,0,0},
	  {0,0,0,1,2,3,3,3,2,1,0,0,0}, {0,0,0,1,1,2,2,2,1,1,0,0,0}, {0,0,0,0,0,1,1,1,0,0,0,0,0}, {0,0,0,0,0,0,0,0,0,0,0,0,0},
	  {0,0,0,0,0,0,0,0,0,0,0,0,0} },
	{ {0,0,0,0,0,0,0,0,0,0,0,0,0}, {0,0,0,0,0,1,1,1,0,0,0,0,0}, {0,0,0,1,1,2,2,2,1,1,0,0,0}, {0,0,1,2,2,3,3,3,2,2,1,0,0},
	  {0,0,1,2,3,4,4,4,3,2,1,0,0}, {0,1,2,3,4,5,5,5,4,3,2,1,0}, {0,1,2,3,4,5,5,5,4,3,2,1,0}, {0,1,2,3,4,5,5,5,4,3,2,1,0},
	  {0,0,1,2,3,4,4,4,3,2,1,0,0}, {0,0,1,2,2,3,3,3,2,2,1,0,0}, {0,0,0,1,1,2,2,2,1,1,0,0,0}, {0,0,0,0,0,1,1,1,0,0,0,0,0},
	  {0,0,0,0,0,0,0,0,0,0,0,0,0} },
	{ {0,0,0,0,0,1,1,1,0,0,0,0,0}, {0,0,0,1,1,2,2,2,1,1,0,0,0}, {0,0,1,2,2,3,3,3,2,2,1,0,0}, {0,1,2,3,3,4,4,4,3,3,2,1,0},
	  {0,1,2,3,4,5,5,5,4,3,2,1,0}, {1,2,3,4,5,5,5,5,5,4,3,2,1}, {1,2,3,4,5,5,5,5,5,4,3,2,1}, {1,2,3,4,5,5,5,5,5,4,3,2,1},
	  {0,1,2,3,4,5,5,5,4,3,2,1,0}, {0,1,2,3,3,4,4,4,3,3,2,1,0}, {0,0,1,2,2,3,3,3,2,2,1,0,0}, {0,0,0,1,1,2,2,2,1,1,0,0,0},
	  {0,0,0,0,0,1,1,1,0,0,0,0,0} },
	{ {0,0,0,1,1,2,2,2,1,1,0,0,0}, {0,0,1,2,2,3,3,3,2,2,1,0,0}, {0,1,2,3,3,4,4,4,3,3,2,1,0}, {1,2,3,4,4,5,5,5,4,4,3,2,1},
	  {1,2,3,4,5,5,5,5,5,4,3,2,1}, {2,3,4,5,5,5,5,5,5,5,4,3,2}, {2,3,4,5,5,5,5,5,5,5,4,3,2}, {2,3,4,5,5,5,5,5,5,5,4,3,2},
	  {1,2,3,4,5,5,5,5,5,4,3,2,1}, {1,2,3,4,4,5,5,5,4,4,3,2,1}, {0,1,2,3,3,4,4,4,3,3,2,1,0}, {0,0,1,2,2,3,3,3,2,2,1,0,0},
	  {0,0,0,1,1,2,2,2,1,1,0,0,0} },
	{ {0,0,0,2,2,3,3,3,2,2,0,0,0}, {0,0,2,3,3,4,4,4,3,3,2,0,0}, {0,2,3,4,4,5,5,5,4,4,3,2,0}, {2,3,4,5,5,5,5,5,5,5,4,3,2},
	  {2,3,4,5,5,5,5,5,5,5,4,3,2}, {3,4,5,5,5,5,5,5,5,5,5,4,3}, {3,4,5,5,5,5,5,5,5,5,5,4,3}, {3,4,5,5,5,5,5,5,5,5,5,4,3},
	  {2,3,4,5,5,5,5,5,5,5,4,3,2}, {2,3,4,5,5,5,5,5,5,5,4,3,2}, {0,2,3,4,4,5,5,5,4,4,3,2,0}, {0,0,2,3,3,4,4,4,3,3,2,0,0},
	  {0,0,0,2,2,3,3,3,2,2,0,0,0} },
	{ {0,0,0,3,3,4,4,4,3,3,0,0,0}, {0,0,3,4,4,5,5,5,4,4,3,0,0}, {0,3,4,5,5,5,5,5,5,5,4,3,0}, {3,4,5,5,5,5,5,5,5,5,5,4,3},
	  {3,4,5,5,5,5,5,5,5,5,5,4,3}, {4,5,5,5,5,5,5,5,5,5,5,5,4}, {4,5,5,5,5,5,5,5,5,5,5,5,4}, {4,5,5,5,5,5,5,5,5,5,5,5,4},
	  {3,4,5,5,5,5,5,5,5,5,5,4,3}, {3,4,5,5,5,5,5,5,5,5,5,4,3}, {0,3,4,5,5,5,5,5,5,5,4,3,0}, {0,0,3,4,4,5,5,5,4,4,3,0,0},
	  {0,0,0,3,3,4,4,4,3,3,0,0,0} },
};

// _projectileBlobs: ракеты 3x6 по типу снаряда
static const uint8_t pblobs[4][6][3] = {
	{ {0,1,0}, {1,9,1}, {1,4,1}, {0,3,0}, {0,2,0}, {0,1,0} },
	{ {1,2,1}, {2,9,2}, {2,5,2}, {1,3,1}, {0,2,0}, {0,1,0} },
	{ {0,0,0}, {0,7,0}, {0,2,0}, {0,1,0}, {0,0,0}, {0,0,0} },
	{ {2,4,2}, {4,9,4}, {2,4,2}, {0,0,0}, {0,0,0}, {0,0,0} },
};

static uint8_t ufo_crashed(const dogfight_t *d)
{
	return ST->ufo[d->ufo].damage > d->udmax / 2;
}

static uint8_t weapon(const dogfight_t *d, uint8_t i)
{
	return i < d->nweap && ST->craft[d->craft].weap[i].type != NONE8;
}

static uint8_t icon_y(uint8_t num)
{
	return 5 * num + 16 * (num - 1);
}

// ---------------------------------------------------------------- рисование

static void blit(const dogfight_t *d, int16_t sx, int16_t sy, int16_t w, int16_t h)
{
	gfx_blit(RES_INTERWIN_DAT, sx, sy, d->x + sx, d->y + sy, w, h);
}

// Кадр INTICON (SPRSET, 09 §5) в рамку bw x bh (поверхность OpenXcom) с углом (x, y):
// off — +off к непрозрачным (Surface::offset), rows > 0 — drawCraftDamage: цвета
// корабля и повреждений в первых rows закрашенных строках — dcolor; bw = 0 — только
// высота силуэта (пиксели цвета корабля в столбце 11 рамки 22x25).
static uint8_t icon(uint16_t frame, int16_t x, int16_t y, uint8_t bw, uint8_t bh, uint8_t off, uint8_t rows, uint8_t dcolor)
{
	res_t r;
	uint8_t e[6], cnt = 0, done = 0;
	if (!res_find(RES_INTICON_PCK, &r) || frame >= r.a) return 0;
	far_read(r.phys + 4 + frame * 6, e, 6);
	uint8_t fx = e[0], fy = e[1], w = e[2], h = e[3];
	if (w > sizeof row) w = sizeof row;
	if (!bw) bh = 25;
	far_t src = r.phys + 4 + (uint32_t)r.a * 6 + ((uint32_t)(e[4] | (e[5] << 8)) << 1);
	uint8_t c0 = c_craft0, c1 = c_craft1, d0 = c_dmg0, d1 = c_dmg1;
	uint8_t old = pg_win3();
	for (uint8_t yy = 0; yy < h; yy++, src += e[2]) {
		uint8_t sy = fy + yy;
		if (sy >= bh) break;
		far_read(src, row, w);
		if (!bw) {
			uint8_t cx = 11 - fx;
			if (fx <= 11 && cx < w && row[cx] >= c0 && row[cx] < c1) cnt++;
			continue;
		}
		uint8_t *p = gfx_map(x, y + sy), any = 0;
		for (uint8_t xx = 0; xx < w; xx++) {
			uint8_t sx = fx + xx, c = row[xx];
			if (!c || sx >= bw) continue;
			if (done < rows && ((c >= d0 && c <= d1) || (c >= c0 && c < c1))) { c = dcolor; any = 1; }
			p[sx] = c + off;
		}
		if (any) done++;
	}
	pg_map3(old);
	return cnt;
}

// ImageButton::invert(color + 3) по прямоугольнику кнопки m
static void invert_btn(const dogfight_t *d, uint8_t m)
{
	const uint8_t *b = btn_pos[tftd][m];
	uint8_t mid = c_btn + 3, old = pg_win3();
	for (uint8_t yy = 0; yy < b[3]; yy++) {
		uint8_t *p = gfx_map(d->x + b[0], d->y + b[1] + yy);
		for (uint8_t xx = 0; xx < b[2]; xx++)
			if (p[xx]) p[xx] = (uint8_t)(2 * mid - p[xx]);
	}
	pg_map3(old);
}

static void text(int16_t x, int16_t y, int16_t w, uint8_t color, const char *s)
{
	tbox_t b;
	b.x = x; b.y = y; b.w = w; b.h = 9;
	b.font = FNT_SMALL; b.color = b.color2 = color; b.flags = 0;
	text_draw(&b, s);
}

static void draw_dist(const dogfight_t *d, uint8_t restore)
{
	const uint8_t *p = dist_pos[tftd];
	if (restore) blit(d, p[0], p[1], 40, 9);
	fmt_num(tbuf, d->dist, 0);
	text(d->x + p[0], d->y + p[1], 40, c_dist, tbuf);
}

static void draw_status(const dogfight_t *d, uint8_t restore)
{
	if (restore) blit(d, 4, 85, 150, 9);
	if (d->status != NOSTR) text(d->x + 4, d->y + 85, 150, c_text, str_get(d->status));
}

static void draw_damage(dogfight_t *d)
{
	if (d->height == NONE8) d->height = icon(d->csprite + 11, 0, 0, 0, 0, 0, 0, 0);
	uint8_t pct = (uint8_t)((uint32_t)ST->craft[d->craft].damage * 100 / d->cdmax);
	uint8_t rows = pct ? (uint8_t)((uint16_t)d->height * pct / 100) : 0;
	blit(d, 93, 40, 22, 25);
	icon(d->csprite + 11, d->x + 93, d->y + 40, 22, 25, 0, rows, d->dcolor);
}

// Сборка поля боя — в невидимых строках экрана 512x512 (OFF_Y..): чистое поле
// (картинка окна, столбцы 2..79, строки 3..78) у x 0, рабочее поле боя k у x 80 + 80k;
// на экран — одним DMA (без мерцания оружия под перерисовкой).
#define OFF_Y 200

static void dma_wait(void)
{
	while (TS_DMASTATUS & DMASTATUS_ACT)
		;
}

static uint16_t dma_dst(int16_t x, int16_t y)
{
	uint16_t offs = (((uint16_t)y & 31) << 9) | (uint16_t)x;
	TS_DMADAL = (uint8_t)offs;
	TS_DMADAH = (uint8_t)(offs >> 8);
	TS_DMADAX = SCREEN_PAGE + (uint8_t)(y >> 5);
	return offs;
}

// Прямоугольник экранной памяти -> другое место (x и w чётные), одним запуском
static void scr_copy(int16_t sx, int16_t sy, int16_t dx, int16_t dy, int16_t w, int16_t h)
{
	uint16_t so = (((uint16_t)sy & 31) << 9) | (uint16_t)sx;
	dma_wait();
	TS_DMASAL = (uint8_t)so; TS_DMASAH = (uint8_t)(so >> 8); TS_DMASAX = SCREEN_PAGE + (uint8_t)(sy >> 5);
	dma_dst(dx, dy);
	TS_DMALEN = (uint8_t)(w / 2 - 1);
	TS_DMANUM = (uint8_t)(h - 1);
	TS_DMACTRL = DMA_RAM_RAM | DMA_S_ALGN | DMA_D_ALGN | DMA_ASZ;
	dma_wait();
}

// Чистое поле боя: строки INTERWIN (2, 3, 78 x 76) в (0, OFF_Y) — gfx_blit отсекает по 200
static void clean_field(void)
{
	res_t r;
	if (!res_find(RES_INTERWIN_DAT, &r)) return;
	far_t src = r.phys + 3 * (uint32_t)r.a + 2;
	uint8_t sp = FAR_PAGE(src);
	uint16_t so = FAR_OFFS(src);
	for (int16_t y = OFF_Y; y < OFF_Y + 76; y++) {
		dma_wait();
		TS_DMASAL = (uint8_t)so; TS_DMASAH = (uint8_t)(so >> 8); TS_DMASAX = sp;
		dma_dst(0, y);
		TS_DMALEN = 78 / 2 - 1;
		TS_DMANUM = 0;
		TS_DMACTRL = DMA_RAM_RAM;
		so += r.a;
		if (so >= 0x4000) { so -= 0x4000; sp++; }
	}
	dma_wait();
}

// Шкала дальности оружия (1 км = 1 пиксель) в поверхности 21x74 у (19 | 43, 3) окна
// с углом (ox, oy)
static void draw_range(const dogfight_t *d, int16_t ox, int16_t oy, uint8_t i, uint8_t color)
{
	int16_t sx = ox + (i ? 43 : 19), sy = oy + 3;
	uint8_t x1 = i ? 0 : 2, x2 = i ? 18 : 0;
	int16_t ry = 74 - d->w[i].range, mn = 0, mx = 0;
	uint8_t old = pg_win3();
	for (uint8_t x = x1; x <= x1 + 18; x += 2) *gfx_map(sx + x, sy + ry) = color;
	if (ry < 57) { mn = ry; mx = 57; }
	else if (ry > 57) { mn = 57; mx = ry; }
	for (int16_t y = mn; y <= mx; y++) *gfx_map(sx + x1 + x2, sy + y) = color;
	for (uint8_t x = x2; x <= x2 + 2; x++) *gfx_map(sx + x, sy + 57) = color;
	pg_map3(old);
}

// Пятно / снаряд поверх пикселя p картинки: как в OpenXcom (цвет пикселя радара сдвинут
// на фазу, минус off, не ниже radarDetail), затем — индекс, который при сдвинутой
// палитре показывает этот цвет.
static uint8_t shade(uint8_t p, uint8_t off)
{
	uint8_t r0 = c_radar0, n = c_radar1 - c_radar0, ph = pal_phase, x;
	if ((uint8_t)(p - r0) < n) { x = p - r0 + ph; if (x >= n) x -= n; p = r0 + x; }
	uint8_t v = (uint8_t)(p - off), lo = c_blob;
	if (v < lo) v = lo;
	if ((uint8_t)(v - r0) < n) { x = v - r0 + n - ph; if (x >= n) x -= n; v = r0 + x; }
	return v;
}

// Поле боя 77x74 у (3, 3): пятно НЛО и снаряды, поверх — оружие, шкалы; full — всё
// поле с оружием и боезапасом, иначе — средняя полоса (столбцы 20..63: пятно, снаряды, шкалы)
static void draw_battle(const dogfight_t *d, uint8_t full)
{
	uint8_t k = (uint8_t)(d - df);
	int16_t wx = 80 + 80 * k, ox = wx - 2, oy = OFF_Y - 3;   // окно (lx, ly) -> (ox + lx, oy + ly)
	int16_t bx = ox + 3, by = oy + 3;
	uint8_t crashed = ufo_crashed(d);
	uint8_t destroyed = ST->ufo[d->ufo].damage >= d->udmax;
	scr_copy(0, OFF_Y, wx, OFF_Y, 78, 76);
	uint8_t old = pg_win3();
	if (d->size >= 0 && !destroyed) {            // drawUfo
		const uint8_t *bl = blobs[d->size + d->hit][0];
		int16_t uy = by + 68 - d->dist / 8;
		uint8_t mul = crashed || d->hit ? 2 : 1;
		for (uint8_t yy = 0; yy < 13; yy++, bl += 13) {
			int16_t y = uy + yy;
			if (y < by || y >= by + 74) continue;
			uint8_t *p = gfx_map(bx + 32, y);
			for (uint8_t xx = 0; xx < 13; xx++) {
				uint8_t o = bl[xx];
				if (o) p[xx] = shade(p[xx], o * mul);
			}
		}
	}
	for (uint8_t i = 0; i < NPROJ; i++) {       // drawProjectile
		const proj_t *pr = &d->p[i];
		if (pr->type == NONE8) continue;
		int16_t xp = bx + 38 + pr->hpos;
		if (pr->type < CWPT_BEAM) {
			const uint8_t *pb = pblobs[pr->type][0];
			int16_t yp = 74 - pr->pos / 8;
			for (uint8_t yy = 0; yy < 6; yy++, pb += 3) {
				int16_t y = yp + yy;
				if (y < 0 || y >= 74) continue;
				uint8_t *p = gfx_map(xp - 1, by + y);
				for (uint8_t xx = 0; xx < 3; xx++) {
					uint8_t o = pb[xx];
					if (o) p[xx] = shade(p[xx], o);
				}
			}
		} else {
			int16_t ye = 74 - d->dist / 8;
			for (int16_t y = 72; y > ye && y >= 0; y--) {
				uint8_t *p = gfx_map(xp, by + y);
				*p = shade(*p, pr->state);
			}
		}
	}
	pg_map3(old);
	for (uint8_t i = 0; i < 2; i++) {
		if (!weapon(d, i)) continue;
		uint8_t off = (d->flags & (i ? DFF_W2OFF : DFF_W1OFF)) != 0;
		if (full) icon(d->w[i].sprite + 5, ox + (i ? 64 : 4), oy + 52, 15, 17, off ? c_disw : 0, 0, 0);
		draw_range(d, ox, oy, i, c_meter + (off ? c_disr : 0));
	}
	if (!full) { scr_copy(wx + 18, OFF_Y, d->x + 20, d->y + 3, 44, 76); return; }
	scr_copy(wx, OFF_Y, d->x + 2, d->y + 3, 78, 76);
	for (uint8_t i = 0; i < 2; i++) {                // боезапас — на экран (text_draw отсекает по 200)
		if (!weapon(d, i)) continue;
		uint8_t off = (d->flags & (i ? DFF_W2OFF : DFF_W1OFF)) != 0;
		fmt_num(tbuf, ST->craft[d->craft].weap[i].ammo, 0);
		text(d->x + (i ? 64 : 4), d->y + 70, 16, c_num + (off ? c_disa : 0), tbuf);
	}
}

// Вид НЛО спереди (_preview): заливка 15, верх и низ рамки, силуэт НЛО
static void draw_preview(const dogfight_t *d)
{
	const uint8_t (*pp)[2] = prev_pos[tftd];
	gfx_fill(d->x, d->y, 160, 96, 15);
	gfx_key = 1;
	gfx_blit(RES_INTERWIN_DAT, 0, pp[0][0], d->x, d->y, 160, pp[0][1]);
	gfx_blit(RES_INTERWIN_DAT, 0, pp[1][0], d->x, d->y + 96 - pp[1][1], 160, pp[1][1]);
	gfx_blit(RES_INTERWIN_DAT, 0, pp[2][0] + pp[2][1] * d->usprite, d->x, d->y + pp[0][1], 160, pp[2][1]);
	gfx_key = 0;
}

static void draw_window(dogfight_t *d)
{
	d->dirty = 0;
	if (d->flags & DFF_PREVIEW) { draw_preview(d); return; }
	blit(d, 0, 0, 160, 96);
	invert_btn(d, d->mode);
	d->omode = d->mode;
	draw_battle(d, 1);
	draw_dist(d, 0);
	draw_status(d, 0);
	draw_damage(d);
}

static void draw_dirty(dogfight_t *d)
{
	uint8_t r = d->dirty;
	d->dirty = 0;
	if (!r || (d->flags & DFF_PREVIEW)) return;
	if ((r & DR_BTN) && d->omode != d->mode) {
		const uint8_t *b = btn_pos[tftd][d->omode];
		blit(d, b[0], b[1], b[2], b[3]);
		invert_btn(d, d->mode);
		d->omode = d->mode;
	}
	if (r & (DR_BATTLE | DR_AMMO)) draw_battle(d, (r & DR_AMMO) != 0);
	if (r & DR_DIST) draw_dist(d, 1);
	if (r & DR_STATUS) draw_status(d, 1);
	if (r & DR_DAMAGE) draw_damage(d);
}

// Значки свёрнутых боёв (_btnMinimizedIcon 32x20 и номер перехвата корабля) — глобус
void df_draw_icons(void) __banked
{
	for (uint8_t k = 0; k < DF_MAX; k++) {
		dogfight_t *d = &df[k];
		if (d->craft == NONE8 || !(d->flags & DFF_MIN)) continue;
		uint8_t y = icon_y(d->num);
		fmt_num(tbuf, ST->craft[d->craft].order, 0);
		icon(d->csprite, 5, y, 32, 20, 0, 0, 0);
		text(23, y + 6, 16, c_minnum, tbuf);
	}
}

uint8_t df_icon_click(void) __banked
{
	for (uint8_t k = 0; k < DF_MAX; k++) {
		dogfight_t *d = &df[k];
		if (d->craft == NONE8 || !(d->flags & DFF_MIN)) continue;
		int16_t y = icon_y(d->num);
		if (ui_click_x >= 5 && ui_click_x < 37 && ui_click_y >= y && ui_click_y < y + 20) return df_maximize(k);
	}
	return 0;
}

// ---------------------------------------------------------------- окна

static uint8_t inside(int16_t x, int16_t y, const uint8_t *r)
{
	return x >= r[0] && y >= r[1] && x < r[0] + r[2] && y < r[1] + r[3];
}

// Клик по окну боя k; 1 — окна перестроить (свёрнуто)
static uint8_t window_click(uint8_t k)
{
	static const uint8_t wr[2][4] = { { 4, 52, 15, 17 }, { 64, 52, 15, 17 } };
	dogfight_t *d = &df[k];
	int16_t x = ui_click_x - d->x, y = ui_click_y - d->y;
	if (d->flags & DFF_PREVIEW) {                    // previewClick
		d->flags &= ~DFF_PREVIEW;
		draw_window(d);
		return 0;
	}
	if (x < 12 && y < 12) return df_minimize(k);     // btnMinimizeClick
	for (uint8_t m = 0; m < 6; m++) {
		if (!inside(x, y, btn_pos[tftd][m])) continue;
		if (m == 5) { d->flags |= DFF_PREVIEW; draw_preview(d); }
		else df_press(k, m);
		return 0;
	}
	for (uint8_t i = 0; i < 2; i++) {                // weapon1Click / weapon2Click
		if (!weapon(d, i) || !inside(x, y, wr[i])) continue;
		d->flags ^= i ? DFF_W2OFF : DFF_W1OFF;
		d->dirty |= DR_AMMO;
	}
	return 0;
}

static const wdef_t w_dogerror[] = {
	WINP(24, 48, 208, 120, UI_EL_WINDOW, POPB),
	BTN(38, 128, 180, 12, UI_EL_BUTTON, STR_CONTINUE_INTERCEPTION_PURSUIT, A_POP, 0, ESC),
	BTN(38, 144, 180, 12, UI_EL_BUTTON, STR_RETURN_TO_BASE, A_CUSTOM, 1, ENT),
	TXT(29, 63, 198, 16, UI_EL_TEXT, DYN(0), BIG | TC),
	TXT(29, 94, 198, 20, UI_EL_TEXT, DYN(1), TC | TW),
};

static const wdef_t w_icons[] = {
	HOT(0, 0, 40, 90, A_CUSTOM, 0x80, 0),       // значки свёрнутых боёв на глобусе под окнами
};

static const scr_t tab[] = {
	SCR(SCR_DOGFIGHT_ERROR, UI_SCR_DOGFIGHTINFO, NOUI, RES_BACK15_SCR, SF_POPUP, w_dogerror),
	SCR(SCR_DOGFIGHT, UI_SCR_DOGFIGHT, NOUI, 0, SF_POPUP, w_icons),
};

uint8_t dogf_get(uint8_t id, sdef_t *s, wdef_t *w) __banked
{
	if (!scr_find(tab, sizeof tab / sizeof tab[0], id, s, w)) return 0;
	if (id != SCR_DOGFIGHT) return 1;
	for (uint8_t k = 0; k < DF_MAX; k++) {          // окна развёрнутых боёв
		dogfight_t *d = &df[k];
		if (d->craft == NONE8 || (d->flags & DFF_MIN) || s->n >= SDEF_MAXW) continue;
		wdef_t *e = &w[s->n];
		e->type = W_CUSTOM; e->x = d->x; e->y = d->y; e->w = 160; e->h = 96;
		e->el = 0xFF; e->str = NOSTR; e->flags = 0; e->act = A_CUSTOM; e->arg = k; e->key = 0;
		df_wid[s->n++] = k;
	}
	return 1;
}

void dogf_text(uint8_t id, uint8_t slot, uint8_t row, char *buf) __banked
{
	(void)row;
	if (id != SCR_DOGFIGHT_ERROR) return;
	if (slot == 0) craft_name(ctx.craft, buf);
	else str_copy(ctx.item ? STR_UNABLE_TO_ENGAGE_AIRBORNE : STR_UNABLE_TO_ENGAGE_DEPTH, buf, 128);
}

uint8_t dogf_rows(uint8_t id, uint8_t slot) __banked
{
	(void)id; (void)slot;
	return 0;
}

uint8_t dogf_event(uint8_t id, uint8_t ev, uint8_t arg) __banked
{
	if (id == SCR_DOGFIGHT_ERROR) {                  // «На базу»: returnToBase
		if (ev == EVT_BUTTON && arg == 1) {
			if (ST->craft[ctx.craft].type != NONE8) craft_return(ctx.craft);
			UI_GO(A_POP, 0);
		}
		return 0;
	}
	switch (ev) {
	case EVT_OPEN:
		if (!df_count) df_debug();
		clean_field();
		df_run();                                    // отсчёт тиков — с этого кадра
		break;
	case EVT_DRAW:                                   // после загрузки палитры (цикл — с нуля)
		pal_phase = 0;
		if (arg) draw_window(&df[df_wid[arg]]);
		break;
	case EVT_KEY:                                    // своё: режимы первого окна клавишами 1..5
		if (arg >= '1' && arg <= '5')
			for (uint8_t k = 0; k < DF_MAX; k++) {
				dogfight_t *d = &df[k];
				if (d->craft == NONE8 || (d->flags & (DFF_MIN | DFF_PREVIEW))) continue;
				df_press(k, arg - '1');
				draw_dirty(d);
				break;
			}
		break;
	case EVT_BUTTON:
		if (arg == 0x80) {                           // значок свёрнутого боя
			if (df_icon_click() == 1) UI_GO(A_REDRAW, 0);
			break;
		}
		if (arg >= DF_MAX || df[arg].craft == NONE8) break;
		if (window_click(arg)) UI_GO(df_nmax ? A_REDRAW : A_POP, 0);
		else draw_dirty(&df[arg]);
		break;
	case EVT_TICK: {
		if (!df_nmax) { UI_GO(A_POP, 0); break; }
		if (df_run()) { UI_GO(df_nmax ? A_REDRAW : A_POP, 0); break; }
		for (uint8_t i = 0; i < df_ticks; i++) {     // animate: радар — цикл палитры
			gfx_pal_cycle(c_radar0, c_radar1 - c_radar0);
			if (++pal_phase >= c_radar1 - c_radar0) pal_phase = 0;
		}
		for (uint8_t k = 0; k < DF_MAX; k++) {
			dogfight_t *d = &df[k];
			if (d->craft == NONE8 || (d->flags & DFF_MIN)) continue;
			if (df_ticks) d->dirty |= DR_BATTLE;     // пятно — по новой фазе палитры
			draw_dirty(d);
		}
		break;
	}
	}
	return 0;
}
