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

void gfx_init(void) __banked
{
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
