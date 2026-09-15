// Мир (банк 17): случайные числа (состояние — в ST->rng, сохраняется с игрой)
// и поиск региона / страны по точке глобуса (зоны из таблиц правил,
// Region::insideRegion / Country::insideCountry OpenXcom). Тригонометрия — geo.c.
#include <stdint.h>
#include <stddef.h>
#include "res.h"
#include "rules.h"
#include "state.h"
#include "game.h"

// xorshift32
uint16_t rng_next(void) __banked
{
	uint32_t x = ST->rng;
	if (!x) x = 0x2545F491ul;
	x ^= x << 13;
	x ^= x >> 17;
	x ^= x << 5;
	ST->rng = x;
	return (uint16_t)(x >> 8);
}

// Равномерно в [lo, hi] (как RNG::generate): 16-битный остаток.
uint16_t rng_range(uint16_t lo, uint16_t hi) __banked
{
	if (hi <= lo) return lo;
	uint16_t span = hi - lo + 1, r = rng_next();
	if (!span) return r;                 // весь диапазон 0..65535
	return lo + r % span;
}

// RNG::percent: generate(0, 99) < n
uint8_t rng_percent(uint8_t n) __banked
{
	return rng_range(0, 99) < n;
}

// Зона: {u16 lonMin, lonMax; i16 latMin, latMax}; долгота может переходить через 0.
// RuleRegion::insideRegion: у южной широты (> 0) граница (min, max], иначе [min, max).
static uint8_t in_area(uint16_t lon, int16_t lat, const uint16_t *a)
{
	int16_t b0 = (int16_t)a[2], b1 = (int16_t)a[3];
	if (lat > 0 ? (lat <= b0 || lat > b1) : (lat < b0 || lat >= b1)) return 0;
	if (a[0] <= a[1]) return lon >= a[0] && lon < a[1];
	return lon >= a[0] || lon < a[1];
}

static uint8_t rec_has(const rtab_t *t, uint16_t i, uint8_t areas_off, uint16_t lon, int16_t lat)
{
	uint16_t a[16 * 4];
	uint8_t rec[3];
	far_read(t->base + (uint16_t)(8 + i * t->size + areas_off), rec, 3);   // rlist_t areas (таблица < 16 КБ)
	uint16_t off = rec[0] | (rec[1] << 8);
	uint8_t n = rec[2];
	while (n) {                                  // области — пачками по 16 одним чтением
		uint8_t m = n > 16 ? 16 : n;
		rtab_tail(t, off, a, m * 8);
		for (uint8_t k = 0; k < m; k++)
			if (in_area(lon, lat, a + k * 4)) return 1;
		off += m * 8;
		n -= m;
	}
	return 0;
}

static uint8_t find_area(uint16_t table, uint8_t areas_off, uint16_t lon, int16_t lat)
{
	rtab_t t;
	if (!rtab_open(table, &t)) return NONE8;
	for (uint16_t i = 0; i < t.n; i++)
		if (rec_has(&t, i, areas_off, lon, lat)) return (uint8_t)i;
	return NONE8;
}

uint8_t region_at(uint16_t lon, int16_t lat) __banked
{
	return find_area(RES_RULE_REGIONS, offsetof(r_regions_t, areas), lon, lat);
}

uint8_t country_at(uint16_t lon, int16_t lat) __banked
{
	return find_area(RES_RULE_COUNTRIES, offsetof(r_countries_t, areas), lon, lat);
}

uint8_t region_has(uint8_t r, uint16_t lon, int16_t lat) __banked
{
	rtab_t t;
	if (!rtab_open(RES_RULE_REGIONS, &t) || r >= t.n) return 0;
	return rec_has(&t, r, offsetof(r_regions_t, areas), lon, lat);
}

// Очки пришельцам / X-COM в регионе и стране точки (addActivityAlien / addActivityXcom).
void add_activity(const geo_t *p, int16_t pts, uint8_t xcom) __banked
{
	uint16_t lon = (uint16_t)((uint32_t)p->lon >> 16);
	int16_t lat = (int16_t)(p->lat >> 16);
	uint8_t m = ST->hist_len - 1, r = region_at(lon, lat), c = country_at(lon, lat);
	if (r < MAX_REGIONS) { if (xcom) ST->region[r].act_xcom[m] += pts; else ST->region[r].act_alien[m] += pts; }
	if (c < MAX_COUNTRIES) { if (xcom) ST->country[c].act_xcom[m] += pts; else ST->country[c].act_alien[m] += pts; }
}
