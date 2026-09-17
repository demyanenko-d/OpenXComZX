// Банк 3: геоскейп и его окна (GeoscapeState, UfoDetectedState, UfoLostState,
// MissionDetectedState, FundingState, MonthlyReportState, BaseNameState,
// BuildNewBaseState, ConfirmNewBaseState, SelectDestinationState); окна полёта —
// scr_fly.c, бой — scr_dogf.c, графики — scr_graph.c.
// Данные — состояние кампании (ST, state.h); ход времени — game_advance (gtime.c),
// события — очередь gev (show_event); свёрнутые воздушные бои — тик df_run и значки.
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "tsconf.h"
#include "res.h"
#include "rules.h"
#include "text.h"
#include "gfx.h"
#include "pages.h"
#include "state.h"
#include "game.h"
#include "globe.h"
#include "scrdef.h"

static char t1[80], t2[80];
static char big_tmp[256];                // промежуточные str_fmt (список стран отчёта, сообщения с {2})
static uint16_t tick_at;                 // кадр последнего такта времени (такт — раз в 5 кадров)
static uint8_t geo_sun;                  // эпоха солнца (globe_sunlon >> 7) последней перерисовки тени
static uint8_t icons_on;                 // нарисованы значки свёрнутых боёв
// Наезд перед перехватом (GeoscapeState::startDogfight / zoomInEffect / zoomOutEffect): 0 — нет,
// DZ_IN — приближение до DOGFIGHT_ZOOM, DZ_FIGHT — бои, DZ_OUT — отдаление к dz_old; время стоит
static uint8_t dz_state, dz_old;
#define DZ_IN          1
#define DZ_FIGHT       2
#define DZ_OUT         3
#define DOGFIGHT_ZOOM  3
uint8_t geo_dbg_attack;                  // сценарии: poke _geo_dbg_attack <база + 1> — штурм подставным НЛО
extern uint8_t df_dbg_type;              // dogfight.c: тип подставного НЛО
extern volatile uint16_t frames;         // crt0.s
// Удержание кнопок поворота и зума (GEOBORD) мышью — повтор, как в OpenXcom (btnRotate*Press):
// кнопка (30..35), кадр следующего повтора; прямоугольники кнопок (как HOT ниже)
static uint8_t rep_arg;
static uint16_t rep_at;
static const uint8_t rep_box[6][4] = {
	{ 259, 176, 12, 10 }, { 283, 176, 12, 10 }, { 271, 162, 13, 12 }, { 271, 187, 13, 12 },
	{ 295, 156, 23, 23 }, { 300, 182, 13, 17 },
};

// ---------------------------------------------------------------- правила

static uint16_t rec_name(uint16_t table, uint16_t i)
{
	rtab_t t;
	if (!rtab_open(table, &t) || i >= t.n) return 0xFFFF;
	return rtab_word(&t, i, 0);          // name — первое поле всех таблиц
}

static uint16_t tab_count(uint16_t table)
{
	rtab_t t;
	rtab_open(table, &t);
	return t.n;
}

// Регионы с зонами на глобусе (как регионы сохранения OpenXcom; в TFTD первые
// два — ARTIFACT_SITES и SHIPPING_LANES без зон и названий). i-й -> запись таблицы.
static uint8_t region_rec(uint8_t i, uint8_t *count)
{
	rtab_t t;
	r_regions_t r;
	uint8_t n = 0, found = 0;
	rtab_open(RES_RULE_REGIONS, &t);
	for (uint16_t k = 0; k < t.n; k++) {
		rtab_get(&t, k, &r);
		if (!r.areas.n) continue;
		if (n == i) found = (uint8_t)k;
		n++;
	}
	if (count) *count = n;
	return found;
}

// Отладочное меню окон: окна НЛО и места миссии без настоящих объектов — первое живое
// или подставное (НЛО в слоте 0, место в слоте 0 рядом с первой базой).
static uint8_t any_ufo(void)
{
	for (uint8_t u = 0; u < MAX_UFOS; u++) if (ST->ufo[u].type != NONE8) return u;
	ufo_t *u = &ST->ufo[0];
	memset(u, 0, sizeof *u);
	ST->nufos++;
	u->type = 1; u->id = ++ST->ids[ID_UFO]; u->altitude = 2; u->speed = 1800; u->dir = 3;
	u->status = US_FLYING; u->flags = UF_DETECTED; u->dest_base = u->shot_by = NONE8;
	u->mission = NONE8;                          // подставное: ход времени его не трогает (ufo.c)
	u->pos = ST->base[0].pos;
	u->pos.lon += 0x08000000l;
	u->dest = u->pos;
	return 0;
}

// Сценарии: штурм базы b подставным НЛО (тип df_dbg_type) — как прилёт
// __RETALIATION_ASSAULT_RUN (ufo.c: base_attack)
static void dbg_attack(uint8_t b)
{
	uint8_t u;
	for (u = 0; u < MAX_UFOS && ST->ufo[u].type != NONE8; u++);
	if (u >= MAX_UFOS || b >= MAX_BASES || !ST->base[b].name[0]) return;
	ufo_t *p = &ST->ufo[u];
	memset(p, 0, sizeof *p);
	ST->nufos++;
	p->type = df_dbg_type; p->id = ++ST->ids[ID_UFO]; p->altitude = 1;
	p->status = US_FLYING; p->dest_base = b; p->shot_by = NONE8; p->mission = NONE8;
	p->pos = ST->base[b].pos;
	p->dest = p->pos;
	gev_push(GE_BASE_ATTACK, b, b, u);
}

static void any_site(void)
{
	for (uint8_t s = 0; s < MAX_SITES; s++) if (ST->site[s].id) { ctx.tidx = s; return; }
	site_t *st = &ST->site[0];
	memset(st, 0, sizeof *st);
	ST->nsites++;
	st->id = ++ST->ids[ID_TERROR];
	st->pos = ST->base[0].pos;
	st->pos.lat += 0x04000000l;
	st->city = NONE16;
	st->flags = SITE_DETECTED;
	ctx.tidx = 0;
}

// UfoDetectedState: «есть подводные корабли» (isWaterOnly — maxAltitude > -1)
static uint8_t underwater(void)
{
	rtab_t t;
	rtab_open(RES_RULE_CRAFTS, &t);
	for (uint8_t i = 0; i < t.n; i++)
		if ((int8_t)(rtab_word(&t, i, offsetof(r_crafts_t, max_altitude)) & 0xFF) > -1) return 1;
	return 0;
}

// Центр глобуса на точку и скорость 5 с (globe->center + timerReset)
static void center_on(const geo_t *p)
{
	ctx.globe_lon = (uint16_t)((uint32_t)p->lon >> 16);
	ctx.globe_lat = (int16_t)(p->lat >> 16);
	ST->speed = 0;
}

// ---------------------------------------------------------------- время

// Номера строк — по алфавиту (str_ids.h), поэтому таблицы, а не STR_JAN + n.
static const uint16_t month_str[12] = {
	STR_JAN, STR_FEB, STR_MAR, STR_APR, STR_MAY, STR_JUN, STR_JUL, STR_AUG, STR_SEP, STR_OCT, STR_NOV, STR_DEC,
};
static const uint16_t wday_str[7] = {
	STR_SUNDAY, STR_MONDAY, STR_TUESDAY, STR_WEDNESDAY, STR_THURSDAY, STR_FRIDAY, STR_SATURDAY,
};

// Шаг времени за тик (как 1, 12, 60, 360, 720, 17280 шагов по 5 секунд): секунды и минуты
static const uint8_t step_sec[6] = { 5, 0, 0, 0, 0, 0 };
static const uint16_t step_min[6] = { 0, 1, 5, 30, 60, 1440 };

// Следующее событие хода времени -> окно (поставки собираются в одно окно).
static void show_event(void)
{
	if (!gev_n) return;
	ev_cur = gev[0];
	if (ev_cur.kind == GE_ARRIVED) {
		narr = 0;
		uint8_t k = 0;
		for (uint8_t i = 0; i < gev_n; i++)
			if (gev[i].kind == GE_ARRIVED) { if (narr < GEV_MAX) arrivals[narr++] = gev[i]; }
			else gev[k++] = gev[i];
		gev_n = k;
		UI_GO(A_PUSH, SCR_ITEMS_ARRIVING);
		return;
	}
	memmove(gev, gev + 1, (gev_n - 1) * sizeof(gevent_t));
	gev_n--;
	switch (ev_cur.kind) {
	case GE_BUILT: case GE_PRODUCTION: case GE_NO_MONEY: case GE_NO_MATERIALS: UI_GO(A_PUSH, SCR_PRODUCTION_DONE); break;
	case GE_RESEARCH: UI_GO(A_PUSH, SCR_RESEARCH_COMPLETE); break;
	case GE_NEWRES: UI_GO(A_PUSH, SCR_NEW_POSS_RESEARCH); break;
	case GE_NEWMAN: UI_GO(A_PUSH, SCR_NEW_POSS_MANUF); break;
	case GE_RESREQ: UI_GO(A_PUSH, SCR_RESEARCH_REQUIRED); break;
	case GE_MONTH: UI_GO(A_PUSH, SCR_MONTHLY_REPORT); break;
	case GE_NO_AMMO:                             // «нет {0} для перевооружения {1} на {2}»
	case GE_NO_FUEL: {                           // «нет {0} для заправки {1} на {2}»
		char *tmp = big_tmp;                      // str_fmt пишет до 255 символов
		craft_name((uint8_t)ev_cur.what, t1);
		str_copy(rec_name(RES_RULE_ITEMS, ev_cur.qty), t2, sizeof t2);
		str_fmt(tmp, str_get(ev_cur.kind == GE_NO_AMMO ? STR_NOT_ENOUGH_ITEM_TO_REARM_CRAFT_AT_BASE
			: STR_NOT_ENOUGH_ITEM_TO_REFUEL_CRAFT_AT_BASE), t2, t1);
		for (char *p = tmp; *p; p++) if (*p == 0x12) *p = 0x10;   // {2} -> {0} второго прохода
		str_fmt(ui_msg, tmp, ST->base[ev_cur.base].name, "");
		UI_GO(A_PUSH, SCR_CRAFT_ERROR);
		break;
	}
	case GE_UFO_DETECTED:                        // UfoDetectedState(ufo, detected = true, hyperwave)
		if (ST->ufo[ev_cur.what].type == NONE8) break;
		ctx.ufo = (uint8_t)ev_cur.what;
		ctx.flag = UFW_DETECTED | (ev_cur.qty ? UFW_HYPER : 0);
		UI_GO(A_PUSH, SCR_UFO_DETECTED);
		break;
	case GE_UFO_LOST:
		if (ST->ufo[ev_cur.what].type == NONE8) break;
		ctx.ufo = (uint8_t)ev_cur.what;
		UI_GO(A_PUSH, SCR_UFO_LOST);
		break;
	case GE_SITE:
		if (!ST->site[ev_cur.what].id) break;
		ctx.tidx = (uint8_t)ev_cur.what;
		UI_GO(A_PUSH, SCR_MISSION_DETECTED);
		break;
	case GE_ALIEN_BASE:
		ctx.tidx = (uint8_t)ev_cur.what;
		UI_GO(A_PUSH, SCR_ALIEN_BASE);
		break;
	case GE_BASE_ATTACK:                         // штурм базы: оборона, затем бой на базе или BaseDestroyed
		if (!ST->base[ev_cur.base].name[0] || ST->ufo[ev_cur.qty].type == NONE8) break;
		ctx.base = ev_cur.base;
		ctx.ufo = (uint8_t)ev_cur.qty;
		geo_base_attack();
		break;
	case GE_DOGFIGHT:                            // корабль догнал НЛО (df_start); base: 1/2 — ошибка TFTD
		if (dz_state != DZ_IN && dz_state != DZ_FIGHT) {   // первый бой: центр на корабль, наезд (startDogfight)
			geo_t p = ST->craft[(uint8_t)ev_cur.what].pos;
			center_on(&p);
			if (dz_state != DZ_OUT) dz_old = ST->zoom;
			dz_state = DZ_IN;
			ui_dirty(10);
		}
		if (ev_cur.base && ev_cur.base != NONE8) {
			ctx.craft = (uint8_t)ev_cur.what;
			ctx.item = ev_cur.base - 1;
			UI_GO(A_PUSH, SCR_DOGFIGHT_ERROR);
		} else if (df_nmax && dz_state != DZ_IN)   // окно боя — после наезда (такт геоскейпа)
			UI_GO(A_PUSH, SCR_DOGFIGHT);
		break;
	case GE_LANDING:
		ctx.craft = (uint8_t)ev_cur.what;
		UI_GO(A_PUSH, SCR_CONFIRM_LANDING);
		break;
	case GE_PATROL:
		ctx.craft = (uint8_t)ev_cur.what;
		ctx.tidx = (uint8_t)ev_cur.qty;
		UI_GO(A_PUSH, SCR_CRAFT_PATROL);
		break;
	case GE_LOW_FUEL:
		ctx.craft = (uint8_t)ev_cur.what;
		UI_GO(A_PUSH, SCR_LOW_FUEL);
		break;
	case GE_TARGET_LOST:                         // GeoscapeCraftState(craft, waypoint)
		ctx.craft = (uint8_t)ev_cur.what;
		ctx.tidx = (uint8_t)ev_cur.qty;
		ctx.ufo = ev_cur.base;
		ctx.flag = ev_cur.qty < MAX_WAYPOINTS ? GCW_LOST : 0;
		UI_GO(A_PUSH, SCR_GEO_CRAFT);
		break;
	}
}

// ---------------------------------------------------------------- геоскейп

static const wdef_t w_geo[] = {
	IMG(0, 0, 320, 200, RES_GEOBORD_SCR),
	BTNF(257, 0, 63, 11, UI_EL_BUTTON, STR_INTERCEPT, WF_GEO, A_CUSTOM, 12, 'i'),
	BTNF(257, 12, 63, 11, UI_EL_BUTTON, STR_BASES, WF_GEO, A_CUSTOM, 11, 'b'),
	BTNF(257, 24, 63, 11, UI_EL_BUTTON, STR_GRAPHS, WF_GEO, A_PUSH, SCR_GRAPHS, 'g'),
	BTNF(257, 36, 63, 11, UI_EL_BUTTON, STR_UFOPAEDIA_UC, WF_GEO, A_PUSH, SCR_UFOPAEDIA, 'u'),
	BTNF(257, 48, 63, 11, UI_EL_BUTTON, STR_OPTIONS_UC, WF_GEO, A_PUSH, SCR_PAUSE, ESC),
	BTNF(257, 60, 63, 11, UI_EL_BUTTON, STR_FUNDING_UC, WF_GEO, A_PUSH, SCR_FUNDING, 'f'),
	TGL(257, 112, 31, 13, UI_EL_BUTTON, STR_5_SECONDS, WF_GEO | BIG, 1, 0, '1'),
	TGL(289, 112, 31, 13, UI_EL_BUTTON, STR_1_MINUTE, WF_GEO | BIG, 1, 1, '2'),
	TGL(257, 126, 31, 13, UI_EL_BUTTON, STR_5_MINUTES, WF_GEO | BIG, 1, 2, '3'),
	TGL(289, 126, 31, 13, UI_EL_BUTTON, STR_30_MINUTES, WF_GEO | BIG, 1, 3, '4'),
	TGL(257, 140, 31, 13, UI_EL_BUTTON, STR_1_HOUR, WF_GEO | BIG, 1, 4, '5'),
	TGL(289, 140, 31, 13, UI_EL_BUTTON, STR_1_DAY, WF_GEO | BIG, 1, 5, '6'),
	HOT(259, 176, 12, 10, A_CUSTOM, 30, 0),      // вращение глобуса-заглушки
	HOT(283, 176, 12, 10, A_CUSTOM, 31, 0),
	HOT(271, 162, 13, 12, A_CUSTOM, 32, 0),
	HOT(271, 187, 13, 12, A_CUSTOM, 33, 0),
	HOT(295, 156, 23, 23, A_CUSTOM, 34, '+'),    // зум: keyGeoZoomIn / Out (ZX: SS+K / SS+J)
	HOT(300, 182, 13, 17, A_CUSTOM, 35, '-'),
	// Часы: высота рамок — по краске глифов (крупные цифры — строки до 85, мелкие — 81..86), а не
	// по кеглю: рамки 16 и 8 задевали строку 87, где начинается день недели, и каждый тик секунд
	// перерисовывал ещё и «SUNDAY» (mark_one: пересекающиеся тексты) — 18 % времени геоскейпа
	TXT(259, 74, 20, 12, UI_EL_TEXT, DYN(2), BIG | TR),
	TXT(279, 74, 4, 16, UI_EL_TEXT, DYN(3), BIG),
	TXT(283, 74, 20, 12, UI_EL_TEXT, DYN(4), BIG),
	TXT(303, 74, 4, 16, UI_EL_TEXT, DYN(3), BIG),
	TXT(307, 80, 11, 6, UI_EL_TEXT, DYN(5), 0),
	TXT(259, 87, 59, 8, UI_EL_TEXT, DYN(6), TC),
	TXT(259, 94, 29, 8, UI_EL_TEXT, DYN(7), TC),
	TXT(288, 94, 29, 8, UI_EL_TEXT, DYN(8), TC),
	TXT(259, 101, 59, 8, UI_EL_TEXT, DYN(9), TC),
	// глобус — последним: перерисовка идёт по порядку, а рендер длится 5–14 кадров; стоял бы
	// раньше текстов — на тике со сменой суток панель часов весь рендер висела бы стёртой
	CUS(0, 0, 256, 200, DYN(10), A_CUSTOM, 10),  // глобус: ui_dirty(10)
};

// Глобус — src/geoscape/earth/globe.c (банк 24): настоящая проекция OpenXcom, вид — ctx.globe_lon/lat
// и ST->zoom.
#define to_screen(p, x, y) globe_xy(p, x, y)
#define from_screen(x, y, p) globe_lonlat(x, y, p)

// Глобус (рендер при смене вида, иначе копия заднего буфера) и метки GlobeMarkers поверх (globe_det.c).
static void draw_globe(void)
{
	globe_det_check();
	globe_draw();
	globe_marks();
	if (df_count || icons_on) {                  // значки свёрнутых боёв (поверх глобуса; старые стёрла копия заднего буфера)
		icons_on = df_count != 0;
		if (icons_on) df_draw_icons();
	}
}

// Вращение на 15° / (зум + 1) (Globe::rotate делит шаг на zoom + 1): 0 — влево, 1 — вправо,
// 2 — вверх (к северу), 3 — вниз. Наклон — в пределах ±90° (у OpenXcom предела нет) и шаг по
// наклону меньше (ROTATE_LATITUDE 0.06 против ROTATE_LONGITUDE 0.10, Globe.cpp:60).
// Шагов за раз — сколько нажатий накопил автоповтор за перерисовку (in_reps): иначе из 14
// взведённых защёлок опрос забирал одну и глобус крутился в разы медленнее оригинала.
static const uint8_t rot_cap[GLOBE_ZOOMS] = { 3, 4, 4, 4, 3, 2 };   // ограничение скачка на экране
static void rotate(uint8_t d)
{
	uint8_t z = ST->zoom;
	if (z >= GLOBE_ZOOMS) z = GLOBE_ZOOMS - 1;
	uint8_t n = in_reps ? in_reps : 1, cap = rot_cap[z];
	if (n > cap) n = cap;
	int16_t st = 2731 / (z + 1);
	uint16_t lon0 = ctx.globe_lon;
	int16_t lat0 = ctx.globe_lat;
	if (d >= 2) st = (int16_t)((int32_t)st * 3 / 5);
	while (n--) {
		if (d == 0) ctx.globe_lon -= st;
		else if (d == 1) ctx.globe_lon += st;
		else if (d == 2) ctx.globe_lat = ctx.globe_lat > -16384 + st ? ctx.globe_lat - st : -16384;
		else ctx.globe_lat = ctx.globe_lat < 16384 - st ? ctx.globe_lat + st : 16384;
	}
	{                                            // по сетке предрасчитанных видов (наклон до ±27°)
		uint16_t nl = ctx.globe_lon;
		int16_t na = ctx.globe_lat;
		if (globe_snap(z, &nl, &na)) { ctx.globe_lon = nl; ctx.globe_lat = na; }
	}
	if (ctx.globe_lon == lon0 && ctx.globe_lat == lat0) return;   // упор в полюс — перерисовки нет
	ui_dirty(10);                                // только глобус (панель не меняется)
}

// Globe::zoomIn / zoomOut (кнопки GEOBORD и клавиши)
static void zoom(int8_t dz)
{
	uint8_t z = ST->zoom;
	if (dz > 0 && z < GLOBE_ZOOMS - 1) z++;
	else if (dz < 0 && z) z--;
	else return;
	ST->zoom = z;
	{                                            // сетки зумов не вложены — вид подтягивается
		uint16_t nl = ctx.globe_lon;
		int16_t na = ctx.globe_lat;
		if (globe_snap(z, &nl, &na)) { ctx.globe_lon = nl; ctx.globe_lat = na; }
	}
	ui_dirty(10);
}

// Globe::targetNear: dx² + dy² <= NEAR_RADIUS (25)
static uint8_t near(const geo_t *p)
{
	int16_t x, y;
	if (!to_screen(p, &x, &y)) return 0;
	x -= ui_click_x; y -= ui_click_y;
	return x >= -5 && x <= 5 && y >= -5 && y <= 5 && x * x + y * y <= 25;
}

static void mt_add(uint8_t kind, uint8_t idx)
{
	if (mt_n < MT_MAX) { mt_list[mt_n].kind = kind; mt_list[mt_n].idx = idx; mt_n++; }
}

// Globe::getTargets: базы и их корабли в полёте (craft = 0), НЛО, путевые точки, места
// миссий, базы пришельцев — в этом порядке
static void get_targets(uint8_t craft)
{
	mt_n = 0;
	if (!craft)
		for (uint8_t b = 0; b < MAX_BASES; b++) {
			if (!ST->base[b].name[0]) continue;
			if (near(&ST->base[b].pos)) mt_add(TGT_BASE, b);
			for (uint8_t c = 0; c < MAX_CRAFTS; c++) {
				craft_t *cr = &ST->craft[c];
				if (cr->type != NONE8 && cr->base == b && cr->status == CS_OUT && !cr->transit && near(&cr->pos)) mt_add(TGT_CRAFT, c);
			}
		}
	for (uint8_t u = 0; u < MAX_UFOS; u++)
		if (ST->ufo[u].type != NONE8 && (ST->ufo[u].flags & UF_DETECTED) && near(&ST->ufo[u].pos)) mt_add(TGT_UFO, u);
	for (uint8_t w = 0; w < MAX_WAYPOINTS; w++)
		if (ST->waypoint[w].id && ST->waypoint[w].id != WP_PENDING && near(&ST->waypoint[w].pos)) mt_add(TGT_WAYPOINT, w);
	for (uint8_t s = 0; s < MAX_SITES; s++)
		if (ST->site[s].id && (ST->site[s].flags & SITE_DETECTED) && near(&ST->site[s].pos)) mt_add(TGT_SITE, s);
	for (uint8_t b = 0; b < MAX_ALIEN_BASES; b++)
		if (ST->abase[b].id && (ST->abase[b].flags & AB_DISCOVERED) && near(&ST->abase[b].pos)) mt_add(TGT_ABASE, b);
}

// GeoscapeState::globeClick: одна цель — её окно, несколько — MultipleTargets
static void globe_click(void)
{
	if (df_count) {                              // значок свёрнутого боя
		uint8_t r = df_icon_click();
		if (r == 1) { ui_dirty(10); UI_GO(A_PUSH, SCR_DOGFIGHT); }   // значок убрать с глобуса
		if (r) return;
	}
	get_targets(0);
	mt_craft = NONE8;
	if (mt_n == 1) mt_popup(0, A_PUSH);
	else if (mt_n) UI_GO(A_PUSH, SCR_MULTI_TARGETS);
}

// SelectDestinationState::globeClick (выше y 28 — окно): нет цели — новая путевая точка,
// одна — ConfirmDestination, несколько — MultipleTargets для корабля
static void pick_target(void)
{
	if (ui_click_y < 28) return;
	get_targets(1);
	mt_craft = ctx.craft;
	if (mt_n == 1) { mt_popup(0, A_PUSH); return; }
	if (mt_n) { UI_GO(A_PUSH, SCR_MULTI_TARGETS); return; }
	mt_craft = NONE8;
	geo_t p;
	from_screen(ui_click_x, ui_click_y, &p);
	uint8_t w = waypoint_new(&p);
	if (w == NONE8) return;
	ctx.tkind = TGT_WAYPOINT;
	ctx.tidx = w;
	UI_GO(A_PUSH, SCR_CONFIRM_DEST);
}

static void geo_clock(uint8_t slot, char *buf)
{
	switch (slot) {
	case 2: fmt_num(buf, ST->hour, 0); break;
	case 3: strcpy(buf, ":"); break;
	case 4: buf[0] = '0' + ST->minute / 10; buf[1] = '0' + ST->minute % 10; buf[2] = 0; break;
	case 5: buf[0] = '0' + ST->second / 10; buf[1] = '0' + ST->second % 10; buf[2] = 0; break;
	case 6: str_copy(wday_str[(ST->weekday + 6) % 7], buf, 32); break;
	case 7: {
		uint8_t d = ST->day % 10;
		uint16_t s = (ST->day / 10 == 1 || d == 0 || d > 3) ? STR_DATE_FOURTH :
			d == 1 ? STR_DATE_FIRST : d == 2 ? STR_DATE_SECOND : STR_DATE_THIRD;
		fmt_num(t2, ST->day, 0);
		str_fmt(buf, str_get(s), t2, "");
		break;
	}
	case 8: str_copy(month_str[(ST->month + 11) % 12], buf, 32); break;
	case 9: fmt_num(buf, ST->year, 0); break;
	}
}

static const uint16_t alt_str[5] = { STR_GROUND, STR_VERY_LOW, STR_LOW_UC, STR_HIGH_UC, STR_VERY_HIGH };

static void fmt1s(char *buf, uint16_t pat, uint16_t arg)
{
	str_copy(arg, t2, sizeof t2);
	str_fmt(buf, str_get(pat), t2, "");
}

// ---------------------------------------------------------------- НЛО, миссия

// UfoDetectedState: кнопки — перехват (центр, 5 с, окно перехвата), центр, отмена;
// «DETECTED» — только во всплывшем при обнаружении (DYN(2)); с гиперволной — выше,
// второй список (тип, раса, миссия, зона) и своя палитра (setInterface(…, hyperwave)).
static const wdef_t w_ufo[] = {
	WINP(16, 44, 224, 128, UI_EL_WINDOW, POPB),
	BTN(28, 118, 200, 12, UI_EL_BUTTON, STR_INTERCEPT, A_CUSTOM, 2, 0),
	BTN(28, 134, 200, 12, UI_EL_BUTTON, STR_CENTER_ON_UFO_TIME_5_SECONDS, A_CUSTOM, 1, 0),
	BTN(28, 150, 200, 12, UI_EL_BUTTON, STR_CANCEL_UC, A_POP, 0, ESC),
	TXT(28, 53, 207, 17, UI_EL_TEXT, DYN(0), BIG),
	TXT(28, 69, 100, 9, UI_EL_TEXT, DYN(2), 0),
	LST(28, 80, 217, 32, UI_EL_TEXT, 1, 0),
};
static const wdef_t w_ufo_hyper[] = {
	WINP(16, 10, 224, 180, UI_EL_WINDOW, POPB),
	BTN(28, 136, 200, 12, UI_EL_BUTTON, STR_INTERCEPT, A_CUSTOM, 2, 0),
	BTN(28, 152, 200, 12, UI_EL_BUTTON, STR_CENTER_ON_UFO_TIME_5_SECONDS, A_CUSTOM, 1, 0),
	BTN(28, 168, 200, 12, UI_EL_BUTTON, STR_CANCEL_UC, A_POP, 0, ESC),
	TXT(28, 20, 207, 17, UI_EL_TEXT, DYN(0), BIG),
	TXT(28, 36, 100, 9, UI_EL_TEXT, DYN(2), 0),
	TXT(21, 44, 214, 17, UI_EL_TEXT, STR_HYPER_WAVE_TRANSMISSIONS_ARE_DECODED, TC | TW),
	LST(28, 60, 217, 32, UI_EL_TEXT, 1, 0),
	LST(28, 96, 217, 32, UI_EL_TEXT, 3, 0),
};
static const scr_t tab_hyper[] = {
	SCR(SCR_UFO_DETECTED, UI_SCR_UFOINFO, NOUI, RES_BACK15_SCR, SF_POPUP | SF_ALTPAL, w_ufo_hyper),
};

static const uint16_t heading_str[8] = {
	STR_NORTH, STR_NORTH_EAST, STR_EAST, STR_SOUTH_EAST, STR_SOUTH, STR_SOUTH_WEST, STR_WEST, STR_NORTH_WEST,
};

static const wdef_t w_ufolost[] = {
	WINP(32, 48, 192, 104, UI_EL_WINDOW, POPB),
	BTN(98, 112, 60, 12, UI_EL_BUTTON, STR_OK, A_POP, 0, ESC),
	TXT(48, 72, 160, 32, UI_EL_TEXT, DYN(0), BIG | TC),
};

// MissionDetectedState: фон и заголовок — из развёртывания (alertBackground, alert), город
static const wdef_t w_mission[] = {
	WINP(0, 0, 256, 200, UI_EL_WINDOW, POPB),
	BTN(28, 130, 200, 16, UI_EL_BUTTON, STR_INTERCEPT, A_CUSTOM, 2, 0),
	BTN(28, 150, 200, 16, UI_EL_BUTTON, STR_CENTER_ON_SITE_TIME_5_SECONDS, A_CUSTOM, 1, 0),
	BTN(28, 170, 200, 16, UI_EL_BUTTON, STR_CANCEL_UC, A_POP, 0, ESC),
	TXT(5, 48, 246, 32, UI_EL_TEXT, DYN(0), BIG | TC | TW),
	TXT(5, 80, 246, 17, UI_EL_TEXT, DYN(1), BIG | TC),
};

// ---------------------------------------------------------------- финансы, отчёт

static const wdef_t w_funding[] = {
	WINP(0, 0, 320, 200, UI_EL_WINDOW, POPB),
	BTN(135, 180, 50, 12, UI_EL_BUTTON, STR_OK, A_POP, 0, ESC),
	TXT(0, 8, 320, 17, UI_EL_TEXT1, STR_INTERNATIONAL_RELATIONS, BIG | TC),
	TXT(32, 30, 100, 9, UI_EL_TEXT2, STR_COUNTRY, 0),
	TXT(140, 30, 100, 9, UI_EL_TEXT2, STR_FUNDING, 0),
	TXT(240, 30, 72, 9, UI_EL_TEXT2, STR_CHANGE, 0),
	LST(32, 40, 260, 136, UI_EL_LIST, 0, 0),
};

// MonthlyReportState: OK — дальше PsiTraining (если есть пси-лаборатории) или, при
// поражении, тот же фон с «You have failed» (окно цветом color2, большая OK).
static const wdef_t w_failed[] = {
	WIN(0, 0, 320, 200, EL_RAW + 0),
	BTN(100, 174, 120, 18, UI_EL_BUTTON, STR_OK, A_CUSTOM, 1, ENT),
	TXT(15, 10, 290, 160, UI_EL_TEXT2, STR_YOU_HAVE_FAILED, BIG | TC | TM | TW),
	HOT(0, 0, 0, 0, A_CUSTOM, 1, ESC),
};

static const wdef_t w_report[] = {
	WIN(0, 0, 320, 200, UI_EL_WINDOW),
	BTN(135, 180, 50, 12, UI_EL_BUTTON, STR_OK, A_CUSTOM, 1, ESC),
	HOT(0, 0, 0, 0, A_CUSTOM, 1, ENT),
	TXT(16, 8, 300, 17, UI_EL_TEXT1, STR_XCOM_PROJECT_MONTHLY_REPORT, BIG),
	TXT(16, 24, 130, 9, UI_EL_TEXT1, DYN(0), 0),
	TXT(146, 24, 160, 9, UI_EL_TEXT1, DYN(1), 0),
	TXT(16, 32, 300, 9, UI_EL_TEXT1, DYN(2), 0),
	TXT(16, 40, 130, 9, UI_EL_TEXT1, DYN(3), 0),
	TXT(146, 40, 160, 9, UI_EL_TEXT1, DYN(4), 0),
	TXT(16, 48, 280, 132, UI_EL_TEXT2, DYN(5), TW),
};

// ---------------------------------------------------------------- база: имя, постройка

static const wdef_t w_basename[] = {
	WINP(32, 60, 192, 80, UI_EL_WINDOW, POPB),
	BTN(47, 118, 162, 12, UI_EL_BUTTON, STR_OK, A_CUSTOM, 1, ENT),
	TXT(37, 70, 182, 17, UI_EL_TEXT, STR_BASE_NAME, BIG | TC),
	{ W_EDIT, 59, 94, 127, 16, UI_EL_TEXT, NOSTR, BIG, 0, NAME_LEN - 1, 0 },
};

static const wdef_t w_buildbase[] = {
	GLOBE_HOTSPOTS,
	WIN(0, 0, 256, 28, UI_EL_GENERICWINDOW),
	BTN(186, 8, 54, 12, UI_EL_GENERICBUTTON2, STR_CANCEL_UC, A_CUSTOM, 2, ESC),
	TXT(8, 6, 180, 16, UI_EL_GENERICTEXT, STR_SELECT_SITE_FOR_NEW_BASE, TM | TW),
	HOT(0, 28, 256, 172, A_CUSTOM, 1, 0),
};

static const wdef_t w_confirmbase[] = {
	WIN(16, 64, 224, 72, UI_EL_GENERICWINDOW),
	BTN(68, 104, 54, 12, UI_EL_GENERICBUTTON2, STR_OK, A_CUSTOM, 1, ENT),
	BTN(138, 104, 54, 12, UI_EL_GENERICBUTTON2, STR_CANCEL_UC, A_POP, 0, ESC),
	TXT(68, 80, 120, 9, UI_EL_GENERICTEXT, DYN(0), 0),
	TXT(68, 90, 120, 9, UI_EL_GENERICTEXT, DYN(1), 0),
};

static int32_t region_cost(uint8_t r)
{
	rtab_t t;
	r_regions_t reg;
	reg.cost = 0;
	if (r != NONE8 && rtab_open(RES_RULE_REGIONS, &t)) rtab_get(&t, r, &reg);
	return (int32_t)reg.cost;
}

// ---------------------------------------------------------------- выбор цели

// SelectDestinationState: клик по глобусу — цель рядом (НЛО, место миссии, база
// пришельцев, путевая точка) или новая путевая точка -> ConfirmDestination (scr_fly.c).
// Кидония — только корабль-spacecraft и финальное исследование (14_todo §3.15).
static const wdef_t w_selectdest[] = {
	GLOBE_HOTSPOTS,
	WIN(0, 0, 256, 28, UI_EL_GENERICWINDOW),
	BTN(110, 8, 60, 12, UI_EL_GENERICBUTTON1, STR_CANCEL_UC, A_POP, 0, ESC),
	BTN(180, 8, 60, 12, UI_EL_GENERICBUTTON1, STR_CYDONIA, A_CUSTOM, 3, 0),
	TXT(10, 6, 100, 16, UI_EL_GENERICTEXT, STR_SELECT_DESTINATION, TM | TW),
	HOT(0, 28, 256, 172, A_CUSTOM, 1, 0),
};

// Кнопка Кидонии: корабль-spacecraft и исследована тема с unlockFinalMission
// (Mod::getFinalResearch)
static uint8_t cydonia(void)
{
	rtab_t t;
	rtab_open(RES_RULE_CRAFTS, &t);
	if (ST->craft[ctx.craft].type == NONE8
	    || !(rtab_word(&t, ST->craft[ctx.craft].type, offsetof(r_crafts_t, flags)) & CRAFTS_F_SPACECRAFT)) return 0;
	rtab_open(RES_RULE_RESEARCH, &t);
	for (uint16_t i = 0; i < t.n; i++)
		if ((rtab_word(&t, i, offsetof(r_research_t, flags)) & RESEARCH_F_UNLOCK_FINAL_MISSION) && res_done(i)) return 1;
	return 0;
}

// ---------------------------------------------------------------- таблица экранов

static const scr_t tab[] = {
	SCR(SCR_GEOSCAPE, UI_SCR_GEOSCAPE, NOUI, 0, 0, w_geo),
	SCR(SCR_UFO_DETECTED, UI_SCR_UFOINFO, NOUI, RES_BACK15_SCR, SF_POPUP, w_ufo),
	SCR(SCR_UFO_LOST, UI_SCR_UFOLOST, NOUI, RES_BACK15_SCR, SF_POPUP, w_ufolost),
	SCR(SCR_MISSION_DETECTED, UI_SCR_TERRORSITE, NOUI, RES_BACK03_SCR, SF_POPUP, w_mission),
	SCR(SCR_FUNDING, UI_SCR_FUNDINGWINDOW, NOUI, RES_BACK13_SCR, SF_POPUP, w_funding),
	SCR(SCR_MONTHLY_REPORT, UI_SCR_MONTHLYREPORT, NOUI, RES_BACK13_SCR, 0, w_report),
	SCR(SCR_REPORT_FAILED, UI_SCR_MONTHLYREPORT, NOUI, RES_BACK13_SCR, 0, w_failed),
	SCR(SCR_BASE_NAME, UI_SCR_BASENAMING, NOUI, RES_BACK01_SCR, SF_POPUP, w_basename),
	SCR(SCR_BUILD_NEW_BASE, UI_SCR_GEOSCAPE, NOUI, RES_BACK01_SCR, SF_POPUP, w_buildbase),
	SCR(SCR_CONFIRM_NEW_BASE, UI_SCR_GEOSCAPE, NOUI, RES_BACK01_SCR, SF_POPUP, w_confirmbase),
	SCR(SCR_SELECT_DEST, UI_SCR_GEOSCAPE, NOUI, RES_BACK01_SCR, SF_POPUP, w_selectdest),
};

uint8_t geo_get(uint8_t id, sdef_t *s, wdef_t *w) __banked
{
	if (id == SCR_REPORT_FAILED) {             // _window->setColor(window.color2)
		ui_raw[0][0] = ui_color_in(UI_SCR_MONTHLYREPORT, UI_EL_WINDOW, 1);
		ui_raw[0][1] = ui_raw[0][0];
	}
	if ((id == SCR_UFO_DETECTED || id == SCR_UFO_LOST) && ST->ufo[ctx.ufo].type == NONE8)
		ctx.ufo = any_ufo();                     // отладочное меню окон
	if (id == SCR_MISSION_DETECTED && !ST->site[ctx.tidx].id) any_site();
	if (id == SCR_UFO_DETECTED && (ctx.flag & UFW_HYPER)) return scr_find(tab_hyper, 1, id, s, w);
	if (!scr_find(tab, sizeof tab / sizeof tab[0], id, s, w)) return 0;
	if (id == SCR_SELECT_DEST && !cydonia()) w[8].type = 0;
	if (id == SCR_MISSION_DETECTED) {
		rtab_t t;
		site_t *st = &ST->site[ctx.tidx];
		rtab_open(RES_RULE_ALIENDEPLOYMENTS, &t);
		s->bg = rtab_word(&t, st->deployment, offsetof(r_alienDeployments_t, alert_background));
	}
	return 1;
}

// MonthlyReportState::countryList: «\n\n» + строка с одной страной или перечнем
// «A, B и C» (STR_COUNTRIES_COMMA / STR_COUNTRIES_AND).
static void country_list(char *buf, uint16_t mask, uint16_t one, uint16_t many)
{
	rtab_t t;
	uint8_t n = 0, k = 0;
	if (!mask) return;
	for (uint8_t i = 0; i < 16; i++) if (mask & (1u << i)) n++;
	static char lst[256];                      // 16 стран по ~16 символов — влезает
	char *tmp = big_tmp;
	rtab_open(RES_RULE_COUNTRIES, &t);
	strcat(buf, "\n\n");
	for (uint8_t i = 0; i < 16; i++) {
		if (!(mask & (1u << i))) continue;
		str_copy(rtab_word(&t, i, 0), t2, sizeof t2);
		if (!k++) strcpy(lst, t2);
		else {
			str_fmt(tmp, str_get(k == n ? STR_COUNTRIES_AND : STR_COUNTRIES_COMMA), lst, t2);
			strcpy(lst, tmp);
		}
	}
	str_fmt(buf + strlen(buf), str_get(n == 1 ? one : many), lst, "");
}

void geo_text(uint8_t id, uint8_t slot, uint8_t row, char *buf) __banked
{
	rtab_t t;
	switch (id) {
	case SCR_GEOSCAPE:
		geo_clock(slot, buf);
		break;
	case SCR_UFO_DETECTED: {
		ufo_t *u = &ST->ufo[ctx.ufo];
		if (slot == 0) { ufo_name(ctx.ufo, buf); break; }
		if (slot == 2) { if (ctx.flag & UFW_DETECTED) str_copy(STR_DETECTED, buf, 40); break; }
		if (row == LIST_COLS) {
			static const uint8_t cw[] = { 77, 140 }, ca[] = { LC_DOT, 0 };   // setDot
			list_cols(buf, 2, cw, ca);
			break;
		}
		if (slot == 1) {
			static const uint16_t lbl[4] = { STR_SIZE_UC, STR_ALTITUDE, STR_HEADING, STR_SPEED };
			str_copy(lbl[row], buf, 40);
			strcat(buf, "\t\x01");
			switch (row) {
			case 0: rtab_open(RES_RULE_UFOS, &t); str_copy(rtab_word(&t, u->type, offsetof(r_ufos_t, size)), t1, 40); break;
			case 1: {
				// на земле — «GROUNDED»; есть подводные корабли и НЛО не над водой — «AIRBORNE»
				uint16_t a = u->altitude == ALT_GROUND ? STR_GROUNDED : alt_str[u->altitude < 5 ? u->altitude : 0];
				geo_t pos = u->pos;
				if (underwater() && !inside_land(&pos)) a = STR_AIRBORNE;
				str_copy(a, t1, 40);
				break;
			}
			case 2: str_copy(u->status == US_FLYING && u->dir ? heading_str[u->dir - 1] : STR_NONE_UC, t1, 40); break;
			default: fmt_num(t1, u->speed, ',');
			}
			strcat(buf, t1);
		} else {                                  // гиперволна: тип, раса, миссия, зона
			static const uint16_t lbl[4] = { STR_CRAFT_TYPE, STR_RACE, STR_MISSION, STR_ZONE };
			mission_t *m = &ST->mission[u->mission];
			uint16_t n;
			switch (row) {
			case 0: n = rec_name(RES_RULE_UFOS, u->type); break;
			case 1: n = rec_name(RES_RULE_ALIENRACES, m->race); break;
			case 2: n = rec_name(RES_RULE_ALIENMISSIONS, m->type); break;
			default: n = rec_name(RES_RULE_REGIONS, m->region);
			}
			str_copy(lbl[row], buf, 40);
			strcat(buf, "\t\x01");
			str_copy(n, buf + strlen(buf), 64);
		}
		break;
	}
	case SCR_UFO_LOST:
		ufo_name(ctx.ufo, buf);
		strcat(buf, "\n");
		str_copy(STR_TRACKING_LOST, buf + strlen(buf), 64);
		break;
	case SCR_MISSION_DETECTED: {
		site_t *st = &ST->site[ctx.tidx];
		if (slot == 0) {
			rtab_open(RES_RULE_ALIENDEPLOYMENTS, &t);
			str_copy(rtab_word(&t, st->deployment, offsetof(r_alienDeployments_t, alert)), buf, 256);
		} else if (st->city != NONE16)
			str_copy(st->city, buf, 64);
		break;
	}
	case SCR_FUNDING: {
		rtab_open(RES_RULE_COUNTRIES, &t);
		if (row == LIST_COLS) {                   // FundingState: setDot(true)
			static const uint8_t cw[] = { 108, 100, 52 }, ca[] = { LC_DOT, LC_DOT, 0 };
			list_cols(buf, 3, cw, ca);
			break;
		}
		uint8_t m = ST->hist_len - 1, nc = t.n > MAX_COUNTRIES ? MAX_COUNTRIES : (uint8_t)t.n;
		int32_t f = 0;
		if (row >= nc) {                          // TOTAL: только сумма, цвет заголовка колонки
			for (uint8_t i = 0; i < nc; i++) f += (int32_t)ST->country[i].funding[m] * 1000;
			buf[0] = 0x03;
			buf[1] = (char)ui_color(UI_EL_TEXT2, 0);
			str_copy(STR_TOTAL_UC, buf + 2, 64);
			strcat(buf, "\t");
			fmt_funds(buf + strlen(buf), f);
			break;
		}
		str_copy(rtab_word(&t, row, 0), buf, 64);
		f = (int32_t)ST->country[row].funding[m] * 1000;
		strcat(buf, "\t\x01");
		fmt_funds(buf + strlen(buf), f);
		strcat(buf, "\x01\t");
		if (m) {                                  // изменение: {ALT}, '+' у роста
			int32_t ch = f - (int32_t)ST->country[row].funding[m - 1] * 1000;
			strcat(buf, ch > 0 ? "\x01+" : "\x01");
			fmt_funds(buf + strlen(buf), ch);
			strcat(buf, "\x01");
		} else
			fmt_funds(buf + strlen(buf), 0);
		break;
	}
	case SCR_MONTHLY_REPORT: {                    // итоги — month.c (report)
		int32_t rt = report.rating, th = report.threshold;
		switch (slot) {
		case 0:
			str_copy(month_str[(ST->month + 10) % 12], t1, 32);
			fmt_num(t2, ST->month == 1 ? ST->year - 1 : ST->year, 0);
			str_fmt(buf, str_get(STR_MONTH), t1, t2);
			break;
		case 1: {
			uint16_t r = rt > 500 ? STR_RATING_EXCELLENT : rt > 0 ? STR_RATING_GOOD
				: rt > th ? STR_RATING_OK : rt > th - 300 ? STR_RATING_POOR : STR_RATING_TERRIBLE;
			fmt_num(t1, rt, 0);
			str_copy(r, t2, sizeof t2);
			str_fmt(buf, str_get(STR_MONTHLY_RATING), t1, t2);
			break;
		}
		case 2:                                   // «Income> {ALT}$x (+$d)» — финансирование нового месяца
			str_copy(STR_INCOME, buf, 40);
			strcat(buf, "> \x01");
			fmt_funds(buf + strlen(buf), report.income);
			strcat(buf, report.diff > 0 ? " (+" : " (");
			fmt_funds(buf + strlen(buf), report.diff);
			strcat(buf, ")");
			break;
		case 3:
			str_copy(STR_MAINTENANCE, buf, 40);
			strcat(buf, "> \x01");
			fmt_funds(buf + strlen(buf), report.maintenance);
			break;
		case 4:
			str_copy(STR_BALANCE, buf, 40);
			strcat(buf, "> \x01");
			fmt_funds(buf + strlen(buf), ST->funds);
			break;
		default:
			if (report.flags & RF_GAME_OVER) { str_copy(STR_YOU_HAVE_NOT_SUCCEEDED, buf, 256); break; }
			str_copy(rt > 500 ? STR_COUNCIL_IS_VERY_PLEASED : rt > th ? STR_COUNCIL_IS_GENERALLY_SATISFIED
				: STR_COUNCIL_IS_DISSATISFIED, buf, 256);
			if (report.flags & RF_DEBTS) { strcat(buf, "\n\n"); str_copy(STR_COUNCIL_REDUCE_DEBTS, buf + strlen(buf), 200); }
			country_list(buf, report.happy, STR_COUNTRY_IS_PARTICULARLY_PLEASED, STR_COUNTRIES_ARE_PARTICULARLY_HAPPY);
			country_list(buf, report.sad, STR_COUNTRY_IS_UNHAPPY_WITH_YOUR_ABILITY, STR_COUNTRIES_ARE_UNHAPPY_WITH_YOUR_ABILITY);
			country_list(buf, report.pact, STR_COUNTRY_HAS_SIGNED_A_SECRET_PACT, STR_COUNTRIES_HAVE_SIGNED_A_SECRET_PACT);
		}
		break;
	}
	case SCR_CONFIRM_NEW_BASE: {
		uint8_t r = region_at((uint16_t)((uint32_t)ctx.pick.lon >> 16), (int16_t)(ctx.pick.lat >> 16));
		if (slot == 0) { fmt_funds(t1, region_cost(r)); str_fmt(buf, str_get(STR_COST_), t1, ""); }
		else if (r != NONE8) fmt1s(buf, STR_AREA_, rec_name(RES_RULE_REGIONS, r));
		break;
	}
	}
}

uint8_t geo_rows(uint8_t id, uint8_t slot) __banked
{
	(void)slot;
	switch (id) {
	case SCR_UFO_DETECTED: return 4;
	case SCR_FUNDING: return (uint8_t)tab_count(RES_RULE_COUNTRIES) + 1;
	}
	return 0;
}

uint8_t geo_event(uint8_t id, uint8_t ev, uint8_t arg) __banked
{
	switch (id) {
	case SCR_GEOSCAPE:
		if (ev == EVT_QUERY) return arg == ST->speed;
		if (ev == EVT_DRAW) { draw_globe(); break; }
		if (ev == EVT_BUTTON) {
			if (arg < 6) ST->speed = arg;          // переключатели скорости
			else if (arg == 10) globe_click();
			else if (arg == 11) { ST->speed = 0; ctx.base = ST->sel_base; UI_GO(A_PUSH, SCR_BASESCAPE); }   // timerReset
			else if (arg == 12) { ctx.tkind = TGT_NONE; ctx.flag = 0; UI_GO(A_PUSH, SCR_INTERCEPT); }   // без цели -> выбор цели
			else if (arg >= 30 && arg <= 35) {
				if (arg <= 33) rotate(arg - 30);
				else zoom(arg == 34 ? 1 : -1);
				rep_arg = arg;                       // удержание мышью — повтор через 12 кадров
				rep_at = frames + 12;
			}
		}
		if (ev == EVT_KEY) {                       // стрелки ZX (CS+5..8) — вращение (keyGeoLeft …)
			static const uint8_t dir[4] = { 0, 3, 2, 1 };   // влево, вниз, вверх, вправо
			if (arg >= KEY_LEFT && arg <= KEY_RIGHT) rotate(dir[arg - KEY_LEFT]);
			break;
		}
		if (ev == EVT_TICK) {
			globe_blink();                           // Globe::blink — раз в 100 мс
			if (rep_arg) {                           // кнопка поворота / зума держится
				uint8_t a = rep_arg;
				const uint8_t *r = rep_box[a - 30];
				if (!(mouse_buttons & 1)) rep_arg = 0;
				else if ((int16_t)(frames - rep_at) >= 0 && cursor_x >= r[0] && cursor_x < r[0] + r[2] &&
					cursor_y >= r[1] && cursor_y < r[1] + r[3]) {
					rep_at = frames + 2;
					if (a <= 33) rotate(a - 30);
					else zoom(a == 34 ? 1 : -1);
				}
			}
			if (dz_state == DZ_IN) {                 // наезд: зум за такт (перерисовка между ними), время стоит
				if (ST->zoom < DOGFIGHT_ZOOM) { zoom(1); return 0; }
				dz_state = DZ_FIGHT;
			}
			if (dz_state == DZ_FIGHT && !df_count) dz_state = DZ_OUT;
			if (dz_state == DZ_OUT) {                // бои кончились — отдаление к прежнему зуму
				if (ST->zoom > dz_old) { zoom(-1); return 0; }
				dz_state = 0;
			}
			if (geo_dbg_attack) { dbg_attack(geo_dbg_attack - 1); geo_dbg_attack = 0; }
			if (gev_n) { show_event(); break; }     // сначала — непоказанные события
			if (df_count) {                          // бои: развёрнутый — окна, время стоит
				if (!df_nmax && df_run()) ui_dirty(10);   // свёрнутые: конец боя или разворот — значки
				if (df_nmax) { if (icons_on) ui_dirty(10); UI_GO(A_PUSH, SCR_DOGFIGHT); break; }
			}
			// по кадрам, а не по вызовам: долгий такт (день на «1 день») — следующий сразу.
			// Догон: пока шла перерисовка глобуса (до 28 кадров), такты пропущены — сделать
			// их за один проход, иначе на быстрых скоростях время идёт в разы медленнее
			uint16_t now = frames, last = tick_at;
			uint8_t due = (uint8_t)(((uint16_t)(now - last)) / 5);
			if (!due) break;
			uint8_t cap = ST->speed >= 4 ? 1 : 4;     // «1 час» и «1 день» — по такту за проход
			if (due > cap) due = cap;
			tick_at = last + (uint16_t)due * 5;
			uint8_t mi = ST->minute, s = ST->second, h = ST->hour, d = ST->day;
			while (due--)
				if (game_advance(step_sec[ST->speed], step_min[ST->speed])) { show_event(); break; }
			// только изменившиеся поля часов (раньше — все 9 текстов на каждый тик)
			if (ST->second != s) ui_dirty(5);
			if (ST->minute != mi) ui_dirty(4);
			if (ST->hour != h) ui_dirty(2);
			if (ST->day != d) { ui_dirty(6); ui_dirty(7); ui_dirty(8); ui_dirty(9); }
			// тень: солнце сдвинулось на эпоху (0.7°) — глобус перерисовать
			uint8_t se = (uint8_t)(globe_sunlon() >> 7), gs = geo_sun;
			if (se != gs) { geo_sun = se; ui_dirty(10); }
			return 0;
		}
		break;
	case SCR_UFO_DETECTED:                        // 1 — центр, 2 — перехват (центр, окно перехвата поверх)
		if (ev == EVT_BUTTON) {
			geo_t p = ST->ufo[ctx.ufo].pos;
			center_on(&p);
			if (arg == 2) { ctx.tkind = TGT_UFO; ctx.tidx = ctx.ufo; UI_GO(A_PUSH, SCR_INTERCEPT); }
			else UI_GO(A_POP, 0);
		}
		break;
	case SCR_MISSION_DETECTED:
		if (ev == EVT_BUTTON) {
			geo_t p = ST->site[ctx.tidx].pos;
			center_on(&p);
			if (arg == 2) { ctx.tkind = TGT_SITE; UI_GO(A_PUSH, SCR_INTERCEPT); }
			else UI_GO(A_POP, 0);
		}
		break;
	case SCR_SELECT_DEST:                         // globeClick: цель рядом или путевая точка
		if (ev == EVT_BUTTON && arg == 1) pick_target();
		else if (ev == EVT_BUTTON && arg == 3) {  // btnCydoniaClick: только с десантом
			uint8_t c = ctx.craft, n = soldiers_count(ST->craft[c].base, c, 0);
			uint8_t cg = ST->craft[c].cargo;
			if (cg != NONE8) for (uint8_t k = 0; k < CARGO_VEH; k++) if (ST->cargo[cg].veh[k].type != NONE8) n++;
			if (n) UI_GO(A_PUSH, SCR_CONFIRM_CYDONIA);
		}
		break;
	case SCR_MONTHLY_REPORT:                      // btnOkClick (Ironman — автосохранение)
		if (ev != EVT_BUTTON) break;
		st_ironsave();
		if (report.flags & RF_GAME_OVER) UI_GO(A_POP_PUSH, SCR_REPORT_FAILED);
		else if (report.flags & RF_PSI) UI_GO(A_POP_PUSH, SCR_PSI_TRAINING);
		else UI_GO(A_POP, 0);
		break;
	case SCR_REPORT_FAILED:                       // CutsceneState(LOSE_GAME), затем статистика
		if (ev == EVT_BUTTON) cut_play(CUT_LOSE, SCR_STATISTICS);
		break;
	case SCR_BUILD_NEW_BASE:
		if (ev == EVT_BUTTON && arg == 2) {       // отмена: первую базу поставить обязательно
			if (ST->months >= 0) UI_GO(A_POP, 0);
		} else if (ev == EVT_BUTTON && arg == 1) {
			from_screen(ui_click_x, ui_click_y, &ctx.pick);
			if (ST->months < 0) {                  // первая база — бесплатно, сразу имя
				ctx.base = 0;
				ST->base[0].pos = ctx.pick;
				for (uint8_t c = 0; c < MAX_CRAFTS; c++) if (ST->craft[c].type != NONE8) ST->craft[c].pos = ctx.pick;
				UI_GO(A_PUSH, SCR_BASE_NAME);
			} else
				UI_GO(A_PUSH, SCR_CONFIRM_NEW_BASE);
		}
		break;
	case SCR_CONFIRM_NEW_BASE:
		if (ev == EVT_BUTTON && arg == 1) {
			int32_t cost = region_cost(region_at((uint16_t)((uint32_t)ctx.pick.lon >> 16), (int16_t)(ctx.pick.lat >> 16)));
			uint8_t b = base_alloc();
			if (ST->funds < cost) { str_copy(STR_NOT_ENOUGH_MONEY, ui_msg, sizeof ui_msg); UI_GO(A_POP_PUSH, SCR_ERROR); break; }
			if (b == NONE8) { UI_GO(A_POP, 0); break; }
			funds_add(-cost);
			ST->base[b].pos = ctx.pick;
			ctx.base = b;
			UI_GO(A_PUSH, SCR_BASE_NAME);
		}
		break;
	case SCR_BASE_NAME:
		if (ev == EVT_OPEN) ui_edit[0] = 0;
		else if (ev == EVT_BUTTON && arg == 1 && ui_edit[0]) {
			strncpy(ST->base[ctx.base].name, ui_edit, NAME_LEN - 1);
			ST->base[ctx.base].name[NAME_LEN - 1] = 0;
			ST->sel_base = ctx.base;
			if (ST->months < 0) {                  // первая база: кампания началась
				game_start();
				ctx.globe_lon = (uint16_t)((uint32_t)ST->base[0].pos.lon >> 16);
				ctx.globe_lat = (int16_t)(ST->base[0].pos.lat >> 16);
				UI_GO(A_SET, SCR_GEOSCAPE);
				if (ST->ironman) ui_request = SCR_SAVE;   // GeoscapeState::init: слот Ironman сразу

			} else {                                // новая база: место лифта, затем её экран
				UI_GO(A_SET, SCR_GEOSCAPE);
				ui_request = SCR_PLACE_LIFT;
			}
		}
		break;
	}
	return 0;
}
