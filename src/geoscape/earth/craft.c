// Полёты кораблей X-COM (банк 20): Craft::setDestination / think / returnToBase /
// consumeFuel / getFuelLimit, прибытие к цели и её потеря (GeoscapeState::
// time5Seconds, time10Minutes), путевые точки. 14_todo §3.3.
//
// Движение — как у НЛО (geo_aim, макрошаги); в погоне за летящим НЛО шагов до
// встречи не больше dist / (vc + vu), а прибытие проверяется по настоящему расстоянию
// (MovingTarget::move: не дальше шага — встал в цель).
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "res.h"
#include "rules.h"
#include "state.h"
#include "game.h"
#include "dbg.h"

static uint16_t cword(uint8_t type, uint8_t off)
{
	rtab_t t;
	rtab_open(RES_RULE_CRAFTS, &t);
	return rtab_word(&t, type, off);
}

// Точка цели корабля; 0 — патруль (цели нет)
static uint8_t dest_pos(uint8_t c, geo_t *out)
{
	craft_t *cr = &ST->craft[c];
	uint8_t d = cr->dest;
	switch (cr->dest_kind) {
	case DK_BASE: *out = ST->base[cr->base].pos; return 1;
	case DK_UFO: *out = ST->ufo[d].pos; return 1;
	case DK_WAYPOINT: *out = ST->waypoint[d].pos; return 1;
	case DK_SITE: *out = ST->site[d].pos; return 1;
	case DK_ALIENBASE: *out = ST->abase[d].pos; return 1;
	}
	return 0;
}

static void caim(uint8_t c)
{
	craft_t *cr = &ST->craft[c];
	geo_vel_t v;
	geo_t p = cr->pos, d;
	cr->aim_n = 0;
	if (!cr->speed || !dest_pos(c, &d)) { cr->vlon = cr->vlat = 0; cr->steps = 0xFFFF; return; }
	uint32_t sc = geo_speed(cr->speed);
	geo_aim(&p, &d, sc, &v);
	cr = &ST->craft[c];
	cr->vlon = v.dlon;
	cr->vlat = v.dlat;
	cr->steps = v.steps;
	if (cr->dest_kind == DK_UFO) {               // летящее НЛО: встреча не раньше dist / (vc + vu)
		ufo_t *u = &ST->ufo[cr->dest];
		if (u->status == US_FLYING && u->speed)
			cr->steps = (uint16_t)((uint32_t)v.steps * sc / (sc + geo_speed(u->speed)));
	}
}

// Craft::setDestination: с базы — взлёт 60 шагов; скорость max, в патруле — max / 2
void craft_set_dest(uint8_t c, uint8_t kind, uint8_t idx) __banked
{
	craft_t *cr = &ST->craft[c];
	uint8_t type = cr->type;
	if (cr->status != CS_OUT) cr->takeoff = 60;
	uint16_t mx = cword(type, offsetof(r_crafts_t, speed_max));
	cr = &ST->craft[c];
	cr->dest_kind = kind;
	cr->dest = idx;
	cr->speed = kind == DK_NONE ? mx / 2 : mx;
	caim(c);
}

// ConfirmDestinationState::btnOkClick
void craft_launch(uint8_t c, uint8_t kind, uint8_t idx) __banked
{
	craft_set_dest(c, kind, idx);
	if (ST->craft[c].status != CS_OUT) ST->ncraft_out++;
	ST->craft[c].status = CS_OUT;
}

void craft_return(uint8_t c) __banked
{
	craft_set_dest(c, DK_BASE, 0);
}

// GeoscapeState::time5Seconds, Craft::isDestroyed: −score стране и региону, экипаж
// погиб, корабль с грузом потерян
void craft_destroyed(uint8_t c) __banked
{
	craft_t *cr = &ST->craft[c];
	geo_t p = cr->pos;
	int16_t score = (int16_t)cword(cr->type, offsetof(r_crafts_t, score));
	if (ST->craft[c].status == CS_OUT && ST->ncraft_out) ST->ncraft_out--;
	add_activity(&p, -score, 1);
	craft_lost(c);
}

// Путевая точка: новая запись (id — при подтверждении цели), NONE8 — нет места
uint8_t waypoint_new(const geo_t *p) __banked
{
	for (uint8_t w = 0; w < MAX_WAYPOINTS; w++)
		if (!ST->waypoint[w].id) {
			geo_t q = *p;
			ST->waypoint[w].pos = q;
			ST->waypoint[w].id = WP_PENDING;
			return w;
		}
	return NONE8;
}

void waypoint_confirm(uint8_t w) __banked
{
	if (ST->waypoint[w].id == WP_PENDING) ST->waypoint[w].id = ++ST->ids[ID_WAYPOINT];
}

void waypoint_drop(uint8_t w) __banked
{
	if (w < MAX_WAYPOINTS && ST->waypoint[w].id == WP_PENDING) ST->waypoint[w].id = 0;
}

static uint8_t crew(uint8_t c)
{
	craft_t *cr = &ST->craft[c];
	uint8_t n = soldiers_count(cr->base, c, 0);
	cr = &ST->craft[c];
	if (cr->cargo != NONE8)
		for (uint8_t k = 0; k < CARGO_VEH; k++) if (ST->cargo[cr->cargo].veh[k].type != NONE8) n++;
	return n;
}

// ---------------------------------------------------------------- макрошаги

static uint8_t flying(const craft_t *cr)
{
	return cr->type != NONE8 && cr->status == CS_OUT && !cr->transit;
}

static uint8_t nfly;                         // в воздухе (считает craft_limit в начале макрошага)

uint16_t craft_limit(uint16_t k) __banked
{
	craft_t *cr = ST->craft;
	nfly = 0;
	if (!ST->ncraft_out) return k;
	for (uint8_t c = 0; c < MAX_CRAFTS; c++, cr++) {
		if (!flying(cr)) continue;
		nfly++;
		if (cr->takeoff) { if (cr->takeoff < k) k = cr->takeoff; continue; }
		if (cr->dest_kind == DK_NONE || !cr->speed) continue;
		if (cr->steps < k) k = cr->steps + 1;
	}
	return k;
}

void craft_advance(uint16_t n) __banked
{
	if (!nfly) return;
	craft_t *cr = ST->craft;
	for (uint8_t c = 0; c < MAX_CRAFTS; c++, cr++) {
		if (!flying(cr)) continue;
		if (cr->takeoff) { cr->takeoff -= (uint8_t)n; continue; }
		if (cr->dest_kind == DK_NONE || !cr->speed || cr->steps == 0xFFFF) continue;
		geo_vel_t v;
		v.dlon = cr->vlon; v.dlat = cr->vlat;
		geo_move(&cr->pos, &v, n);
		cr->steps -= n;
	}
}

// Прибыл на базу (Craft::think): осмотр, цели нет, флаги сброшены
static void home(uint8_t c)
{
	if (ST->ncraft_out) ST->ncraft_out--;
	craft_checkup(c);
	craft_t *cr = &ST->craft[c];
	cr->dest_kind = DK_NONE;
	cr->speed = 0;
	cr->flags &= ~(CRF_LOWFUEL | CRF_MISSION | CRF_BATTLE);
	cr->takeoff = 0;
	cr->order = 0;
}

// Журнал прибытия к НЛО: печатается, только когда у корабля меняется исход (07 §4) —
// по нему видно, почему бой не начался (корабль уже в бою, не догнать, нет слота боя).
static uint8_t arr_why[MAX_CRAFTS];

static void arr_log(uint8_t c, uint8_t why)
{
	uint8_t was = arr_why[c];
	if (was == why) return;
	arr_why[c] = why;
	dbg_puts("craft: "); dbg_dec(c); dbg_puts(" ufo -> "); dbg_dec(why); dbg_puts("\n");
}

// Прибытие к цели (GeoscapeState::time5Seconds, reachedDestination)
static void arrive(uint8_t c)
{
	craft_t *cr = &ST->craft[c];
	uint8_t d = cr->dest;
	switch (cr->dest_kind) {
	case DK_BASE: home(c); return;
	case DK_UFO: {
		ufo_t *u = &ST->ufo[d];
		if (u->status == US_FLYING) {
			if (cr->flags & CRF_BATTLE) { arr_log(c, 1); return; }   // уже в бою
			uint16_t mx = cword(cr->type, offsetof(r_crafts_t, speed_max));
			if (ST->ufo[d].speed > mx) { arr_log(c, 2); return; }    // не догнать
			uint8_t e = df_start(c, d);              // не больше 4 боёв: иначе — на следующем шаге
			arr_log(c, e ? 4 : 3);                   // 3 — нет слота боя, 4 — бой начался
			if (e) gev_push(GE_DOGFIGHT, e - 1, c, d);
			return;
		}
		if (crew(c)) {
			arr_log(c, 5);                           // высадка
			if (!(ST->craft[c].flags & CRF_BATTLE)) { ST->speed = 0; gev_push(GE_LANDING, NONE8, c, DK_UFO); }
		} else if (ST->ufo[d].status != US_LANDED) {
			arr_log(c, 7);                           // домой
			craft_return(c);
		} else
			arr_log(c, 6);                           // висит над севшим НЛО
		return;
	}
	case DK_WAYPOINT:
		gev_push(GE_PATROL, NONE8, c, d);
		craft_set_dest(c, DK_NONE, 0);
		return;
	case DK_SITE:
		if (crew(c)) { ST->speed = 0; gev_push(GE_LANDING, NONE8, c, DK_SITE); }
		else craft_return(c);
		return;
	case DK_ALIENBASE:
		if (!(ST->abase[d].flags & AB_DISCOVERED)) return;
		if (crew(c)) { ST->speed = 0; gev_push(GE_LANDING, NONE8, c, DK_ALIENBASE); }
		else craft_return(c);
		return;
	}
}

// Цель — НЛО: пропало с радаров / уничтожено (GeoscapeState::time5Seconds)
static void check_target(uint8_t c)
{
	craft_t *cr = &ST->craft[c];
	if (cr->dest_kind != DK_UFO) return;
	uint8_t d = cr->dest;
	ufo_t *u = &ST->ufo[d];
	if (u->type == NONE8 || u->status == US_DESTROYED) { craft_return(c); return; }
	if (!(u->flags & UF_DETECTED)) {
		rtab_t t;
		rtab_open(RES_RULE_UFOTRAJECTORIES, &t);
		uint8_t assault = (uint8_t)rtab_word(&t, u->traj, offsetof(r_ufoTrajectories_t, assault));
		if (assault && (u->status == US_LANDED || u->status == US_DESTROYED)) { craft_return(c); return; }
		geo_t p = ST->ufo[d].pos;
		uint8_t w = waypoint_new(&p);            // «идти к последней позиции» — окно корабля
		craft_set_dest(c, DK_NONE, 0);
		gev_push(GE_TARGET_LOST, d, c, w);       // base — НЛО (номер для «перехват НЛО-n»)
		return;
	}
	if (u->status == US_LANDED) ST->craft[c].flags &= ~CRF_BATTLE;
}

// time5Seconds: корабли
void craft_step(void) __banked
{
	if (!nfly) return;
	for (uint8_t c = 0; c < MAX_CRAFTS; c++) {
		craft_t *cr = &ST->craft[c];
		if (!flying(cr)) continue;
		check_target(c);
		cr = &ST->craft[c];
		if (cr->takeoff) {                       // Craft::think: взлёт
			if (!--cr->takeoff) caim(c);
			continue;
		}
		if (cr->dest_kind == DK_NONE || !cr->speed) continue;
		geo_vel_t v;
		if (cr->steps && cr->steps != 0xFFFF) {
			v.dlon = cr->vlon; v.dlat = cr->vlat;
			geo_move(&cr->pos, &v, 1);
			if (cr->dest_kind == DK_UFO || cr->steps <= 360 || ++cr->aim_n >= 3) caim(c);
			else cr->steps--;
			continue;
		}
		// шагов не осталось: не дальше шага — в цель, иначе ещё шаг (догоняем)
		geo_t p = cr->pos, d;
		if (!dest_pos(c, &d)) continue;
		if (geo_dist(&p, &d) <= geo_speed(ST->craft[c].speed)) {
			ST->craft[c].pos = d;
			arrive(c);
			// Прибыл и остался у цели (висит над севшим НЛО, ждёт подтверждения высадки):
			// шагов до цели больше нет. Иначе craft_limit режет макрошаг до одного шага в
			// 5 секунд, и на быстром времени геоскейп встаёт: такт «30 минут» — 360 полных
			// шагов со всей тригонометрией вместо трёх.
			cr = &ST->craft[c];
			if (flying(cr) && cr->dest_kind != DK_NONE) cr->steps = 0xFFFF;
		} else {
			caim(c);
			cr = &ST->craft[c];
			v.dlon = cr->vlon; v.dlat = cr->vlat;
			geo_move(&cr->pos, &v, 1);
		}
	}
	// путевые точки без преследователей — убрать (кроме ждущих подтверждения)
	for (uint8_t w = 0; w < MAX_WAYPOINTS; w++) {
		if (!ST->waypoint[w].id || ST->waypoint[w].id == WP_PENDING) continue;
		uint8_t used = 0;
		for (uint8_t c = 0; c < MAX_CRAFTS && !used; c++) {
			craft_t *cr = &ST->craft[c];
			used = flying(cr) && cr->dest_kind == DK_WAYPOINT && cr->dest == w;
		}
		if (!used) ST->waypoint[w].id = 0;
	}
}

// ---------------------------------------------------------------- топливо

// Craft::getFuelConsumption: с предметом-топливом — 1, иначе скорость / 100
static uint16_t consumption(uint8_t type, uint16_t speed)
{
	return cword(type, offsetof(r_crafts_t, refuel_item)) != RNONE ? 1 : speed / 100;
}

// Умножение 32x16 двумя 16x16 (32x32 __mullong дороже втрое); старшие биты за 2^32 отбрасываются
static uint32_t mul32x16u(uint32_t a, uint16_t n)
{
	return (uint32_t)(uint16_t)a * n + ((uint32_t)(uint16_t)(a >> 16) * n << 16);
}

// Craft::getFuelLimit: floor(расход · расстояние / _speedMaxRadian) — топливо на путь до базы на
// максимальной скорости; _speedMaxRadian — путь за 10 минут (скорость за шаг 5 с · 120, Craft.cpp:65),
// расход — тоже за 10 минут
uint16_t craft_fuel_limit(uint8_t c) __banked
{
	craft_t *cr = &ST->craft[c];
	uint8_t type = cr->type;
	geo_t p = cr->pos, b = ST->base[cr->base].pos;
	uint16_t mx = cword(type, offsetof(r_crafts_t, speed_max));
	uint32_t sm = geo_speed(mx) * 120, d = geo_angle(&p, &b);
	if (!sm) return 0;
	uint16_t use = consumption(type, mx);
	// без переполнения и с одним делением вместо двух: остаток — d − q · sm (умножение 32x16 дешевле)
	uint32_t q = d / sm, r = d - mul32x16u(sm, (uint16_t)q);
	return (uint16_t)((uint32_t)use * q + (uint32_t)use * (r >> 8) / (sm >> 8));
}

// time10Minutes: расход топлива, возврат при нехватке; патруль ищет базы пришельцев
void craft_10min(void) __banked
{
	if (!nfly) return;
	for (uint8_t c = 0; c < MAX_CRAFTS; c++) {
		craft_t *cr = &ST->craft[c];
		if (!flying(cr)) continue;
		uint8_t type = cr->type;
		uint16_t use = consumption(type, ST->craft[c].speed);
		cr = &ST->craft[c];
		cr->fuel = cr->fuel > use ? cr->fuel - use : 0;
		if (!(cr->flags & CRF_LOWFUEL) && cr->fuel <= craft_fuel_limit(c)) {
			ST->craft[c].flags |= CRF_LOWFUEL;
			craft_return(c);
			gev_push(GE_LOW_FUEL, NONE8, c, 0);
		}
		cr = &ST->craft[c];
		if (cr->dest_kind != DK_NONE) continue;
		// патруль: база пришельцев в sightRange — шанс 50 − dist / range · 50 %
		uint32_t range = geo_speed(cword(type, offsetof(r_crafts_t, sight_range))) * 720;   // мили -> единицы
		for (uint8_t b = 0; b < MAX_ALIEN_BASES; b++) {
			alienbase_t *ab = &ST->abase[b];
			if (!ab->id || (ab->flags & AB_DISCOVERED)) continue;
			geo_t p = ST->craft[c].pos, q = ab->pos;
			uint32_t d = geo_angle(&p, &q);
			if (d > range) continue;
			uint8_t pc = (uint8_t)(50 - (uint8_t)(d / (range / 50 + 1)));
			if (rng_percent(pc)) ST->abase[b].flags |= AB_DISCOVERED;
		}
	}
}
