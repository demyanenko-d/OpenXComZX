// Сфера (банк 17): тригонометрия Q14, хорды, курс и шаг движения (MovingTarget
// OpenXcom в фиксированной точке), случайные точки зон миссий (RuleRegion::
// getRandomPoint, AlienMission::getLandPoint), маска полигонов WORLDMAP (insideLand).
//
// Координаты — geo_t: int32 «двоичные углы» (2^32 = 360°), широта минус — север.
// Скорость — единиц за шаг 5 с: узлы * 2^32 / (360 * 60 * 720) = узлы * 276.17.
// Движение (14_todo §3.1–3.2): курс и число шагов до цели считаются заново на каждом
// макрошаге (не реже раза в 10 минут игры), шаг — два сложения int32.
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "res.h"
#include "res_ids.h"
#include "rules.h"
#include "state.h"
#include "game.h"

// ---------------------------------------------------------------- тригонометрия

// sin четверти волны, Q14 (16384 = 1), шаг 90°/64
static const int16_t sin_q[65] = {
	0, 402, 804, 1205, 1606, 2006, 2404, 2801, 3196, 3590, 3981, 4370, 4756,
	5139, 5520, 5897, 6270, 6639, 7005, 7366, 7723, 8076, 8423, 8765, 9102, 9434,
	9760, 10080, 10394, 10702, 11003, 11297, 11585, 11866, 12140, 12406, 12665, 12916, 13160,
	13395, 13623, 13842, 14053, 14256, 14449, 14635, 14811, 14978, 15137, 15286, 15426, 15557,
	15679, 15791, 15893, 15986, 16069, 16143, 16207, 16261, 16305, 16340, 16364, 16379, 16384,
};

// sin угла a (65536 = 360°), Q14; линейная интерполяция между точками таблицы
static int16_t s_sin(uint16_t a)
{
	uint8_t q = (uint8_t)(a >> 14);
	uint16_t r = a & 0x3FFF;
	if (q & 1) r = 0x4000 - r;
	uint8_t i = (uint8_t)(r >> 8), f = (uint8_t)r;
	int16_t v = sin_q[i];
	if (i < 64) v += (int16_t)(((int32_t)(sin_q[i + 1] - v) * f) >> 8);
	return (q & 2) ? -v : v;
}

static int16_t s_cos(uint16_t a) { return s_sin(a + 0x4000); }

int16_t isin(uint16_t a) __banked { return s_sin(a); }
int16_t icos(uint16_t a) __banked { return s_cos(a); }

static uint16_t isqrt32(uint32_t v)
{
	uint32_t r = 0, bit = 1ul << 30;
	while (bit > v) bit >>= 2;
	while (bit) {
		if (v >= r + bit) { v -= r + bit; r = (r >> 1) + bit; }
		else r >>= 1;
		bit >>= 2;
	}
	return (uint16_t)r;
}

// a * q / 16384 (q — Q14, |q| <= 16384) через умножение 16x16 -> 32: a
// сдвигается до 15 бит (потеря — младшие биты a, для скоростей и близких
// расстояний это доли метра)
static int32_t mulq14(int32_t a, int16_t q)
{
	uint8_t sh = 0;
	while (a > 32767 || a < -32767) { a >>= 1; sh++; }
	int32_t r = (int32_t)(int16_t)a * q;
	return sh >= 14 ? r << (sh - 14) : r >> (14 - sh);
}

#define LON16(p) ((uint16_t)((uint32_t)(p)->lon >> 16))
#define LAT16(p) ((uint16_t)((uint32_t)(p)->lat >> 16))

// Единичный вектор точки, Q14
static void vec(const geo_t *p, int16_t v[3])
{
	uint16_t lo = LON16(p), la = LAT16(p);
	int16_t cl = s_cos(la);
	v[0] = (int16_t)(((int32_t)cl * s_cos(lo)) >> 14);
	v[1] = (int16_t)(((int32_t)cl * s_sin(lo)) >> 14);
	v[2] = s_sin(la);
}

// Хорда между точками, Q14 (0..32768)
uint16_t geo_chord(const geo_t *a, const geo_t *b) __banked
{
	int16_t u[3], v[3];
	vec(a, u);
	vec(b, v);
	uint32_t d2 = 0;
	for (uint8_t i = 0; i < 3; i++) {
		int32_t d = (int32_t)v[i] - u[i];
		if (d > 32767) d = 32767;
		if (d < -32767) d = -32767;
		d2 += (uint32_t)((int32_t)(int16_t)d * (int16_t)d);
	}
	return isqrt32(d2);
}

// Хорда между базами на сфере r = 51.2 (TransferItemsState::getDistance), * 16
uint16_t base_dist16(uint8_t a, uint8_t b) __banked
{
	geo_t pa = ST->base[a].pos, pb = ST->base[b].pos;
	return geo_chord(&pa, &pb) / 20;           // Q14 -> * 51.2 * 16 / 16384
}

// Косинус угла между точками, Q14 (скалярное произведение): радары — cos(угла) >= cos(R)
int16_t geo_cos(const geo_t *a, const geo_t *b) __banked
{
	int16_t u[3], v[3];
	vec(a, u);
	vec(b, v);
	return (int16_t)(((int32_t)u[0] * v[0] + (int32_t)u[1] * v[1] + (int32_t)u[2] * v[2]) >> 14);
}

// Хорда -> угол в двоичных единицах (для малых углов хорда ≈ угол, дальше — занижение,
// которое только добавляет пересчётов курса)
#define CHORD_UNITS 41722ul              // 2^32 / (2π · 16384)
#define NEAR_CHORD  400                  // ~1.4°: ближе — плоское приближение по int32

// Плоское расстояние в двоичных единицах (точно для малых углов)
static uint32_t near_dist(const geo_t *a, const geo_t *b, int32_t *dx, int32_t *dy)
{
	int32_t dlat = b->lat - a->lat, dlon = b->lon - a->lon;
	int16_t c = s_cos((uint16_t)(((uint32_t)a->lat + (uint32_t)(dlat / 2)) >> 16));
	int32_t x = mulq14(dlon, c);
	*dx = x; *dy = dlat;
	uint32_t ax = (uint32_t)(x < 0 ? -x : x) >> 9, ay = (uint32_t)(dlat < 0 ? -dlat : dlat) >> 9;
	if (ax > 0x7FFF || ay > 0x7FFF) return 0xFFFFFFFFul;
	return (uint32_t)isqrt32((uint32_t)(uint16_t)ax * (uint16_t)ax + (uint32_t)(uint16_t)ay * (uint16_t)ay) << 9;
}

// Расстояние в двоичных единицах (хорда или плоское приближение вблизи)
uint32_t geo_dist(const geo_t *a, const geo_t *b) __banked
{
	geo_t pa = *a, pb = *b;
	uint16_t ch = geo_chord(&pa, &pb);
	if (ch >= NEAR_CHORD) return (uint32_t)ch * CHORD_UNITS;
	int32_t dx, dy;
	return near_dist(&pa, &pb, &dx, &dy);
}

// Угол между точками, двоичные единицы: 2·asin(хорда/2) (asin — поиском по таблице
// синуса). Для топлива и дальности: хорда дальние расстояния занижает (на 90° — 11 %).
uint32_t geo_angle(const geo_t *a, const geo_t *b) __banked
{
	geo_t pa = *a, pb = *b;
	uint16_t ch = geo_chord(&pa, &pb);
	if (ch < NEAR_CHORD) {
		int32_t dx, dy;
		return near_dist(&pa, &pb, &dx, &dy);
	}
	uint16_t x = ch / 2, lo = 0, hi = 16384;     // sin(угла/2) Q14; угол/2 — 16-битные единицы
	while (lo < hi) {
		uint16_t mid = (uint16_t)((lo + hi + 1) >> 1);
		if ((uint16_t)s_sin(mid) <= x) lo = mid; else hi = mid - 1;
	}
	return (uint32_t)lo << 17;                   // * 2 и в 32-битные единицы
}

uint32_t geo_speed(uint16_t knots) __banked
{
	return (uint32_t)knots * 70699ul >> 8;      // * 276.17
}

// MovingTarget::calculateSpeed: вектор скорости к цели по большому кругу и число целых
// шагов до цели (затем шаг «прибытия» ставит точно в цель). speed — единиц за шаг.
void geo_aim(const geo_t *pos, const geo_t *dst, uint32_t speed, geo_vel_t *v) __banked
{
	geo_t p = *pos, d = *dst;
	int32_t x, y;
	uint32_t dist;
	v->dlon = v->dlat = 0;
	v->steps = 0;
	uint16_t ch = geo_chord(&p, &d);
	if (ch >= NEAR_CHORD) {
		// dLon = sin(Δlon)·cos(latB); dLat = cos(latA)·sin(latB) − sin(latA)·cos(latB)·cos(Δlon)
		uint16_t la = LAT16(&p), lb = LAT16(&d), dl = LON16(&d) - LON16(&p);
		int16_t cLb = s_cos(lb);
		x = ((int32_t)s_sin(dl) * cLb) >> 14;
		int16_t t = (int16_t)(((int32_t)s_sin(la) * cLb) >> 14);
		y = (((int32_t)s_cos(la) * s_sin(lb)) >> 14) - (((int32_t)t * s_cos(dl)) >> 14);
		dist = (uint32_t)ch * CHORD_UNITS;
	} else {
		dist = near_dist(&p, &d, &x, &y);
		while (x > 32767 || x < -32767 || y > 32767 || y < -32767) { x >>= 1; y >>= 1; }
	}
	if (!speed) { v->steps = 0xFFFF; return; }
	uint32_t n = dist / speed;
	v->steps = n > 0xFFFE ? 0xFFFE : (uint16_t)n;
	int16_t xs = (int16_t)x, ys = (int16_t)y;
	uint16_t len = isqrt32((uint32_t)((int32_t)xs * xs) + (uint32_t)((int32_t)ys * ys));
	if (!len) return;
	int16_t uy = (int16_t)((y << 14) / len), ux = (int16_t)((x << 14) / len);
	v->dlat = mulq14((int32_t)speed, uy);
	int32_t raw = mulq14((int32_t)speed, ux);
	int16_t c = s_cos((uint16_t)(((uint32_t)p.lat + (uint32_t)v->dlat) >> 16));   // cos(lat + speedLat)
	if (c < 256) c = 256;                    // у полюса (OpenXcom — NaN -> 0)
	v->dlon = (raw / c) * 16384 + ((raw % c) << 14) / c;
}

// Ufo::calculateSpeed: курс для окна НЛО (8 секторов по atan2(-dlat, dlon)): 0 — нет
// движения, 1..8 — N, NE, E, SE, S, SW, W, NW.
uint8_t geo_heading(const geo_vel_t *v) __banked
{
	int32_t x = v->dlon, y = -v->dlat;
	if (!x && !y) return 0;
	uint32_t ax = x < 0 ? -x : x, ay = y < 0 ? -y : y;
	// tan 22.5° ≈ 106/256
	while (ax > 0xFFFFFF || ay > 0xFFFFFF) { ax >>= 1; ay >>= 1; }
	if (ay * 256 < ax * 106) return x > 0 ? 3 : 7;
	if (ax * 256 < ay * 106) return y > 0 ? 1 : 5;
	if (y > 0) return x > 0 ? 2 : 8;
	return x > 0 ? 4 : 6;
}

// a * n (n < 32768) через два умножения 16x16 -> 32 (дешевле __mullong)
static int32_t mul32x16(int32_t a, uint16_t n)
{
	uint32_t lo = (uint32_t)(uint16_t)a * (uint16_t)n;
	int32_t hi = (int32_t)(int16_t)(a >> 16) * (int16_t)n;
	return (int32_t)(lo + ((uint32_t)hi << 16));
}

// n шагов по вектору
void geo_move(geo_t *p, const geo_vel_t *v, uint16_t n) __banked
{
	p->lon += n == 1 ? v->dlon : mul32x16(v->dlon, n);
	int32_t lat = p->lat + (n == 1 ? v->dlat : mul32x16(v->dlat, n));
	if (lat > 0x3FFFFFFFl) lat = 0x3FFFFFFFl;
	if (lat < -0x3FFFFFFFl) lat = -0x3FFFFFFFl;
	p->lat = lat;
}

// ---------------------------------------------------------------- маска полигонов

// WORLDMAP (OxzConv/Core/WorldMap.cs): текстура полигона WORLD.DAT в точке или -1.
// Растр 1° ниббл, граничные клетки — подрастр 1/8°.
int8_t world_texture(uint16_t lon, int16_t lat) __banked
{
	static far_t base;
	static uint8_t ok;
	if (!ok) {
		res_t r;
		if (!res_find(RES_WORLDMAP, &r)) return -1;
		base = r.phys;
		ok = 1;
	}
	uint16_t fy = (uint16_t)(((uint32_t)(uint16_t)(lat + 16384) * 1440u) >> 15);   // 1/8° с севера
	if (fy >= 1440) fy = 1439;
	uint16_t fx = (uint16_t)(((uint32_t)lon * 2880u) >> 16);
	uint8_t r = (uint8_t)(fy >> 3);
	uint16_t c = fx >> 3;
	far_t row = base + 2 + (uint32_t)r * 180;
	uint8_t b = far_byte(row + (c >> 1)), nib = (c & 1) ? b >> 4 : b & 15;
	if (nib == 15) return -1;
	if (nib < 14) return (int8_t)nib;
	uint8_t buf[180];                        // граничная: номер записи = до строки + в строке левее
	far_read(row, buf, 180);
	uint16_t k = far_word(base + 2 + 32400 + (uint32_t)r * 2);
	for (uint16_t i = 0; i < c; i++) {
		uint8_t n = (i & 1) ? buf[i >> 1] >> 4 : buf[i >> 1] & 15;
		if (n == 14) k++;
	}
	far_t rec = base + 2 + 32400 + 360 + (uint32_t)k * 9;
	if (!((far_byte(rec + 1 + (fy & 7)) >> (fx & 7)) & 1)) return -1;
	return (int8_t)far_byte(rec);
}

uint8_t inside_land(const geo_t *p) __banked
{
	return world_texture(LON16(p), (int16_t)LAT16(p)) >= 0;
}

// ---------------------------------------------------------------- зоны миссий

// Область зоны: запись zoneAreas. Номер первой области зоны и их число (regions.zones).
uint8_t zone_areas(uint8_t region, uint8_t zone, uint16_t *first) __banked
{
	rtab_t t;
	r_regions_t r;
	uint16_t st[2];
	*first = 0;
	rtab_open(RES_RULE_REGIONS, &t);
	rtab_get(&t, region, &r);
	if (zone + 1 >= r.zones.n) return 0;
	rtab_tail(&t, r.zones.off + zone * 2, st, 4);
	*first = st[0];
	return (uint8_t)(st[1] - st[0]);
}

// Случайная точка области (RNG::generate(min, max) по долготе и широте); точка-город — сама точка.
void area_point(uint16_t area, geo_t *out) __banked
{
	rtab_t t;
	r_zoneAreas_t a;
	rtab_open(RES_RULE_ZONEAREAS, &t);
	rtab_get(&t, area, &a);
	uint16_t lo0 = a.lon_min, lo1 = a.lon_max;
	if (lo0 > lo1) { uint16_t s = lo0; lo0 = lo1; lo1 = s; }
	uint16_t lon = lo0 + rng_range(0, lo1 - lo0);
	int16_t lat = a.lat_min + (int16_t)rng_range(0, (uint16_t)(a.lat_max - a.lat_min));
	out->lon = (int32_t)((uint32_t)lon << 16);
	out->lat = (int32_t)((uint32_t)(uint16_t)lat << 16);
	if (lo1 != lo0) out->lon |= rng_next();            // внутри области — и младшие биты
	if (a.lat_max != a.lat_min) out->lat |= rng_next();
}

uint8_t area_is_point(uint16_t area) __banked
{
	rtab_t t;
	r_zoneAreas_t a;
	rtab_open(RES_RULE_ZONEAREAS, &t);
	rtab_get(&t, area, &a);
	return a.lon_min == a.lon_max && a.lat_min == a.lat_max;
}

// RuleRegion::getRandomPoint: случайная область зоны, в ней случайная точка. 0 — нет зоны.
uint8_t zone_point(uint8_t region, uint8_t zone, geo_t *out) __banked
{
	uint16_t first;
	uint8_t n = zone_areas(region, zone, &first);
	if (!n) return 0;
	area_point(first + rng_range(0, n - 1), out);
	return 1;
}

// AlienMission::getLandPoint: город — как есть; иначе до 100 попыток на суше и в регионе.
uint8_t land_point(uint8_t region, uint8_t zone, geo_t *out) __banked
{
	uint16_t first;
	uint8_t n = zone_areas(region, zone, &first);
	if (!n) return 0;
	if (area_is_point(first)) return zone_point(region, zone, out);
	for (uint8_t tries = 0; tries < 100; tries++) {
		zone_point(region, zone, out);
		if (inside_land(out) && region_has(region, LON16(out), (int16_t)LAT16(out))) break;
	}
	return 1;
}
