// Фазы миссии (банк 32): см. bstage.h.
#include <stdint.h>
#include <stddef.h>
#include <string.h>
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
#include "state.h"
#include "screens.h"
#include "music.h"
#include "mus_ids.h"
#include "gfx.h"
#include "memmap.h"
#include "bview.h"
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

// BattlescapeMessage «Hidden Movement» (Map.cpp:96-101): окно на месте карты (320x144) на фоне
// TAC00.SCR, крупно по центру в y 64 — цвета messageWindowBorder / messageWindows
void st_hidden_draw(void) __banked
{
	gfx_window(0, 0, SCREEN_W, 144, ui_color_in(UI_SCR_BATTLESCAPE, UI_EL_MESSAGEWINDOWBORDER, 0), RES_TAC00_SCR, 0);
	char s[32];
	tbox_t b;
	str_copy(STR_HIDDEN_MOVEMENT, s, sizeof s);
	b.x = 0; b.y = 64; b.w = SCREEN_W; b.h = 17; b.font = FNT_BIG; b.flags = TX_CENTER;
	b.color = ui_color_in(UI_SCR_BATTLESCAPE, UI_EL_MESSAGEWINDOWS, 0);
	b.color2 = ui_color_in(UI_SCR_BATTLESCAPE, UI_EL_MESSAGEWINDOWS, 1);
	text_draw(&b, s);
}

// ---------------------------------------------------------------- BriefingState
// BriefingState.cpp:50-151: окно 320x200 на фоне развёртывания (BACK16.SCR по умолчанию) с палитрой
// PAL_GEOSCAPE и блоком BACKPALS развёртывания, крупные заголовок (16,24), цель (16,40), корабль
// (16,56 + сдвиг), текст брифинга с переносом (16,72 + сдвиг, 274x94), OK (100,164); цвета —
// интерфейс briefing; музыка — развёртывания. OK — бой (перед следующей фазой — назад в бой)
static const wdef_t w_brief[] = {
	WIN(0, 0, 320, 200, UI_EL_WINDOW),
	BTN(100, 164, 120, 18, UI_EL_BUTTON, STR_OK, A_CUSTOM, 1, ENT),
	TXT(16, 24, 300, 32, UI_EL_TEXT, DYN(0), BIG),
	TXT(16, 72, 274, 94, UI_EL_TEXT, DYN(3), TW),
	TXT(16, 56, 300, 17, UI_EL_TEXT, DYN(2), BIG),
	TXT(16, 40, 300, 17, UI_EL_TEXT, DYN(1), BIG),
	HOT(0, 0, 0, 0, A_CUSTOM, 1, ESC),          // keyCancel — тоже OK
	CUS(0, 0, 0, 0, NOSTR, 0, 0),               // палитра с блоком BACKPALS развёртывания (EVT_DRAW)
};
static const scr_t tab_brief = SCR(SCR_BRIEFING, UI_SCR_BRIEFING, NOUI, RES_BACK16_SCR, 0, w_brief);
// AliensCrashState (AliensCrashState.cpp): окно 256x160 в (32, 20) на фоне TAC00.SCR, крупный текст
// по центру с переносом, OK — разбор (его зовёт бой, вернувшись из окна)
static const wdef_t w_crash[] = {
	WIN(32, 20, 256, 160, UI_EL_MESSAGEWINDOWBORDER),
	BTN(100, 154, 120, 18, UI_EL_MESSAGEWINDOWBUTTONS, STR_OK, A_CUSTOM, 1, ENT),
	TXT(37, 50, 246, 80, UI_EL_MESSAGEWINDOWS, STR_ALL_ALIENS_KILLED_IN_CRASH, TC | TM | TW | BIG),
	HOT(0, 0, 0, 0, A_CUSTOM, 1, ESC),
};
static const scr_t tab_crash = SCR(SCR_BRIEFING, UI_SCR_BATTLESCAPE, RES_PAL_BATTLESCAPE, RES_TAC00_SCR, SF_POPUP | SF_RAWPAL, w_crash);
static uint8_t st_bstage;                    // 1 — брифинг следующей фазы поверх боя, 2 — AliensCrashState

static uint8_t brief(uint16_t dep, r_alienDeployments_t *d)
{
	rtab_t t;
	if (dep == 0xFFFF || !rtab_open(RES_RULE_ALIENDEPLOYMENTS, &t) || dep >= t.n) return 0;
	rtab_get(&t, dep, d);
	return 1;
}

void st_brief_stage(uint8_t on) __banked
{
	st_bstage = on;
}

uint16_t st_mission(uint8_t utype, uint16_t deploy) __banked
{
	st_bstage = 0;
	return utype != 0xFF ? st_ufo_deploy(utype) : deploy;
}

static uint8_t abort_get(sdef_t *s, wdef_t *w, uint8_t page, uint16_t dep);

uint8_t st_get(uint8_t id, sdef_t *s, wdef_t *w, uint8_t page, uint16_t dep) __banked
{
	r_alienDeployments_t d;
	if (id == SCR_ABORT) return abort_get(s, w, page, dep);
	if (st_bstage == 2) return scr_find(&tab_crash, 1, SCR_BRIEFING, s, w);
	scr_find(&tab_brief, 1, SCR_BRIEFING, s, w);
	if (!brief(dep, &d)) return 1;
	if (d.briefing_bg) s->bg = d.briefing_bg;
	w[3].y = (int16_t)(72 + d.briefing_offset);
	w[4].y = (int16_t)(56 + d.briefing_offset);
	if (!(d.briefing_show & 2)) w[5].str = NOSTR;   // _txtTarget->setVisible(showTarget)
	if (!(d.briefing_show & 1)) w[4].str = NOSTR;   // _txtCraft->setVisible(showCraft)
	return 1;
}

void st_brief_text(uint8_t slot, char *buf, uint16_t dep, uint16_t craft) __banked
{
	r_alienDeployments_t d;
	buf[0] = 0;
	if (slot == 0 || slot == 3) {
		if (brief(dep, &d)) str_copy(slot ? d.briefing_desc : d.briefing_title, buf, slot ? 1024 : 64);
		return;
	}
	if (craft >= MAX_CRAFTS || ST->craft[(uint8_t)craft].type == NONE8) return;
	const craft_t *cr = &ST->craft[(uint8_t)craft];
	if (slot == 1) { target_name(cr->dest_kind, cr->dest, buf); return; }
	char n[32];                                  // STR_CRAFT_: «Craft> {0}»
	craft_name((uint8_t)craft, n);
	str_fmt(buf, str_get(STR_CRAFT_), n, "");
}

uint8_t st_brief_event(uint8_t ev, uint8_t arg, uint16_t dep) __banked
{
	r_alienDeployments_t d;
	uint8_t ok = st_bstage < 2 && brief(dep, &d);   // AliensCrashState — без музыки и палитры брифинга
	if (ev == EVT_MUSIC) { if (ok && d.briefing_music != 0xFF) mus_play(MUS_BASE + d.briefing_music); }
	else if (ev == EVT_DRAW) {
		if (st_bstage == 1) bv_pause();          // поверх боя: сплит вида снять (окно — во весь экран)
		if (ok) gfx_palette(RES_PAL_GEOSCAPE, (int8_t)d.briefing_pal);
	}
	else if (ev == EVT_BUTTON && arg == 1) {
		if (st_bstage) { st_bstage = 0; UI_GO(A_POP, 0); }
		else UI_GO(A_POP_PUSH, SCR_BATTLE);
	}
	return 0;
}

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

static uint8_t abort_get(sdef_t *s, wdef_t *w, uint8_t page, uint16_t dep)
{
	scr_find(&tab_abort, 1, SCR_ABORT, s, w);
	if (st_next(dep) == 0xFFFF || !has_exit(page)) {
		w[1].y = 26; w[2].y = 54;
		s->n--;                              // строки зоны выхода нет
	}
	return 1;
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

uint8_t st_zone(uint8_t page, uint8_t i) __banked
{
	uint8_t cx, cy, cw, cl, base;
	unit_t u;
	mapgen_craft(&cx, &cy, &cw, &cl, &base);
	far_read(UNITS_FAR + (uint16_t)i * sizeof u, &u, sizeof u);
	uint8_t p = far_byte(mapgen_cells() + ((uint32_t)((uint16_t)u.z * mapgen_sy() + u.y) * mapgen_sx() + u.x) * 4), t = special(page, p);
	if (cw && u.x >= cx && u.y >= cy && u.x < cx + cw && u.y < cy + cl && p && p >= base) return 0;   // на полу корабля (части от base) в его прямоугольнике
	if (t == SPECIAL_END_POINT) return 2;
	return t == 1 && !cw ? 0 : 1;            // START_POINT — вход карты без корабля
}

// Бойцы по зонам: [0] в корабле (у входа), [1] снаружи, [2] в зоне выхода
static void areas(uint8_t page, uint8_t nsold, uint8_t *cnt, uint8_t *craft)
{
	uint8_t cx, cy, cw, cl, base;
	mapgen_craft(&cx, &cy, &cw, &cl, &base);
	*craft = cw != 0;
	cnt[0] = cnt[1] = cnt[2] = 0;
	for (uint8_t i = 0; i < nsold; i++)
		if (far_byte(UNITS_FAR + (uint16_t)i * sizeof(unit_t) + offsetof(unit_t, alive))) cnt[st_zone(page, i)]++;
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
		else if (drop) { u.alive = 0; u.hp = 0; u.done = U_MIA; far_write(a, &u, sizeof u); }
	}
	return in;
}
