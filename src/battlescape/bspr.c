// Значки замеченных пришельцев над панелью боя (банк 33): BattlescapeState _btnVisibleUnit —
// до 10 красных кнопок 15x12 в (300, 128 - 13 * i) (элемент visibleUnits: pos [300, -16] от
// панели), рамка цвета 15, заливка мигает 32..44 (blinkVisibleUnitButtons: +1 вверх, -2 вниз,
// раз в 100 мс), номер — NumberText цвета visibleUnits в (x + 6, y + 4), у «10» — на 2 левее.
// Карта показывается из своего холста (сплит), рисовать поверх неё нечем — значки сделаны
// аппаратными спрайтами TSU, как курсор. Спрайт берёт 16 цветов одной группы палитры, а у значка
// цвета трёх групп (15, 32..44, 16 + 1) — на значок три спрайта: рамка, заливка, номер.
// Цвет 32 — нулевой в своей группе, у спрайта он прозрачный: вместо него — 33.
// Дескрипторы 0..29 S-file — значки, 30 — курсор (input.s CUR_SPR), он поверх.
#include <stdint.h>
#include "tsconf.h"
#include "memmap.h"
#include "pages.h"
#include "bspr.h"

extern uint8_t cur_lock;                 // input.s: 1 — прерывание не трогает S-file

// Плитки в листе TSU (страница TSU_PAGE, 4 бита на точку, строка 256 байт): полоса 2 (строки
// 16..31), столбцы по 8 точек: рамка — 0, заливка — 2, номера 1..10 — 4, 6, …, 22
#define T_STRIP   2
#define T_BORDER  0
#define T_FILL    2
#define T_DIGIT   4

static const uint8_t dg[10][5] = {
	{7,5,5,5,7}, {2,6,2,2,7}, {7,1,7,4,7}, {7,1,7,1,7}, {5,5,7,1,1},
	{7,4,7,1,7}, {7,4,7,5,7}, {7,1,1,1,1}, {7,5,7,5,7}, {7,5,7,1,7},
};

static uint8_t bl_color = 32, bl_delta = 1;
static uint8_t shown;                    // сколько значков на экране

static void px(uint8_t col, uint8_t x, uint8_t y, uint8_t c)
{
	uint8_t *p = (uint8_t *)0xC000 + (uint16_t)(T_STRIP * 8 + y) * 256 + col * 4 + (x >> 1);
	if (x & 1) *p = (uint8_t)((*p & 0xF0) | c);
	else *p = (uint8_t)((*p & 0x0F) | (c << 4));
}

static void tile_clear(uint8_t col)
{
	for (uint8_t y = 0; y < 16; y++)
		for (uint8_t x = 0; x < 16; x++) px(col, x, y, 0);
}

static void fill_tile(void)
{
	uint8_t c = bl_color == 32 ? 1 : (uint8_t)(bl_color - 32);
	for (uint8_t y = 1; y < 11; y++)
		for (uint8_t x = 1; x < 14; x++) px(T_FILL, x, y, c);
}

void marks_init(void) __banked
{
	uint8_t old = pg_win3();
	pg_map3(TSU_PAGE);
	tile_clear(T_BORDER);
	for (uint8_t y = 0; y < 12; y++)
		for (uint8_t x = 0; x < 15; x++)
			if (!y || y == 11 || !x || x == 14) px(T_BORDER, x, y, 15);
	tile_clear(T_FILL);
	fill_tile();
	for (uint8_t n = 1; n <= 10; n++) {
		uint8_t col = (uint8_t)(T_DIGIT + (n - 1) * 2);
		tile_clear(col);
		char s[2] = { n < 10 ? (char)('0' + n) : '1', n < 10 ? 0 : '0' };
		uint8_t x0 = n < 10 ? 6 : 4;
		for (uint8_t k = 0; k < 2 && s[k]; k++)
			for (uint8_t r = 0; r < 5; r++) {
				uint8_t g = dg[s[k] - '0'][r];
				for (uint8_t b = 0; b < 3; b++)
					if (g & (4 >> b)) px(col, (uint8_t)(x0 + k * 4 + b), (uint8_t)(4 + r), 1);
			}
	}
	pg_map3(old);
	bl_color = 32; bl_delta = 1;
	shown = 0;
}

static void sfile(uint8_t idx, uint16_t w0, uint16_t w1, uint16_t w2)
{
	volatile uint16_t *sf = (volatile uint16_t *)0xC200;
	sf[idx * 3] = w0; sf[idx * 3 + 1] = w1; sf[idx * 3 + 2] = w2;
}

void marks_show(uint8_t n, uint8_t color) __banked
{
	if (n > 10) n = 10;
	uint8_t old = pg_win3();
	cur_lock = 1;
	pg_map3(SCRATCH_PAGE);
	TS_FMADDR = FMADDR_EN | 0x0C;
	for (uint8_t i = 0; i < 10; i++) {
		uint16_t y = 128 - 13 * i, act = i < n ? 0x2000 : 0;
		uint16_t w0 = (y & 0x1FF) | 0x0200 | act, w1 = 300 | 0x0200;   // 16x16
		sfile(i * 3, w0, w1, (uint16_t)((T_STRIP << 6) | T_BORDER));
		sfile(i * 3 + 1, w0, w1, (uint16_t)(0x2000 | (T_STRIP << 6) | T_FILL));
		sfile(i * 3 + 2, w0, w1, (uint16_t)(((uint16_t)(color >> 4) << 12) | (T_STRIP << 6) | (T_DIGIT + i * 2)));
	}
	TS_FMADDR = 0;
	pg_map3(old);
	cur_lock = 0;
	shown = n;
}

void marks_blink(void) __banked
{
	if (!shown) return;
	if (bl_color == 44) bl_delta = (uint8_t)-2;
	if (bl_color == 32) bl_delta = 1;
	bl_color += bl_delta;
	uint8_t old = pg_win3();
	pg_map3(TSU_PAGE);
	fill_tile();
	pg_map3(old);
}
