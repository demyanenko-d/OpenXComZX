// Фазы миссии (банк 32): см. bstage.h.
#include <stdint.h>
#include <stddef.h>
#include "far.h"
#include "res.h"
#include "res_ids.h"
#include "rules.h"
#include "loader.h"
#include "units.h"
#include "mapgen.h"
#include "ui.h"
#include "ui_ids.h"
#include "str_ids.h"
#include "scrdef.h"
#include "text.h"
#include "bstage.h"

uint16_t st_ufo_deploy(uint8_t utype) __banked
{
	rtab_t t;
	if (!rtab_open(RES_RULE_UFOS, &t) || utype >= t.n) return 0xFFFF;
	uint16_t name = rtab_word(&t, utype, 0);
	if (!rtab_open(RES_RULE_ALIENDEPLOYMENTS, &t)) return 0xFFFF;
	uint16_t i;
	for (i = 0; i < t.n; i++) {
		uint16_t w = rtab_word(&t, i, 0);    // сравнение — с копией (SDCC 4.5, checkasm)
		if (w == name) break;
	}
	return i < t.n ? i : 0xFFFF;
}

void st_side_prepare(uint8_t side, uint8_t n) __banked
{
	for (uint8_t i = 0; i < n; i++) {
		unit_t u;
		far_t a = UNITS_FAR + (uint16_t)i * sizeof u;
		far_read(a, &u, sizeof u);
		uint8_t f = u.faction;               // сравнения — с копиями (SDCC 4.5)
		if (!u.alive || f != side) continue;
		u.done = 0;                          // _dontReselect = false
		u.tu = u.tu_max;
		uint16_t e = (uint16_t)u.en + u.en_rec;
		u.en = e > u.en_max ? u.en_max : (uint8_t)e;
		far_write(a, &u, sizeof u);
	}
}

uint8_t st_alive(uint8_t n, uint8_t *xc) __banked
{
	uint8_t al = 0, x = 0;
	for (uint8_t i = 0; i < n; i++) {
		unit_t u;
		far_read(UNITS_FAR + (uint16_t)i * sizeof u, &u, sizeof u);
		uint8_t f = u.faction;
		if (!u.alive) continue;
		if (f == FACTION_HOSTILE) al++;
		else if (f == FACTION_PLAYER) x++;
	}
	*xc = x;
	return al;
}

uint16_t st_next(uint16_t dep) __banked
{
	rtab_t t;
	if (dep == 0xFFFF || !rtab_open(RES_RULE_ALIENDEPLOYMENTS, &t) || dep >= t.n) return 0xFFFF;
	uint16_t n = rtab_word(&t, dep, offsetof(r_alienDeployments_t, next_stage));
	return n < t.n ? n : 0xFFFF;
}

// ---------------------------------------------------------------- AbortMissionState
// Окно 320x144 на фоне TAC00.SCR (AbortMissionState.cpp:44-186): «в корабле» (у карты без корабля
// — «у входа», START_POINT), «в зоне выхода» (END_POINT; строка есть, если у развёртывания есть
// следующая фаза и на карте есть такие клетки), «снаружи»; без зоны выхода две строки — в 26 и 54
static const wdef_t w_abort3[] = {
	WIN(0, 0, 320, 144, UI_EL_MESSAGEWINDOWBORDER),
	TXT(16, 20, 304, 17, UI_EL_MESSAGEWINDOWS, DYN(0), BIG),
	TXT(16, 60, 304, 17, UI_EL_MESSAGEWINDOWS, DYN(1), BIG),
	TXT(0, 80, 320, 17, UI_EL_MESSAGEWINDOWS, STR_ABORT_MISSION_QUESTION, TC | BIG),
	BTN(16, 110, 120, 16, UI_EL_MESSAGEWINDOWBUTTONS, STR_OK, A_CUSTOM, 1, ENT),
	BTN(184, 110, 120, 16, UI_EL_MESSAGEWINDOWBUTTONS, STR_CANCEL_UC, A_POP, 0, ESC),
	TXT(16, 40, 304, 17, UI_EL_MESSAGEWINDOWS, DYN(2), BIG),
};
static const scr_t tab_abort = SCR(SCR_ABORT, UI_SCR_BATTLESCAPE, RES_PAL_BATTLESCAPE, RES_TAC00_SCR, SF_POPUP | SF_RAWPAL, w_abort3);

// targetType части пола (Tile::getMapData(O_FLOOR)->getSpecialType(); loader.h TV_PROPS, байт 10)
static uint8_t special(uint8_t page, uint8_t part)
{
	return part ? far_byte(FAR(page + 2, TV_PROPS) + (uint16_t)part * TV_PSIZE + 10) : 0;
}

// Есть ли на карте пол END_POINT (по частям миссии — затем клетки рядами)
static uint8_t has_exit(uint8_t page)
{
	uint8_t m[32], any = 0;
	for (uint16_t p = 1; p < 256; p++)
		if (special(page, (uint8_t)p) == SPECIAL_END_POINT) { m[p >> 3] |= (uint8_t)(1 << (p & 7)); any = 1; }
		else m[p >> 3] &= (uint8_t)~(1 << (p & 7));
	m[0] &= 0xFE;
	if (!any) return 0;
	far_t c = mapgen_cells();
	uint16_t n = (uint16_t)mapgen_sx() * mapgen_sy() * mapgen_sz();
	uint8_t buf[256];                        // клетки — по 64 за чтение
	while (n) {
		uint8_t k = n > 64 ? 64 : (uint8_t)n;
		far_read(c, buf, (uint16_t)k * 4);
		for (uint8_t j = 0; j < k; j++) {
			uint8_t p = buf[j * 4];
			if (m[p >> 3] & (1 << (p & 7))) return 1;
		}
		c += (uint16_t)k * 4;
		n -= k;
	}
	return 0;
}

uint8_t st_abort_get(sdef_t *s, wdef_t *w, uint8_t page, uint16_t dep) __banked
{
	scr_find(&tab_abort, 1, SCR_ABORT, s, w);
	if (st_next(dep) == 0xFFFF || !has_exit(page)) {
		w[1].y = 26; w[2].y = 54;
		s->n--;                              // строки зоны выхода нет
	}
	return 1;
}

// Бойцы по зонам: [0] в корабле (у входа), [1] снаружи, [2] в зоне выхода
static void areas(uint8_t page, uint8_t nsold, uint8_t *cnt, uint8_t *craft)
{
	uint8_t cx, cy, cw, cl, base;
	mapgen_craft(&cx, &cy, &cw, &cl, &base);
	*craft = cw != 0;
	far_t cells = mapgen_cells();
	uint16_t sx = mapgen_sx(), sy = mapgen_sy();
	cnt[0] = cnt[1] = cnt[2] = 0;
	for (uint8_t i = 0; i < nsold; i++) {
		unit_t u;
		far_read(UNITS_FAR + (uint16_t)i * sizeof u, &u, sizeof u);
		if (!u.alive) continue;
		uint8_t p = far_byte(cells + ((uint32_t)((uint16_t)u.z * sy + u.y) * sx + u.x) * 4), k = 1, t = special(page, p);
		if (cw && u.x >= cx && u.y >= cy && u.x < cx + cw && u.y < cy + cl && p && p >= base) k = 0;   // на полу корабля (части от base) в его прямоугольнике
		else if (t == SPECIAL_END_POINT) k = 2;
		else if (t == 1 && !cw) k = 0;       // START_POINT — вход карты без корабля
		cnt[k]++;
	}
}

void st_abort_text(uint8_t slot, char *buf, uint8_t page, uint8_t nsold) __banked
{
	uint8_t cnt[3], craft;
	areas(page, nsold, cnt, &craft);
	static const uint16_t str[3] = { STR_UNITS_IN_CRAFT_one, STR_UNITS_OUTSIDE_one, STR_UNITS_IN_EXIT_one };
	str_plural(buf, slot == 0 && !craft ? STR_UNITS_IN_ENTRANCE_one : str[slot], cnt[slot]);
}

// Tile::getMapData(O_FLOOR)->getSpecialType(): targetType части пола (loader.h TV_PROPS, байт 10)
uint8_t st_exit(uint8_t page, uint8_t nsold, uint8_t drop) __banked
{
	far_t cells = mapgen_cells();
	uint16_t sx = mapgen_sx(), sy = mapgen_sy();
	uint8_t in = 0;
	for (uint8_t i = 0; i < nsold; i++) {
		unit_t u;
		far_t a = UNITS_FAR + (uint16_t)i * sizeof u;
		far_read(a, &u, sizeof u);
		if (!u.alive) continue;
		uint8_t p = far_byte(cells + ((uint32_t)((uint16_t)u.z * sy + u.y) * sx + u.x) * 4);
		if (p && far_byte(FAR(page + 2, TV_PROPS) + (uint16_t)p * TV_PSIZE + 10) == SPECIAL_END_POINT) in++;
		else if (drop) { u.alive = 0; u.hp = 0; far_write(a, &u, sizeof u); }
	}
	return in;
}
