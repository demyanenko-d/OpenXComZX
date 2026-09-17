// Банк 21: окна полёта кораблей (InterceptState, GeoscapeCraftState, TargetInfoState,
// ConfirmDestinationState, CraftPatrolState, LowFuelState, CraftErrorState,
// MultipleTargetsState). Логика полёта — src/game/craft.c. Выбор цели на глобусе
// (SelectDestinationState) — в scr_geo.c: он знает проекцию глобуса.
//
// Цель — ctx.tkind (TGT_* = DK_*) и ctx.tidx; корабль — ctx.craft. Стек окон как в
// OpenXcom: перехват поверх окна цели, ConfirmDestination OK закрывает два окна.
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "res.h"
#include "rules.h"
#include "text.h"
#include "state.h"
#include "game.h"
#include "scrdef.h"

static char t1[80], t2[80];

static uint16_t rec_name(uint16_t table, uint16_t i)
{
	rtab_t t;
	if (!rtab_open(table, &t) || i >= t.n) return 0xFFFF;
	return rtab_word(&t, i, 0);
}

static void fmt1n(char *buf, uint16_t pat, int32_t v)
{
	fmt_num(t2, v, ',');
	str_fmt(buf, str_get(pat), t2, "");
}

static void fmt1s(char *buf, uint16_t pat, uint16_t arg)
{
	str_copy(arg, t2, sizeof t2);
	str_fmt(buf, str_get(pat), t2, "");
}

static const uint16_t status_str[5] = { STR_READY, STR_OUT, STR_REPAIRS, STR_REFUELLING, STR_REARMING };
static const uint16_t alt_str[5] = { STR_GROUND, STR_VERY_LOW, STR_LOW_UC, STR_HIGH_UC, STR_VERY_HIGH };

// k-й корабль (не в пути; открыто с базы — только её), NONE8 — нет; k = NONE8 — число
static uint8_t craft_nth(uint8_t k)
{
	uint8_t b = (ctx.flag & ICW_BASE) ? ctx.base : NONE8, n = 0;
	for (uint8_t c = 0; c < MAX_CRAFTS; c++) {
		craft_t *cr = &ST->craft[c];
		if (cr->type == NONE8 || cr->transit || (b != NONE8 && cr->base != b)) continue;
		if (n++ == k) return c;
	}
	return k == NONE8 ? n : NONE8;
}

static uint8_t craft_vehicles(uint8_t c)
{
	uint8_t n = 0, cg = ST->craft[c].cargo;
	if (cg != NONE8)
		for (uint8_t k = 0; k < CARGO_VEH; k++) if (ST->cargo[cg].veh[k].type != NONE8) n++;
	return n;
}

// Имя цели (Target::getName): НЛО, место миссии (markerName + номер), база
// пришельцев (STR_ALIEN_BASE_), путевая точка (STR_WAY_POINT_; новая — STR_WAY_POINT).
// Зовут и окна других банков: buf — в Win1 или на стеке.
void target_name(uint8_t kind, uint8_t idx, char *buf) __banked
{
	rtab_t t;
	uint16_t id = 0, s = STR_WAY_POINT_;
	switch (kind) {
	case TGT_UFO: ufo_name(idx, buf); return;
	case TGT_BASE: strcpy(buf, ST->base[idx].name); return;
	case TGT_CRAFT: craft_name(idx, buf); return;
	case TGT_WAYPOINT:
		if (ST->waypoint[idx].id == WP_PENDING) { str_copy(STR_WAY_POINT, buf, 64); return; }
		id = ST->waypoint[idx].id;
		break;
	case TGT_SITE:
		id = ST->site[idx].id;
		rtab_open(RES_RULE_ALIENDEPLOYMENTS, &t);
		s = rtab_word(&t, ST->site[idx].deployment, offsetof(r_alienDeployments_t, marker_name));
		break;
	case TGT_ABASE:
		id = ST->abase[idx].id;
		s = STR_ALIEN_BASE_;
		break;
	default:
		buf[0] = 0;
		return;
	}
	fmt_num(t1, id, 0);
	str_fmt(buf, str_get(s), t1, "");
}

static void center_on(const geo_t *p)
{
	ctx.globe_lon = (uint16_t)((uint32_t)p->lon >> 16);
	ctx.globe_lat = (int16_t)(p->lat >> 16);
}

// ---------------------------------------------------------------- окна

static const wdef_t w_intercept[] = {
	WINP(0, 30, 320, 140, UI_EL_WINDOW, POPH),
	BTN(16, 146, 288, 16, UI_EL_BUTTON, STR_CANCEL, A_POP, 0, ESC),
	TXT(10, 46, 300, 17, UI_EL_TEXT1, STR_LAUNCH_INTERCEPTION, BIG | TC),
	TXT(14, 70, 86, 9, UI_EL_TEXT2, STR_CRAFT, 0),
	TXT(100, 70, 70, 9, UI_EL_TEXT2, STR_STATUS, 0),
	TXT(170, 70, 80, 9, UI_EL_TEXT2, STR_BASE, 0),
	TXT(238, 62, 80, 17, UI_EL_TEXT2, STR_WEAPONS_CREW_HWPS, 0),
	LST(8, 78, 288, 64, UI_EL_LIST, 0, WF_SEL | WF_RSEL),
	HOT(0, 0, 0, 0, A_POP, 0, 'i'),
};

// GeoscapeCraftState: кнопки 1–4 — к базе, новая цель, патруль, отмена / к последней
// позиции НЛО (цель потеряна: GCW_LOST, путевая точка ctx.tidx)
static const wdef_t w_craft[] = {
	WINP(8, 8, 240, 184, UI_EL_WINDOW, POPB),
	BTN(22, 124, 212, 12, UI_EL_BUTTON, STR_RETURN_TO_BASE, A_CUSTOM, 1, 0),
	BTN(22, 140, 212, 12, UI_EL_BUTTON, STR_SELECT_NEW_TARGET, A_CUSTOM, 2, 0),
	BTN(22, 156, 212, 12, UI_EL_BUTTON, STR_PATROL, A_CUSTOM, 3, 0),
	BTN(22, 172, 212, 12, UI_EL_BUTTON, STR_CANCEL_UC, A_CUSTOM, 4, ESC),
	TXT(32, 20, 210, 17, UI_EL_TEXT1, DYN(0), BIG),
	TXT(32, 36, 210, 17, UI_EL_TEXT1, DYN(1), TW),
	TXT(32, 52, 210, 9, UI_EL_TEXT3, DYN(2), 0),
	TXT(32, 60, 210, 9, UI_EL_TEXT3, DYN(3), 0),
	TXT(32, 68, 210, 9, UI_EL_TEXT3, DYN(4), 0),
	TXT(32, 76, 210, 9, UI_EL_TEXT3, DYN(5), 0),
	TXT(32, 84, 130, 9, UI_EL_TEXT3, DYN(6), 0),
	TXT(164, 84, 80, 9, UI_EL_TEXT3, DYN(7), 0),
	TXT(32, 92, 130, 9, UI_EL_TEXT3, DYN(8), 0),
	TXT(164, 92, 80, 9, UI_EL_TEXT3, DYN(9), 0),
	TXT(32, 100, 130, 9, UI_EL_TEXT3, DYN(10), 0),
	TXT(164, 100, 80, 9, UI_EL_TEXT3, DYN(11), 0),
	TXT(13, 108, 230, 17, UI_EL_TEXT3, STR_REDIRECT_CRAFT, BIG | TC),
	TXT(164, 68, 80, 9, UI_EL_TEXT3, DYN(12), 0),
	TXT(164, 76, 80, 9, UI_EL_TEXT3, DYN(13), 0),
};
#define GC_BTN_BASE   1
#define GC_BTN_CANCEL 4
#define GC_REDIRECT   17
#define GC_SOLDIERS   18

static const wdef_t w_target[] = {
	WINP(32, 40, 192, 120, UI_EL_WINDOW, POPB),
	BTN(48, 124, 160, 12, UI_EL_BUTTON, STR_INTERCEPT, A_PUSH, SCR_INTERCEPT, 0),
	BTN(48, 140, 160, 12, UI_EL_BUTTON, STR_OK, A_POP, 0, ESC),
	TXT(37, 46, 182, 32, UI_EL_TEXT2, DYN(0), BIG | TC | TM | TW),
	TXT(37, 78, 182, 9, UI_EL_TEXT1, STR_TARGETTED_BY, TC),
	TXT(37, 88, 182, 40, UI_EL_TEXT1, DYN(1), TC),
};

static const wdef_t w_confirmdest[] = {
	WIN(6, 64, 244, 72, UI_EL_WINDOW),
	BTN(68, 104, 50, 12, UI_EL_BUTTON, STR_OK, A_CUSTOM, 1, ENT),
	BTN(138, 104, 50, 12, UI_EL_BUTTON, STR_CANCEL_UC, A_CUSTOM, 2, ESC),
	TXT(12, 72, 232, 32, UI_EL_TEXT, DYN(0), BIG | TC | TM | TW),
};

static const wdef_t w_patrol[] = {
	WINP(16, 16, 224, 168, UI_EL_WINDOW, POPB),
	BTN(58, 144, 140, 12, UI_EL_BUTTON, STR_OK, A_POP, 0, ESC),
	BTN(58, 160, 140, 12, UI_EL_BUTTON, STR_REDIRECT_CRAFT, A_CUSTOM, 1, ENT),
	TXT(16, 48, 224, 64, UI_EL_TEXT1, DYN(0), BIG | TC | TW),
	TXT(16, 120, 224, 17, UI_EL_TEXT1, STR_NOW_PATROLLING, BIG | TC),
};

static const wdef_t w_lowfuel[] = {
	WINP(16, 40, 224, 120, UI_EL_WINDOW, POPB),
	BTN(30, 120, 90, 18, UI_EL_BUTTON, STR_OK, A_POP, 0, ESC),
	BTN(136, 120, 90, 18, UI_EL_BUTTON, STR_OK_5_SECONDS, A_CUSTOM, 1, ENT),
	TXT(21, 60, 214, 17, UI_EL_TEXT, DYN(0), BIG | TC),
	TXT(21, 90, 214, 17, UI_EL_TEXT, STR_IS_LOW_ON_FUEL_RETURNING_TO_BASE, TC),
};

static const wdef_t w_crafterr[] = {
	WINP(32, 20, 256, 160, UI_EL_WINDOW, POPB),
	BTN(48, 150, 100, 18, UI_EL_BUTTON, STR_OK, A_POP, 0, ESC),
	BTN(172, 150, 100, 18, UI_EL_BUTTON, STR_OK_5_SECONDS, A_CUSTOM, 1, ENT),
	TXT(37, 42, 246, 96, UI_EL_TEXT1, DYN(0), BIG | TC | TM | TW),
};

// MultipleTargetsState: кнопки целей mt_list (116x16 через 4, поля 10) — fly_get
static const wdef_t w_multi[] = {
	WINP(60, 62, 136, 16, UI_EL_WINDOW, POPV),
	HOT(0, 0, 0, 0, A_POP, 0, ESC),
};

mtarget_t mt_list[MT_MAX];
uint8_t mt_n, mt_craft = NONE8;

// MultipleTargetsState::popupTarget: окно цели или — выбор цели корабля — подтверждение
void mt_popup(uint8_t i, uint8_t op) __banked
{
	uint8_t k = mt_list[i].kind, x = mt_list[i].idx, c = mt_craft, scr;
	mt_craft = NONE8;
	if (c != NONE8) { ctx.craft = c; ctx.tkind = k; ctx.tidx = x; scr = SCR_CONFIRM_DEST; }
	else if (k == TGT_BASE) { ctx.base = x; ctx.tkind = TGT_NONE; ctx.flag = ICW_BASE; scr = SCR_INTERCEPT; }
	else if (k == TGT_CRAFT) { ctx.craft = x; ctx.flag = 0; scr = SCR_GEO_CRAFT; }
	else if (k == TGT_UFO) {                      // UfoDetectedState(detected = false, hyper как у НЛО)
		ctx.ufo = x;
		ctx.flag = (ST->ufo[x].flags & UF_HYPER) ? UFW_HYPER : 0;
		scr = SCR_UFO_DETECTED;
	} else { ctx.tkind = k; ctx.tidx = x; scr = SCR_TARGET_INFO; }
	UI_GO(op, scr);
}

static const scr_t tab[] = {
	SCR(SCR_INTERCEPT, UI_SCR_INTERCEPT, NOUI, RES_BACK12_SCR, SF_POPUP, w_intercept),
	SCR(SCR_GEO_CRAFT, UI_SCR_GEOCRAFT, NOUI, RES_BACK12_SCR, SF_POPUP, w_craft),
	SCR(SCR_TARGET_INFO, UI_SCR_TARGETINFO, NOUI, RES_BACK01_SCR, SF_POPUP, w_target),
	SCR(SCR_CONFIRM_DEST, UI_SCR_CONFIRMDESTINATION, NOUI, RES_BACK12_SCR, SF_POPUP, w_confirmdest),
	SCR(SCR_CRAFT_PATROL, UI_SCR_CRAFTPATROL, NOUI, RES_BACK12_SCR, SF_POPUP, w_patrol),
	SCR(SCR_LOW_FUEL, UI_SCR_LOWFUEL, NOUI, RES_BACK12_SCR, SF_POPUP, w_lowfuel),
	SCR(SCR_CRAFT_ERROR, UI_SCR_CRAFTERROR, NOUI, RES_BACK12_SCR, SF_POPUP, w_crafterr),
	SCR(SCR_MULTI_TARGETS, UI_SCR_MULTIPLETARGETS, NOUI, RES_BACK15_SCR, SF_POPUP, w_multi),
};

uint8_t fly_get(uint8_t id, sdef_t *s, wdef_t *w) __banked
{
	if (!scr_find(tab, sizeof tab / sizeof tab[0], id, s, w)) return 0;
	if (id == SCR_INTERCEPT && (ctx.flag & ICW_BASE)) {   // с базы: «Отмена» уже, «К базе»
		wdef_t *d = &w[s->n++];
		w[1].w = 142;
		d->type = W_BUTTON; d->x = 162; d->y = 146; d->w = 142; d->h = 16;
		d->el = UI_EL_BUTTON; d->str = STR_GO_TO_BASE; d->flags = 0;
		d->act = A_CUSTOM; d->arg = 1; d->key = 0;
	}
	if (id == SCR_GEO_CRAFT) {
		craft_t *cr = &ST->craft[ctx.craft];
		if (cr->type == NONE8) return 0;
		rtab_t t;
		rtab_open(RES_RULE_CRAFTS, &t);
		if (!(rtab_word(&t, cr->type, offsetof(r_crafts_t, soldiers)) & 0xFF)) w[GC_SOLDIERS].type = 0;
		if (!(rtab_word(&t, cr->type, offsetof(r_crafts_t, vehicles)) & 0xFF)) w[GC_SOLDIERS + 1].type = 0;
		if (ctx.flag & GCW_LOST) w[GC_BTN_CANCEL].str = STR_GO_TO_LAST_KNOWN_UFO_POSITION;
		else w[GC_REDIRECT].type = 0;
		if (cr->flags & (CRF_LOWFUEL | CRF_MISSION))   // к базе / цель / патруль — скрыты
			for (uint8_t i = GC_BTN_BASE; i < GC_BTN_CANCEL; i++) w[i].type = 0;
	}
	if (id == SCR_MULTI_TARGETS) {                // окно по числу целей, по центру по вертикали
		if (mt_n < 2) {                           // отладочное меню окон: корабль, НЛО, база
			mt_list[0].kind = TGT_CRAFT; mt_list[0].idx = 0;
			mt_list[1].kind = TGT_UFO; mt_list[1].idx = 0;
			mt_list[2].kind = TGT_BASE; mt_list[2].idx = 0;
			mt_n = 3;
		}
		int16_t h = 16 * mt_n + 4 * (mt_n - 1) + 20, y = (200 - h) / 2;
		w[0].y = y; w[0].h = h;
		for (uint8_t i = 0; i < mt_n; i++) {
			wdef_t *d = &w[s->n++];
			d->type = W_BUTTON; d->x = 70; d->y = y + 10 + 20 * i; d->w = 116; d->h = 16;
			d->el = UI_EL_BUTTON; d->str = DYN(i); d->flags = 0; d->act = A_CUSTOM; d->arg = i; d->key = 0;
		}
	}
	if (id == SCR_CONFIRM_DEST && ctx.tkind == TGT_WAYPOINT && ST->waypoint[ctx.tidx].id == WP_PENDING)
		s->flags |= SF_ALTPAL;                   // setInterface(…, новая путевая точка)
	return 1;
}

// GeoscapeCraftState: строка состояния
static void craft_status(uint8_t c, char *buf)
{
	craft_t *cr = &ST->craft[c];
	uint16_t s;
	char name[48];
	name[0] = 0;
	if (ctx.flag & GCW_LOST) {                   // «перехват НЛО-n» — номер пропавшего НЛО
		fmt_num(name, ST->ufo[ctx.ufo].id, 0);
		s = STR_INTERCEPTING_UFO;
	} else if (cr->flags & CRF_LOWFUEL) s = STR_LOW_FUEL_RETURNING_TO_BASE;
	else if (cr->flags & CRF_MISSION) s = STR_MISSION_COMPLETE_RETURNING_TO_BASE;
	else if (cr->dest_kind == DK_NONE) s = STR_PATROLLING;
	else if (cr->dest_kind == DK_BASE) s = STR_RETURNING_TO_BASE;
	else if (cr->dest_kind == DK_UFO && (cr->flags & CRF_BATTLE)) s = STR_TAILING_UFO;
	else if (cr->dest_kind == DK_UFO && ST->ufo[cr->dest].status == US_FLYING) {
		fmt_num(name, ST->ufo[cr->dest].id, 0);
		s = STR_INTERCEPTING_UFO;
	} else {
		target_name(cr->dest_kind, cr->dest, name);
		s = STR_DESTINATION_UC_;
	}
	str_fmt(t1, str_get(s), name, "");
	str_fmt(buf, str_get(STR_STATUS_), t1, "");
}

static void craft_text(uint8_t slot, char *buf)
{
	rtab_t t;
	uint8_t c = ctx.craft;
	craft_t *cr = &ST->craft[c];
	if (cr->type == NONE8) return;
	rtab_open(RES_RULE_CRAFTS, &t);
	switch (slot) {
	case 0: craft_name(c, buf); break;
	case 1: craft_status(c, buf); break;
	case 2: str_fmt(buf, str_get(STR_BASE_UC), ST->base[cr->base].name, ""); break;
	case 3: fmt1n(buf, STR_SPEED_, (cr->flags & CRF_BATTLE) && cr->dest_kind == DK_UFO ? ST->ufo[cr->dest].speed : cr->speed); break;
	case 4: fmt1n(buf, STR_MAXIMUM_SPEED_UC, rtab_word(&t, cr->type, offsetof(r_crafts_t, speed_max))); break;
	case 5: {                                     // Craft::getAltitude; подводный не в воде — AIRBORNE
		uint8_t a = 1;
		if (cr->dest_kind == DK_UFO && ST->ufo[cr->dest].altitude != ALT_GROUND) a = ST->ufo[cr->dest].altitude;
		uint16_t s = alt_str[a < 5 ? a : 1];
		geo_t p = cr->pos;
		if ((int8_t)(rtab_word(&t, cr->type, offsetof(r_crafts_t, max_altitude)) & 0xFF) > -1 && !inside_land(&p)) s = STR_AIRBORNE;
		fmt1s(buf, STR_ALTITUDE_, s);
		break;
	}
	case 6: {
		uint16_t fm = rtab_word(&t, cr->type, offsetof(r_crafts_t, fuel_max));
		fmt_num(t1, fm ? (int32_t)cr->fuel * 100 / fm : 0, 0); strcat(t1, "%");
		str_fmt(buf, str_get(STR_FUEL), t1, "");
		break;
	}
	case 7: {
		uint16_t dm = rtab_word(&t, cr->type, offsetof(r_crafts_t, damage_max));
		fmt_num(t1, dm ? (int32_t)cr->damage * 100 / dm : 0, 0); strcat(t1, "%");
		str_fmt(buf, str_get(STR_DAMAGE_UC_), t1, "");
		break;
	}
	case 8: case 10: {
		uint8_t k = (slot - 8) / 2;
		uint16_t n = cr->weap[k].type == NONE8 ? STR_NONE_UC : rec_name(RES_RULE_CRAFTWEAPONS, cr->weap[k].type);
		fmt1s(buf, k ? STR_WEAPON_TWO : STR_WEAPON_ONE, n);
		break;
	}
	case 9: case 11: {
		uint8_t k = (slot - 9) / 2;
		if (cr->weap[k].type != NONE8) fmt1n(buf, STR_ROUNDS_, cr->weap[k].ammo);
		break;
	}
	case 12: case 13:
		str_copy(slot == 12 ? STR_SOLDIERS_UC : STR_HWPS, buf, 40);
		strcat(buf, ">\x01");
		fmt_num(buf + strlen(buf), slot == 12 ? soldiers_count(cr->base, c, 0) : craft_vehicles(c), 0);
		break;
	}
}

// «n» или «{ALT}n{ALT}» (InterceptState: ненулевые — вторым цветом)
static void alt_num(char *buf, uint8_t n)
{
	char *p = buf + strlen(buf);
	if (n) *p++ = 1;
	fmt_num(p, n, 0);
	if (n) strcat(buf, "\x01");
}

void fly_text(uint8_t id, uint8_t slot, uint8_t row, char *buf) __banked
{
	switch (id) {
	case SCR_INTERCEPT:
		if (row == LIST_COLS) {
			static const uint8_t cw[] = { 86, 70, 80, 46 };
			list_cols(buf, 4, cw, 0);
		} else {
			uint8_t c = craft_nth(row);
			if (c == NONE8) break;
			craft_t *cr = &ST->craft[c];
			uint8_t st = cr->status, b = cr->base, nw = (cr->weap[0].type != NONE8) + (cr->weap[1].type != NONE8);
			craft_name(c, buf);
			strcat(buf, st == CS_READY ? "\t\x01" : "\t");   // READY — вторым цветом (setCellColor)
			str_copy(status_str[st], buf + strlen(buf), 40);
			strcat(buf, st == CS_READY ? "\x01\t" : "\t");
			strcat(buf, ST->base[b].name);
			strcat(buf, "\t");
			alt_num(buf, nw);
			strcat(buf, "/");
			alt_num(buf, soldiers_count(b, c, 0));
			strcat(buf, "/");
			alt_num(buf, craft_vehicles(c));
		}
		break;
	case SCR_GEO_CRAFT:
		craft_text(slot, buf);
		break;
	case SCR_TARGET_INFO:                         // имя цели и кто к ней летит
		if (slot == 0) target_name(ctx.tkind, ctx.tidx, buf);
		else
			for (uint8_t c = 0; c < MAX_CRAFTS; c++) {
				craft_t *cr = &ST->craft[c];
				if (cr->type == NONE8 || cr->status != CS_OUT || cr->dest_kind != ctx.tkind || cr->dest != ctx.tidx) continue;
				if (buf[0]) strcat(buf, "\n");
				craft_name(c, buf + strlen(buf));
			}
		break;
	case SCR_CONFIRM_DEST:
		target_name(ctx.tkind, ctx.tidx, t2);
		str_fmt(buf, str_get(STR_TARGET), t2, "");
		break;
	case SCR_CRAFT_PATROL: {
		char wp[40];
		craft_name(ctx.craft, t1);
		target_name(TGT_WAYPOINT, ctx.tidx, wp);
		str_fmt(buf, str_get(STR_CRAFT_HAS_REACHED_DESTINATION), t1, wp);
		break;
	}
	case SCR_LOW_FUEL:
		craft_name(ctx.craft, buf);
		break;
	case SCR_CRAFT_ERROR:
		strcpy(buf, ui_msg);
		break;
	case SCR_MULTI_TARGETS:
		if (slot < mt_n) target_name(mt_list[slot].kind, mt_list[slot].idx, buf);
		break;
	}
}

uint8_t fly_rows(uint8_t id, uint8_t slot) __banked
{
	(void)slot;
	return id == SCR_INTERCEPT ? craft_nth(NONE8) : 0;
}

uint8_t fly_event(uint8_t id, uint8_t ev, uint8_t arg) __banked
{
	switch (id) {
	case SCR_INTERCEPT:
		if (ev == EVT_BUTTON && arg == 1) { UI_GO(A_POP_PUSH, SCR_BASESCAPE); break; }   // btnGotoBaseClick
		if (ev == EVT_CLOSE) ctx.flag &= ~ICW_BASE;
		if (ev == EVT_LIST) {
			uint8_t c = craft_nth(arg);
			if (c == NONE8) break;
			craft_t *cr = &ST->craft[c];
			if (ui_arrow_max) {                      // правая кнопка: центр на корабль в полёте
				if (cr->status == CS_OUT) { geo_t p = cr->pos; center_on(&p); UI_GO(A_POP, 0); }
				break;
			}
			if (cr->status == CS_READY || (cr->status == CS_OUT && !(cr->flags & (CRF_LOWFUEL | CRF_MISSION)))) {
				ctx.craft = c;
				UI_GO(A_POP_PUSH, ctx.tkind ? SCR_CONFIRM_DEST : SCR_SELECT_DEST);
			}
		}
		break;
	case SCR_GEO_CRAFT:
		if (ev != EVT_BUTTON) break;
		{
			uint8_t lost = (ctx.flag & GCW_LOST) != 0, w = ctx.tidx;
			switch (arg) {
			case 1: craft_return(ctx.craft); UI_GO(A_POP, 0); break;
			case 2: ctx.tkind = TGT_NONE; UI_GO(A_POP_PUSH, SCR_SELECT_DEST); break;
			case 3: craft_set_dest(ctx.craft, DK_NONE, 0); UI_GO(A_POP, 0); break;
			default:                             // к последней позиции НЛО — путевая точка
				if (lost) { waypoint_confirm(w); craft_set_dest(ctx.craft, DK_WAYPOINT, w); lost = 0; }
				UI_GO(A_POP, 0);
			}
			if (lost) waypoint_drop(w);
			ctx.flag = 0;
		}
		break;
	case SCR_TARGET_INFO:                         // перехват этой цели — окно перехвата поверх
		break;
	case SCR_CONFIRM_DEST:
		if (ev != EVT_BUTTON) break;
		if (arg == 1) {
			if (ctx.tkind == TGT_WAYPOINT) waypoint_confirm(ctx.tidx);
			craft_launch(ctx.craft, ctx.tkind, ctx.tidx);
			UI_GO(A_POP2, 0);
		} else {
			if (ctx.tkind == TGT_WAYPOINT) waypoint_drop(ctx.tidx);
			UI_GO(A_POP, 0);
		}
		break;
	case SCR_CRAFT_PATROL:                        // «перенаправить» — окно корабля
		if (ev == EVT_BUTTON) { ctx.flag = 0; UI_GO(A_POP_PUSH, SCR_GEO_CRAFT); }
		break;
	case SCR_LOW_FUEL:
	case SCR_CRAFT_ERROR:
		if (ev == EVT_BUTTON && arg == 1) { ST->speed = 0; UI_GO(A_POP, 0); }
		break;
	case SCR_MULTI_TARGETS:                       // btnTargetClick
		if (ev == EVT_BUTTON && arg < mt_n) mt_popup(arg, A_POP_PUSH);
		else if (ev == EVT_CLOSE) { mt_n = 0; mt_craft = NONE8; }
		break;
	}
	return 0;
}
