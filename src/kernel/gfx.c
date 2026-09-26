// Графика интерфейса (банк 1, вместе с ui.c): заливки DMA, фон из пакета,
// спрайты, линии, палитра. Все функции восстанавливают Win3 (в нём может быть
// подключено состояние кампании); исключение — gfx_map (общий код, pages.c).
#include <stdint.h>
#include <string.h>
#include "tsconf.h"
#include "memmap.h"
#include "pages.h"
#include "far.h"
#include "res.h"
#include "dmabuf.h"
#include "res_ids.h"
#include "gfx.h"

static void dma_wait(void)
{
	while (TS_DMASTATUS & DMASTATUS_ACT)
		;
}

// Фон под всплывающими окнами (gfx_bgsave): прямоугольник экрана построчно в страницах пула
#define BG_LEVELS 8
typedef struct { int16_t x, y, w, h; uint8_t page, n; } bgsave_t;
static bgsave_t __at(0xBF00) bgs[BG_LEVELS];      // память банка 11 (окно 1 занято); gfx_init обнуляет

void gfx_init(void) __banked
{
	memset(bgs, 0, sizeof bgs);
	TS_VPAGE = SCREEN_PAGE;
	TS_GXOFFSL = 0; TS_GXOFFSH = 0;
	TS_GYOFFSL = 0; TS_GYOFFSH = 0;
	TS_BORDER = 0;
	TS_VCONFIG = VCONF_RRES_320x200 | VCONF_VM_256C;
	gfx_fill(0, 0, SCREEN_W, SCREEN_H, 0);
}

static void pset(int16_t x, int16_t y, uint8_t c)
{
	if ((uint16_t)x >= SCREEN_W || (uint16_t)y >= SCREEN_H) return;
	*gfx_map(x, y) = c;
}

void gfx_pset(int16_t x, int16_t y, uint8_t c) __banked
{
	uint8_t old = pg_win3();
	pset(x, y, c);
	pg_map3(old);
}

// Столбец CPU: страница экрана — 32 строки по 512 байт, подключается раз на полосу.
static void cpu_column(int16_t x, int16_t y, int16_t h, uint8_t c)
{
	uint8_t old = pg_win3();
	while (h > 0) {
		uint8_t *p = gfx_map(x, y);
		uint8_t n = 32 - ((uint8_t)y & 31);
		if (n > h) n = (uint8_t)h;
		h -= n;
		y += n;
		do { *p = c; p += 512; } while (--n);
	}
	pg_map3(old);
}

static uint16_t dma_dst(int16_t x, int16_t y)
{
	y += gfx_yb;                         // рабочая область композиции (ui.c) или видимый экран
	uint16_t offs = (((uint16_t)y & 31) << 9) | (uint16_t)x;
	TS_DMADAL = (uint8_t)offs;
	TS_DMADAH = (uint8_t)(offs >> 8);
	TS_DMADAX = SCREEN_PAGE + (uint8_t)(y >> 5);
	return offs;
}

static uint8_t clip(int16_t *x, int16_t *y, int16_t *w, int16_t *h)
{
	if (*x < 0) { *w += *x; *x = 0; }
	if (*y < 0) { *h += *y; *y = 0; }
	if (*x + *w > SCREEN_W) *w = SCREEN_W - *x;
	if (*y + *h > SCREEN_H) *h = SCREEN_H - *y;
	return *w > 0 && *h > 0;
}

// Заливка: низкие (до 2 строк) — CPU memset по строкам, узкие (до 4 точек) —
// столбцами CPU, остальное — DMA FILL (края по нечётным x — столбцами CPU).
// Слово заливки лежит в _DMABUF страницы данных — адрес источника DMA постоянный.
void gfx_fill(int16_t x, int16_t y, int16_t w, int16_t h, uint8_t c) __banked
{
	if (!clip(&x, &y, &w, &h)) return;
	if (h <= 2) {
		uint8_t old = pg_win3();
		for (; h > 0; h--, y++) memset(gfx_map(x, y), c, (uint16_t)w);
		pg_map3(old);
		return;
	}
	if (w <= 4) {                        // вертикальные линии рамок
		for (; w > 0; w--, x++) cpu_column(x, y, h, c);
		return;
	}
	if (x & 1) { cpu_column(x, y, h, c); x++; w--; }
	if (w & 1) { cpu_column(x + w - 1, y, h, c); w--; }
	uint16_t src = (uint16_t)&dma_fill_word;
	while (h > 0) {                      // не больше 256 строк за запуск
		int16_t n = h > 256 ? 256 : h;
		dma_wait();
		dma_fill_word = c | ((uint16_t)c << 8);
		TS_DMASAL = (uint8_t)src; TS_DMASAH = (uint8_t)(src >> 8) & 0x3F; TS_DMASAX = DATA_PAGE;
		dma_dst(x, y);
		TS_DMALEN = (uint8_t)(w / 2 - 1);
		TS_DMANUM = (uint8_t)(n - 1);
		TS_DMACTRL = DMA_FILL | DMA_D_ALGN | DMA_ASZ;
		y += n; h -= n;
	}
	dma_wait();
}

// Картинка IMG8 в пакете: строка — r.a байт подряд; копия построчно (источник не
// выровнен на 512). x и ширина выравниваются на 2 (DMA — словами), поэтому
// ширина картинки и sx должны быть чётными.
uint8_t gfx_key;                       // 1 — gfx_blit пропускает цвет 0 (DMA BLT1)

uint8_t gfx_blit(uint16_t res_id, int16_t sx, int16_t sy, int16_t x, int16_t y, int16_t w, int16_t h) __banked
{
	res_t r;
	if (!res_find(res_id, &r)) return 0;
	int16_t x0 = x, y0 = y;
	if (!clip(&x, &y, &w, &h)) return 1;
	sx += x - x0; sy += y - y0;
	if (x & 1) { x--; sx--; w++; }
	if (w & 1) w++;
	if (sx < 0) { x -= sx; w += sx; sx = 0; }
	if (sx + w > (int16_t)r.a) w = r.a - sx;
	if (sy + h > (int16_t)r.b) h = r.b - sy;
	if (w <= 0) return 1;
	far_t src = r.phys + (uint32_t)sy * r.a + sx;
	uint8_t sp = FAR_PAGE(src);          // страница и смещение — 16-битно по строкам
	uint16_t so = FAR_OFFS(src), step = r.a;
	for (; h > 0; h--, y++) {
		dma_wait();
		TS_DMASAL = (uint8_t)so; TS_DMASAH = (uint8_t)(so >> 8); TS_DMASAX = sp;
		dma_dst(x, y);
		TS_DMALEN = (uint8_t)(w / 2 - 1);
		TS_DMANUM = 0;
		TS_DMACTRL = gfx_key ? DMA_BLT1 | DMA_ASZ : DMA_RAM_RAM;
		so += step;
		if (so >= 0x4000) { so -= 0x4000; sp++; }
	}
	dma_wait();
	return 1;
}

uint8_t gfx_bg(uint16_t res_id, int16_t x, int16_t y, int16_t w, int16_t h) __banked
{
	return gfx_blit(res_id, x, y, x, y, w, h);
}

// Кадр набора спрайтов SPRSET (09 §5): u16 n, u8 cw, u8 ch; n x {x, y, w, h,
// u16 смещение/2}; данные кадров w*h (w чётная), 0 — прозрачно. (x, y) — угол
// ячейки. Кадр целиком на экране и x кадра чётный — DMA BLT1 по строкам,
// иначе — CPU с отсечением.
uint8_t gfx_sprite(uint16_t res_id, uint16_t frame, int16_t x, int16_t y) __banked
{
	res_t r;
	uint8_t e[6];
	if (!res_find(res_id, &r) || frame >= r.a) return 0;
	far_read(r.phys + 4 + frame * 6, e, 6);
	int16_t w = e[2], h = e[3];
	if (!w) return 1;
	x += e[0]; y += e[1];
	far_t src = r.phys + 4 + (uint32_t)r.a * 6 + ((uint32_t)(e[4] | (e[5] << 8)) << 1);
	uint8_t sp = FAR_PAGE(src);
	uint16_t so = FAR_OFFS(src);
	if (!(x & 1) && x >= 0 && y >= 0 && x + w <= SCREEN_W && y + h <= SCREEN_H) {
		for (; h > 0; h--, y++) {
			dma_wait();
			TS_DMASAL = (uint8_t)so; TS_DMASAH = (uint8_t)(so >> 8); TS_DMASAX = sp;
			dma_dst(x, y);
			TS_DMALEN = (uint8_t)(w / 2 - 1);
			TS_DMANUM = 0;
			TS_DMACTRL = DMA_BLT1 | DMA_ASZ;
			so += w;
			if (so >= 0x4000) { so -= 0x4000; sp++; }
		}
		dma_wait();
		return 1;
	}
	uint8_t row[256];
	uint8_t old = pg_win3();
	for (int16_t yy = 0; yy < h; yy++, y++) {
		far_read(FAR(sp, 0) + so, row, (uint16_t)w);
		so += w;
		if (so >= 0x4000) { so -= 0x4000; sp++; }
		if ((uint16_t)y >= SCREEN_H) continue;
		uint8_t *d = gfx_map(0, y);
		for (int16_t xx = 0; xx < w; xx++) {
			int16_t px = x + xx;
			if (row[xx] && (uint16_t)px < SCREEN_W) d[px] = row[xx];
		}
	}
	pg_map3(old);
	return 1;
}

void gfx_line(int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint8_t c) __banked
{
	int16_t dx = x1 > x0 ? x1 - x0 : x0 - x1, sx = x0 < x1 ? 1 : -1;
	int16_t dy = y1 > y0 ? y0 - y1 : y1 - y0, sy = y0 < y1 ? 1 : -1;
	int16_t err = dx + dy;
	uint8_t old = pg_win3();
	for (;;) {
		pset(x0, y0, c);
		if (x0 == x1 && y0 == y1) break;
		int16_t e2 = 2 * err;
		if (e2 >= dy) { err += dy; x0 += sx; }
		if (e2 <= dx) { err += dx; y0 += sy; }
	}
	pg_map3(old);
}

// Канал CRAM: без бита 15 — уровень 0..24 (2 бита ЦАП + 3 бита ШИМ, 24 и выше —
// максимум; REF/fpga/video/video_out.v). Стоковый ZX-Evo (Status.VDVER = 0) —
// пересчёт 5 бит -> 0..24; плата VDAC (VDVER != 0) — бит 15 (vdac_mode): все 5 бит
// линейно. Без бита 15 VDAC работает как ШИМ, и 25..31 насыщаются (засветка).
static uint16_t pwm(uint16_t w)
{
	uint16_t r = (w >> 10) & 31, g = (w >> 5) & 31, b = w & 31;
	r = r * 24 / 31; g = g * 24 / 31; b = b * 24 / 31;
	return (r << 10) | (g << 5) | b;
}

void gfx_palette(uint16_t pal_id, int8_t backpal) __banked
{
	res_t r;
	if (!res_find(pal_id, &r)) return;
	far_read(r.phys, dma_palette, 512);
	if (backpal >= 0 && res_find(RES_BACKPALS, &r))
		far_read(r.phys + (uint16_t)backpal * 32, &dma_palette[224], 32);
	if ((TS_STATUS & 7) == 0)
		for (uint16_t i = 0; i < 256; i++) dma_palette[i] = pwm(dma_palette[i]);
	else
		for (uint16_t i = 0; i < 256; i++) dma_palette[i] |= 0x8000;
	dma_wait();
	far_t s = near_phys(dma_palette);
	TS_DMASAL = (uint8_t)s; TS_DMASAH = (uint8_t)(s >> 8) & 0x3F; TS_DMASAX = FAR_PAGE(s);
	TS_DMADAL = 0; TS_DMADAH = 0; TS_DMADAX = 0;
	TS_DMALEN = 255;
	TS_DMANUM = 0;
	TS_DMACTRL = DMA_RAM_CRAM;
	dma_wait();
}

// Сдвиг цветов lo..lo+n-1 на шаг (цвет i показывает бывший i+1, последний — первый):
// радар перехвата (DogfightState::animate сдвигает пиксели окна — здесь палитра).
void gfx_pal_cycle(uint8_t lo, uint8_t n) __banked
{
	uint16_t first = dma_palette[lo];
	memmove(&dma_palette[lo], &dma_palette[lo + 1], (uint16_t)(n - 1) * 2);
	dma_palette[lo + n - 1] = first;
	dma_wait();
	far_t s = near_phys(&dma_palette[lo]);
	uint16_t d = (uint16_t)lo * 2;
	TS_DMASAL = (uint8_t)s; TS_DMASAH = (uint8_t)(s >> 8) & 0x3F; TS_DMASAX = FAR_PAGE(s);
	TS_DMADAL = (uint8_t)d; TS_DMADAH = (uint8_t)(d >> 8); TS_DMADAX = 0;
	TS_DMALEN = n - 1;
	TS_DMANUM = 0;
	TS_DMACTRL = DMA_RAM_CRAM;
	dma_wait();
}

// ---------------------------------------------------------------- всплывающие окна

extern volatile uint16_t frames;

// Строки прямоугольника: экран -> страницы пула (to_screen = 0) или обратно — одним 2D DMA: на экране
// строки через 512 байт (ALGN), в пуле — подряд (w чётная, h <= 200 — DMANum байт)
static void bg_copy(const bgsave_t *b, uint8_t to_screen)
{
	uint16_t so = (((uint16_t)b->y & 31) << 9) | (uint16_t)b->x;
	uint8_t sp = SCREEN_PAGE + (uint8_t)(b->y >> 5);
	dma_wait();
	if (to_screen) {
		TS_DMASAL = 0; TS_DMASAH = 0; TS_DMASAX = b->page;
		TS_DMADAL = (uint8_t)so; TS_DMADAH = (uint8_t)(so >> 8); TS_DMADAX = sp;
	} else {
		TS_DMASAL = (uint8_t)so; TS_DMASAH = (uint8_t)(so >> 8); TS_DMASAX = sp;
		TS_DMADAL = 0; TS_DMADAH = 0; TS_DMADAX = b->page;
	}
	TS_DMALEN = (uint8_t)(b->w / 2 - 1);
	TS_DMANUM = (uint8_t)(b->h - 1);
	TS_DMACTRL = DMA_RAM_RAM | DMA_ASZ | (to_screen ? DMA_D_ALGN : DMA_S_ALGN);
	dma_wait();
}

uint8_t gfx_bgsave(uint8_t lvl, int16_t x, int16_t y, int16_t w, int16_t h) __banked
{
	if (lvl >= BG_LEVELS) return 0;
	gfx_bgrestore(lvl, 0);
	if (!clip(&x, &y, &w, &h)) return 0;
	if (x & 1) { x--; w++; }
	if (w & 1) w++;
	uint8_t n = (uint8_t)(((uint32_t)w * (uint16_t)h + 0x3FFF) >> 14);
	uint8_t p = pg_alloc(n, 1);
	if (p == PG_NONE) return 0;
	bgsave_t *b = &bgs[lvl];
	b->x = x; b->y = y; b->w = w; b->h = h; b->page = p; b->n = n;
	bg_copy(b, 0);
	return 1;
}

uint8_t gfx_bgrestore(uint8_t lvl, uint8_t draw) __banked
{
	if (lvl >= BG_LEVELS) return 0;
	bgsave_t *b = &bgs[lvl];
	if (!b->n) return 0;
	if (draw) bg_copy(b, 1);
	pg_free(b->page, b->n);
	b->n = 0;
	return draw;
}

// Кольцо рамки окна
static void ring(int16_t x, int16_t y, int16_t w, int16_t h, uint8_t c)
{
	gfx_fill(x, y, w, 1, c);
	gfx_fill(x, y + h - 1, w, 1, c);
	gfx_fill(x, y + 1, 1, h - 2, c);
	gfx_fill(x + w - 1, y + 1, 1, h - 2, c);
}

// Рамка окна (Window::draw, толщина 5): кольца c+3, c+2, c+1, c+2, c+3 (вынесено из ui.c — банк 1 полон)
void gfx_rings(int16_t x, int16_t y, int16_t w, int16_t h, uint8_t c) __banked
{
	uint8_t k = c + 3;
	for (uint8_t i = 0; i < 5; i++) {
		ring(x, y, w, h, k);
		k = i < 2 ? k - 1 : k + 1;
		x++; y++; w -= 2; h -= 2;
	}
}

// Window::popup / draw: 10 шагов по кадру (POPUP_SPEED 0.05 по 10 мс у OpenXcom — те же ~200 мс), рамка
// растёт от середины по горизонтали (flags & 2) и/или вертикали (flags & 4): 5 колец c+3, c+2, c+1, c+2,
// c+3, внутри — фон окна bg (или c+3). Растёт — прошлый шаг накрыт, фон под окном не нужен.
void gfx_popup(int16_t x, int16_t y, int16_t w, int16_t h, uint8_t c, uint16_t bg, uint8_t flags) __banked
{
	for (uint8_t s = 1; s <= 10; s++) {
		uint16_t f = frames;
		int16_t sw = (flags & 2) ? w * s / 10 : w, sh = (flags & 4) ? h * s / 10 : h;
		int16_t sx = x + (w - sw) / 2, sy = y + (h - sh) / 2;
		uint8_t k = c + 3;
		for (uint8_t i = 0; i < 5; i++) {
			ring(sx, sy, sw, sh, k);
			k = i < 2 ? k - 1 : k + 1;
			sx++; sy++;
			sw = sw >= 2 ? sw - 2 : 1;
			sh = sh >= 2 ? sh - 2 : 1;
		}
		if (bg) gfx_bg(bg, sx, sy, sw, sh);
		else gfx_fill(sx, sy, sw, sh, c + 3);
		while (frames == f)
			;
	}
}

// Фаска TextButton и тонкой рамки Window::setThinBorder (ui.c, банк 1 переполнен): заливки
// c+1, c+5, c+2, c+4, c+3; inv — цвет середины для нажатой кнопки (0 — без инверсии).
static uint8_t gb_inv;
static uint8_t iv(uint8_t v) { return gb_inv ? (uint8_t)(2 * gb_inv - v) : v; }

void gfx_bevel(int16_t x, int16_t y, int16_t w, int16_t h, uint8_t c, uint8_t geo, uint8_t inv) __banked
{
	int16_t sx = 0, sy = 0;
	uint8_t k = c + 1;
	gb_inv = inv;
	for (uint8_t i = 0; i < 5; i++) {
		gfx_fill(x + sx, y + sy, w, h, iv(k));
		if (!(i & 1)) { sx++; sy++; }
		w--; h--;
		switch (i) {
		case 0: k = c + 5; gfx_pset(x + w, y, iv(k)); break;
		case 1: k = c + 2; break;
		case 2: k = c + 4; gfx_pset(x + w + 1, y + 1, iv(k)); break;
		case 3: k = c + 3; break;
		default:
			if (geo) { gfx_pset(x, y, iv(c)); gfx_pset(x + 1, y + 1, iv(c)); }
		}
	}
}

// Кнопка-стрелка 13x14 (ArrowButton ARROW_BIG_UP/DOWN); down — вниз
void gfx_arrow(int16_t x, int16_t y, uint8_t c, uint8_t down) __banked
{
	gfx_fill(x, y, 12, 13, c + 2);
	gfx_fill(x + 1, y + 1, 12, 13, c + 5);
	gfx_fill(x + 1, y + 1, 11, 12, c + 4);
	gfx_pset(x, y, c + 1);
	gfx_pset(x, y + 13, c + 4);
	gfx_pset(x + 12, y, c + 4);
	gfx_fill(x + 5, y + (down ? 3 : 8), 3, 3, c + 1);
	for (uint8_t i = 0; i < 5; i++)
		gfx_fill(x + 2 + i, down ? y + 6 + i : y + 7 - i, 9 - 2 * i, 1, c + 1);
}

// Прямоугольник экранной памяти -> другая строка экранной памяти (2D DMA, обе стороны с шагом
// 512): готовый виджет из рабочей области композиции на экран (ui.c). x и w — чётные.
void gfx_copy(int16_t x, int16_t ysrc, int16_t ydst, int16_t w, int16_t h) __banked
{
	uint16_t so = (((uint16_t)ysrc & 31) << 9) | (uint16_t)x;
	uint16_t dof = (((uint16_t)ydst & 31) << 9) | (uint16_t)x;
	while (h > 0) {                      // не больше 256 строк за запуск
		int16_t n = h > 256 ? 256 : h;
		dma_wait();
		TS_DMASAL = (uint8_t)so; TS_DMASAH = (uint8_t)(so >> 8);
		TS_DMASAX = SCREEN_PAGE + (uint8_t)(ysrc >> 5);
		TS_DMADAL = (uint8_t)dof; TS_DMADAH = (uint8_t)(dof >> 8);
		TS_DMADAX = SCREEN_PAGE + (uint8_t)(ydst >> 5);
		TS_DMALEN = (uint8_t)(w / 2 - 1);
		TS_DMANUM = (uint8_t)(n - 1);
		TS_DMACTRL = DMA_RAM_RAM | DMA_ASZ | DMA_S_ALGN | DMA_D_ALGN;
		ysrc += n; ydst += n; h -= n;
		so = (((uint16_t)ysrc & 31) << 9) | (uint16_t)x;
		dof = (((uint16_t)ydst & 31) << 9) | (uint16_t)x;
	}
	dma_wait();
}

// Окно (Window): фон пакета bg или заливка c+3 и кольца рамки; thin — фаска ComboBox
// (Window::setThinBorder). Вынесено из ui.c — банк 1 полон.
void gfx_window(int16_t x, int16_t y, int16_t w, int16_t h, uint8_t c, uint16_t bg, uint8_t thin) __banked
{
	// Фон мог не прочитаться (нет карты, вытеснен слот): тогда заливаем цветом, иначе
	// внутри окна остаётся прошлая картинка и понять это по экрану нельзя.
	if (thin) {
		gfx_bevel(x, y, w, h, c, 0, 0);
		if (!bg || !gfx_bg(bg, x + 3, y + 3, w - 5, h - 5)) gfx_fill(x + 3, y + 3, w - 5, h - 5, c + 3);
		return;
	}
	if (!bg || !gfx_bg(bg, x + 5, y + 5, w - 10, h - 10)) gfx_fill(x + 4, y + 4, w - 8, h - 8, c + 3);
	gfx_rings(x, y, w, h, c);
}

// Подсветка строки списка под курсором (TextList::mouseOver): цвета точек сдвигаются в своей
// палитре, нулевые не трогаются; combo — правила списка ComboBox. Вынесено из ui.c (банк 1 полон).
void gfx_selector(int16_t x, int16_t y, int16_t w, uint8_t h, uint8_t combo) __banked
{
	uint8_t old = pg_win3();
	for (uint8_t yy = 0; yy < h; yy++) {
		uint8_t *p = gfx_map(x, y + yy);
		for (int16_t xx = 0; xx < w; xx++) {
			uint8_t c = p[xx];
			if (!c) continue;
			if (combo) c = c < 223 ? 224 : c + 1;
			else c = (c & 15) < 10 ? c & 0xF0 : c - 10;
			p[xx] = c;
		}
	}
	pg_map3(old);
}

// Маленькая стрелка списка 11x8 (ArrowButton ARROW_SMALL_UP/DOWN/LEFT/RIGHT): shape 0 — вверх,
// 1 — вниз, 2/3 — влево/вправо. Вынесено из ui.c (банк 1 полон).
static const uint8_t harrow[2][6][4] = {
	{ { 2, 4, 2, 1 }, { 4, 3, 2, 3 }, { 6, 2, 1, 5 }, { 3, 4, 2, 1 }, { 5, 3, 2, 3 }, { 7, 2, 1, 5 } },
	{ { 7, 4, 2, 1 }, { 5, 3, 2, 3 }, { 4, 2, 1, 5 }, { 6, 4, 2, 1 }, { 4, 3, 2, 3 }, { 3, 2, 1, 5 } },
};

void gfx_arrow_small(int16_t x, int16_t y, uint8_t c, uint8_t shape) __banked
{
	gfx_fill(x, y, 10, 7, c + 2);
	gfx_fill(x + 1, y + 1, 10, 7, c + 5);
	gfx_fill(x + 1, y + 1, 9, 6, c + 4);
	gfx_pset(x, y, c + 1);
	gfx_pset(x, y + 7, c + 4);
	gfx_pset(x + 10, y, c + 4);
	if (shape >= 2) {
		const uint8_t (*r)[4] = harrow[shape - 2];
		for (uint8_t i = 0; i < 6; i++) gfx_fill(x + r[i][0], y + r[i][1], r[i][2], r[i][3], i < 3 ? c + 3 : c + 1);
		return;
	}
	for (uint8_t i = 0; i < 5; i++) {
		int16_t ry = shape ? y + 6 - i : y + 1 + i;
		gfx_fill(x + 5 - i, ry, 2 * i + 1, 1, c + 3);
		if (i) gfx_fill(x + 6 - i, ry, 2 * i - 1, 1, c + 1);
	}
}
