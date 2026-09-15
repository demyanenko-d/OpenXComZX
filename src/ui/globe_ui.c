// Банк 2: точки и клики глобуса (src/inc/globe.h) — настоящая ортографическая проекция
// OpenXcom (Globe::polarToCart / cartToPolar) и синус 16-битного угла для констант вида
// рендера (src/ui/globe.c, банк 24). Вынесено из банка 24: там — только рендер.
#include <stdint.h>
#include "state.h"
#include "game.h"
#include "globe.h"
#include "globe_ui_tab.h"

// 16x16 -> 32 (src/kernel/mul32.s)
int32_t __mulsint2slong(int16_t a, int16_t b);
#define MUL(a, b) __mulsint2slong((int16_t)(a), (int16_t)(b))
#define HI16(v)   ((int16_t)((uint32_t)(v) >> 16))
#define SHR14(v)  HI16((int32_t)(v) << 2)

static const int16_t zoom_r[GLOBE_ZOOMS] = { 90, 120, 180, 280, 450, 720 };   // Globe::setupRadii

static int16_t sin16(uint16_t a)
{
	uint16_t i = (a >> 4) & 1023;
	int16_t v = (a & 0x4000) ? sin_q14[1024 - i] : sin_q14[i];
	return (a & 0x8000) ? -v : v;
}
#define cos16(a) sin16((uint16_t)(a) + 0x4000)

int16_t globe_sin(uint16_t a) __banked
{
	return sin16(a);
}

static uint16_t isqrt32(uint32_t v)
{
	uint32_t r = 0, b = 1ul << 30;
	while (b > v) b >>= 2;
	while (b) {
		if (v >= r + b) { v -= r + b; r = (r >> 1) + b; }
		else r >>= 1;
		b >>= 2;
	}
	return (uint16_t)r;
}

uint8_t globe_xy(const geo_t *p, int16_t *x, int16_t *y) __banked
{
	uint16_t a = (uint16_t)((uint32_t)p->lat >> 16), d = (uint16_t)((uint32_t)p->lon >> 16) - ctx.globe_lon;
	int16_t sp = sin16(a), cp = cos16(a), sd = sin16(d), cd = cos16(d);
	int16_t r = zoom_r[ST->zoom < GLOBE_ZOOMS ? ST->zoom : GLOBE_ZOOMS - 1];
	int16_t sc = sin16((uint16_t)ctx.globe_lat), cc = cos16((uint16_t)ctx.globe_lat);
	int16_t pc = SHR14(MUL(cp, cd));
	if (MUL(cc, pc) + MUL(sc, sp) < 0) return 0;                 // Globe::pointBack
	int16_t u = SHR14(MUL(cp, sd));
	int16_t v = SHR14(MUL(cc, sp) - MUL(sc, pc));
	int16_t px = GLOBE_CX + SHR14(MUL(r, u)), py = GLOBE_CY + SHR14(MUL(r, v));
	if (px < 1 || px > GLOBE_W - 2 || py < 1 || py > GLOBE_H - 2) return 0;
	*x = px; *y = py;
	return 1;
}

// Угол вектора (x, y) в единицах 65536 = 360° (atan_tab с линейной интерполяцией)
static uint16_t atan2_16(int32_t y, int32_t x)
{
	uint32_t ax = x < 0 ? (uint32_t)-x : (uint32_t)x, ay = y < 0 ? (uint32_t)-y : (uint32_t)y;
	uint16_t a;
	if (!ax && !ay) return 0;
	uint8_t swap = ay > ax;
	uint32_t num = swap ? ax : ay, den = swap ? ay : ax;
	while (num > 0xFFFF) { num >>= 1; den >>= 1; }
	uint32_t t = (num << 16) / den;            // отношение 0..1 в 16.16 = индекс таблицы в 8.8
	if (t >= 65536ul) a = atan_tab[256];
	else {
		uint8_t i = (uint8_t)(t >> 8), f = (uint8_t)t;
		a = atan_tab[i] + (uint16_t)(((uint32_t)(atan_tab[i + 1] - atan_tab[i]) * f) >> 8);
	}
	if (swap) a = 16384 - a;
	if (x < 0) a = 32768 - a;
	return y < 0 ? (uint16_t)-a : a;
}

// Globe::cartToPolar: точка сферы (X, Y, Z) в долях радиуса Q14, обратный поворот на
// наклон C: sinφ = cosC·Y + sinC·Z, cosφ·cosΔ = cosC·Z − sinC·Y, cosφ·sinΔ = X.
void globe_lonlat(int16_t x, int16_t y, geo_t *p) __banked
{
	int16_t r = zoom_r[ST->zoom < GLOBE_ZOOMS ? ST->zoom : GLOBE_ZOOMS - 1];
	int32_t X = ((int32_t)(x - GLOBE_CX) << 14) / r, Y = ((int32_t)(y - GLOBE_CY) << 14) / r;
	int32_t rr = X * X + Y * Y;
	if (rr > 16384l * 16384) {                 // за краем — на край
		uint16_t m = isqrt32((uint32_t)rr);
		X = X * 16384 / m; Y = Y * 16384 / m;
		rr = 16384l * 16384;
	}
	int32_t Z = isqrt32(16384ul * 16384 - (uint32_t)rr);
	int16_t sc = sin16((uint16_t)ctx.globe_lat), cc = cos16((uint16_t)ctx.globe_lat);
	int32_t s = (cc * Y + sc * Z) >> 14, pc = (cc * Z - sc * Y) >> 14;
	uint16_t h = isqrt32((uint32_t)(pc * pc + X * X));
	uint16_t lat = atan2_16(s, h), d = atan2_16(X, pc);
	p->lat = (int32_t)((uint32_t)lat << 16);
	p->lon = (int32_t)((uint32_t)(uint16_t)(ctx.globe_lon + d) << 16);
}
