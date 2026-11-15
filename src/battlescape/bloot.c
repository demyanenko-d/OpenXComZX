// Части НЛО на карте для разбора (банк 40): st_specials и st_recovery (bstage.h), вынесены из
// банка 32 — он полон.
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "far.h"
#include "res.h"
#include "res_ids.h"
#include "rules.h"
#include "state.h"
#include "loader.h"
#include "mapgen.h"
#include "units.h"
#include "vision.h"
#include "bstage.h"

// Юниты для обзора (vu_t) из units[] экрана боя — дальним чтением; seen — ставить VU_SEEN замеченным
// (вынесено из see_update банка 31)
void vis_fill(vu_t *v, uint8_t n, uint8_t seen) __banked
{
	unit_t u;
	for (uint8_t i = 0; i < n; i++) {
		far_read(UNITS_FAR + (uint16_t)i * sizeof u, &u, sizeof u);
		v[i].x = u.x; v[i].y = u.y; v[i].z = u.z; v[i].dir = u.dir;
		v[i].flags = (u.alive ? VU_ALIVE : 0) | (u.size > 1 ? VU_BIG : 0) | (seen && u.seen ? VU_SEEN : 0)
			| VU_H(UNIT_H(&u));
	}
}

// targetType части пола/стены/объекта (loader.h TV_PROPS, байт 10)
static uint8_t special(uint8_t page, uint8_t part)
{
	return part ? far_byte(FAR(page + 2, TV_PROPS) + (uint16_t)part * TV_PSIZE + 10) : 0;
}

void st_specials(uint8_t page, uint16_t *cnt) __banked
{
	uint8_t sp[256];                         // targetType каждой части миссии
	for (uint16_t p = 0; p < 256; p++) sp[p] = special(page, (uint8_t)p);
	memset(cnt, 0, 16 * 2);
	far_t c = mapgen_cells();
	uint16_t n = (uint16_t)mapgen_sx() * mapgen_sy() * mapgen_sz();
	uint8_t buf[128];                        // клетки — по 32 за чтение, 4 части в каждой
	while (n) {
		uint8_t k = n > 32 ? 32 : (uint8_t)n;
		far_read(c, buf, (uint16_t)k * 4);
		for (uint16_t j = 0; j < (uint16_t)k * 4; j++) {
			uint8_t t = sp[buf[j]];
			if (t > 1 && t < 16) cnt[t]++;
		}
		c += (uint16_t)k * 4;
		n -= k;
	}
}

// Части НЛО на карте (DebriefingState.cpp:788-799, 1378-1392, 1446-1463): предметы со specialType > 1
// — строкой каждый, в порядке правил; клетка — recoveryPoints; сплавы (ALIEN_ALLOYS, 10) — /10 у
// НЛО, иначе /150; на склад базы корабля craft
uint8_t st_recovery(uint8_t page, uint8_t ufo, uint16_t craft, strow_t *out, uint8_t max) __banked
{
	uint16_t cnt[16];
	rtab_t ti;
	st_specials(page, cnt);
	if (!rtab_open(RES_RULE_ITEMS, &ti)) return 0;
	uint8_t k = 0, b = NONE8;
	if (craft < MAX_CRAFTS && ST->craft[(uint8_t)craft].type != NONE8) b = ST->craft[(uint8_t)craft].base;
	for (uint16_t i = 0; i < ti.n && k < max; i++) {
		uint8_t sp = (uint8_t)rtab_word(&ti, i, offsetof(r_items_t, special_type));
		if (sp < 2 || sp > 15 || !cnt[sp]) continue;
		uint16_t n = cnt[sp], div = sp == 10 ? (ufo ? 10 : 150) : 1;
		int16_t v = (int16_t)((int32_t)n * (int16_t)rtab_word(&ti, i, offsetof(r_items_t, recovery_points)) / div);
		n /= div;
		if (!n) continue;
		out[k].str = rtab_word(&ti, i, 0); out[k].qty = n > 255 ? 255 : (uint8_t)n; out[k].score = v;
		k++;
		if (b < MAX_BASES && i < MAX_ITEMS) ST->base[b].items[i] += n;
	}
	return k;
}
