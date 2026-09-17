// Стратегия и миссии пришельцев (банк 18): AlienStrategy, GeoscapeState::
// determineAlienMissions / processCommand, AlienMission (start, think, spawnUfo,
// getWaypoint, ufoReachedWaypoint, ufoLifting, ufoShotDown, spawnMissionSite,
// spawnAlienBase, addScore). Источник — REF/OpenXcom/src/Savegame/AlienMission.cpp,
// AlienStrategy.cpp, Geoscape/GeoscapeState.cpp; таблицы — OxzConv (14_todo §3.17).
//
// Взвешенный выбор (WeightedOptions::choose): RNG(0, сумма) включительно, пары в
// порядке имён (std::map) — так их пишет конвертер. Отличие от OpenXcom: список мест
// avoidRepeats обрезается со своего начала (в OpenXcom addMissionLocation стирает всю
// первую по имени переменную — ошибка, при фиксированных массивах не повторить).
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "res.h"
#include "rules.h"
#include "state.h"
#include "game.h"
#include "str_ids.h"
#include "dbg.h"

// Журнал событий пришельцев в консоль эмулятора (проверка сценариев, 14_todo §3.19)
static void log2(const char *s, uint16_t a, uint16_t b)
{
	dbg_puts(s);
	dbg_dec(a);
	dbg_puts(" ");
	dbg_dec(b);
	dbg_puts("\n");
}

#define OBJ_SCORE        0
#define OBJ_INFILTRATION 1
#define OBJ_BASE         2
#define OBJ_SITE         3
#define OBJ_RETALIATION  4
#define OBJ_SUPPLY       5
#define WAVE_SITE        0x4000              // волна: ufo = #4000 + развёртывание (место миссии сразу)

static int16_t lb[64], lb2[64];              // хвостовые списки (Win1: стек в Win0 мал)
uint8_t alien_off;                           // сценарии тестов: 1 — пришельцы не начинают миссий

static uint8_t tlist(const rtab_t *t, const rlist_t *l, int16_t *buf)
{
	uint8_t n = l->n < 64 ? l->n : 64;
	if (n) rtab_tail(t, l->off, buf, n * 2);
	return n;
}

// WeightedOptions::choose по парам (номер, вес); -1 — пусто
static int16_t choose(const int16_t *p, uint8_t npairs)
{
	uint16_t total = 0;
	for (uint8_t i = 0; i < npairs; i++) total += (uint16_t)p[i * 2 + 1];
	if (!total) return -1;
	uint16_t var = rng_range(0, total);
	for (uint8_t i = 0; i < npairs; i++) {
		uint16_t w = (uint16_t)p[i * 2 + 1];
		if (var <= w) return p[i * 2];
		var -= w;
	}
	return p[(npairs - 1) * 2];
}

// Список по месяцам [месяц, n, пары] ...: последняя запись с месяцем <= month.
// strict — нет такой: 0 (generateRace); иначе первая (getMissionTypes).
static uint8_t month_pick(const int16_t *l, uint8_t n, int16_t month, uint8_t strict, const int16_t **pairs)
{
	uint8_t i = 0, found = 0xFF, first = 0xFF;
	while (i + 1 < n) {
		if (first == 0xFF) first = i;
		if (l[i] <= month) found = i;
		i += 2 + 2 * (uint8_t)l[i + 1];
	}
	if (found == 0xFF) {
		if (strict || first == 0xFF) return 0;
		found = first;
	}
	*pairs = l + found + 2;
	return (uint8_t)l[found + 1];
}

static int16_t month_choose(const int16_t *l, uint8_t n, int16_t month, uint8_t strict)
{
	const int16_t *p;
	uint8_t np = month_pick(l, n, month, strict, &p);
	return np ? choose(p, np) : -1;
}

// ---------------------------------------------------------------- AlienStrategy

static uint8_t nregions(void)
{
	rtab_t t;
	rtab_open(RES_RULE_REGIONS, &t);
	return t.n > MAX_REGIONS ? MAX_REGIONS : (uint8_t)t.n;
}

void strategy_init(void) __banked
{
	rtab_t t;
	r_regions_t r;
	strategy_t *s = &ST->strategy;
	rtab_open(RES_RULE_REGIONS, &t);
	uint8_t nr = t.n > MAX_REGIONS ? MAX_REGIONS : (uint8_t)t.n;
	for (uint8_t i = 0; i < nr; i++) {
		rtab_get(&t, i, &r);
		s->region_w[i] = (uint8_t)r.region_weight;
		uint8_t n = tlist(&t, &r.mission_weights, lb) / 2;
		for (uint8_t k = 0; k < MAX_AMT; k++) s->mission_w[i][k] = k < n ? (uint8_t)lb[k * 2 + 1] : 0;
	}
}

// chooseRandomRegion: регионы в порядке имён; пусто — заново init
static uint8_t choose_region(void)
{
	rtab_t t;
	uint8_t ord[MAX_REGIONS], nr = nregions();
	rtab_open(RES_RULE_REGIONS, &t);
	for (uint8_t i = 0; i < nr; i++) ord[rtab_word(&t, i, offsetof(r_regions_t, sort_rank)) & 0xFF] = i;
	for (uint8_t pass = 0; pass < 2; pass++) {
		uint16_t total = 0;
		for (uint8_t i = 0; i < nr; i++) total += ST->strategy.region_w[i];
		if (total) {
			uint16_t var = rng_range(0, total);
			for (uint8_t k = 0; k < nr; k++) {
				uint8_t w = ST->strategy.region_w[ord[k]];
				if (!w) continue;
				if (var <= w) return ord[k];
				var -= w;
			}
			return ord[nr - 1];
		}
		strategy_init();
	}
	return NONE8;
}

// Миссии региона из таблицы: пары (миссия, исходный вес) -> lb; число пар
static uint8_t region_missions(uint8_t r)
{
	rtab_t t;
	r_regions_t rr;
	rtab_open(RES_RULE_REGIONS, &t);
	rtab_get(&t, r, &rr);
	return tlist(&t, &rr.mission_weights, lb) / 2;
}

static int16_t choose_mission(uint8_t r)
{
	uint8_t n = region_missions(r);
	for (uint8_t k = 0; k < n; k++) lb[k * 2 + 1] = k < MAX_AMT ? ST->strategy.mission_w[r][k] : 0;
	return choose(lb, n);
}

static void remove_mission(uint8_t r, uint8_t type)
{
	uint8_t n = region_missions(r), any = 0;
	for (uint8_t k = 0; k < n && k < MAX_AMT; k++) {
		if (lb[k * 2] == type) ST->strategy.mission_w[r][k] = 0;
		any |= ST->strategy.mission_w[r][k];
	}
	if (n && !any) ST->strategy.region_w[r] = 0;
}

// validMissionRegion: регион ещё в таблице миссий (пустой исходный список не удаляется)
static uint8_t valid_region(uint8_t r)
{
	uint8_t n = region_missions(r);
	if (!n) return 1;
	for (uint8_t k = 0; k < n && k < MAX_AMT; k++) if (ST->strategy.mission_w[r][k]) return 1;
	return 0;
}

static uint8_t valid_location(int8_t var, uint8_t r, uint8_t zone)
{
	if (var < 0 || var >= NVARS) return 1;
	strategy_t *s = &ST->strategy;
	for (uint8_t i = 0; i < s->loc_n[var]; i++)
		if (s->loc[var][i].region == r && s->loc[var][i].zone == zone) return 0;
	return 1;
}

static void add_location(int8_t var, uint8_t r, uint8_t zone, uint8_t max)
{
	if (!max || var < 0 || var >= NVARS) return;
	strategy_t *s = &ST->strategy;
	if (max > VARLOC) max = VARLOC;
	if (s->loc_n[var] >= max) {                  // убрать самое старое
		memmove(&s->loc[var][0], &s->loc[var][1], (VARLOC - 1) * 2);
		s->loc_n[var]--;
	}
	s->loc[var][s->loc_n[var]].region = r;
	s->loc[var][s->loc_n[var]].zone = zone;
	s->loc_n[var]++;
}

// ---------------------------------------------------------------- правила

static void mission_rule(uint8_t type, r_alienMissions_t *m, rtab_t *t)
{
	rtab_open(RES_RULE_ALIENMISSIONS, t);
	rtab_get(t, type, m);
}

// Волна: 5 элементов [ufo, count, trajectory, timer, objective]
static uint8_t get_wave(uint8_t type, uint8_t w, int16_t *wave)
{
	rtab_t t;
	r_alienMissions_t m;
	mission_rule(type, &m, &t);
	uint8_t n = tlist(&t, &m.waves, lb2) / 5;
	if (w >= n) return n;
	memcpy(wave, lb2 + w * 5, 10);
	return n;
}

// Точка траектории: зона, высота, скорость %
static uint8_t traj_point(uint8_t traj, uint8_t pt, uint8_t *zone, uint8_t *alt, uint8_t *spd)
{
	rtab_t t;
	r_ufoTrajectories_t r;
	rtab_open(RES_RULE_UFOTRAJECTORIES, &t);
	rtab_get(&t, traj, &r);
	uint8_t n = tlist(&t, &r.waypoints, lb2) / 3;
	if (pt < n) { *zone = (uint8_t)lb2[pt * 3]; *alt = (uint8_t)lb2[pt * 3 + 1]; *spd = (uint8_t)lb2[pt * 3 + 2]; }
	return n;
}

static uint8_t assault_traj(void)
{
	rtab_t t;
	rtab_open(RES_RULE_UFOTRAJECTORIES, &t);
	for (uint8_t i = 0; i < t.n; i++)
		if (rtab_word(&t, i, offsetof(r_ufoTrajectories_t, assault)) & 0xFF) return i;
	return NONE8;
}

static uint16_t ufo_word(uint8_t type, uint8_t off)
{
	rtab_t t;
	rtab_open(RES_RULE_UFOS, &t);
	return rtab_word(&t, type, off);
}

// ---------------------------------------------------------------- места миссий, базы

// Случайное развёртывание текстуры (Texture::getRandomDeployment)
static int16_t texture_deployment(int8_t tex)
{
	rtab_t t;
	r_globeTextures_t g;
	rtab_open(RES_RULE_GLOBETEXTURES, &t);
	for (uint8_t i = 0; i < t.n; i++) {
		rtab_get(&t, i, &g);
		if (g.id != tex) continue;
		uint8_t n = tlist(&t, &g.deployments, lb2) / 2;
		return choose(lb2, n);
	}
	return -1;
}

// AlienMission::spawnMissionSite
static uint8_t spawn_site(uint8_t mi, int16_t dep, uint16_t area)
{
	rtab_t t;
	r_alienDeployments_t d;
	r_zoneAreas_t a;
	if (dep < 0) return NONE8;
	uint8_t s;
	for (s = 0; s < MAX_SITES && ST->site[s].id; s++);
	if (s >= MAX_SITES) return NONE8;
	rtab_open(RES_RULE_ALIENDEPLOYMENTS, &t);
	rtab_get(&t, (uint16_t)dep, &d);
	rtab_open(RES_RULE_ZONEAREAS, &t);
	rtab_get(&t, area, &a);
	site_t *st = &ST->site[s];
	memset(st, 0, sizeof *st);
	ST->nsites++;
	area_point(area, &st->pos);
	st->mission = ST->mission[mi].type;
	st->deployment = (uint8_t)dep;
	st->id = ++ST->ids[d.marker_name == STR_ARTIFACT_SITE ? ID_ARTIFACT : ID_TERROR];   // getId(markerName)
	st->secs = (uint32_t)rng_range(d.duration_min, d.duration_max) * 3600;
	st->race = ST->mission[mi].race;
	st->texture = a.texture;
	st->city = a.name;
	log2("alien: site deployment/area ", (uint16_t)dep, area);
	return s;
}

// AlienMission::addScore: очки миссии региону и стране точки (кроме INFILTRATION)
static void add_score(uint8_t mi, const geo_t *p)
{
	rtab_t t;
	r_alienMissions_t m;
	mission_rule(ST->mission[mi].type, &m, &t);
	if (m.objective == OBJ_INFILTRATION) return;
	geo_t q = *p;
	add_activity(&q, m.points, 0);
}

// AlienMission::spawnAlienBase
static void spawn_abase(uint8_t mi, uint16_t area, const geo_t *pos)
{
	rtab_t t;
	r_alienMissions_t m;
	r_zoneAreas_t a;
	uint8_t b;
	for (b = 0; b < MAX_ALIEN_BASES && ST->abase[b].id; b++);
	if (b >= MAX_ALIEN_BASES) return;
	mission_rule(ST->mission[mi].type, &m, &t);
	rtab_open(RES_RULE_ZONEAREAS, &t);
	rtab_get(&t, area, &a);
	int16_t dep = m.site_type != RNONE ? (int16_t)m.site_type : texture_deployment(a.texture);
	if (dep < 0) {                               // STR_ALIEN_BASE_ASSAULT по имени — первая с маркером базы
		rtab_open(RES_RULE_ALIENDEPLOYMENTS, &t);
		dep = 0;
	}
	alienbase_t *ab = &ST->abase[b];
	memset(ab, 0, sizeof *ab);
	ab->pos = *pos;
	ab->race = ST->mission[mi].race;
	ab->deployment = (uint8_t)dep;
	ab->id = ++ST->ids[ID_ALIENBASE];
	log2("alien: base slot/deployment ", b, (uint16_t)dep);
	add_score(mi, pos);
}

// Точка на суше в регионе среди областей зоны (до 100 попыток), область -> *area
static void base_point(uint8_t region, uint8_t zone, geo_t *out, uint16_t *area)
{
	uint16_t first;
	uint8_t n = zone_areas(region, zone, &first);
	*area = first;
	if (!n) { out->lon = out->lat = 0; return; }
	for (uint8_t tries = 0; tries < 100; tries++) {
		*area = first + rng_range(0, n - 1);
		area_point(*area, out);
		if (inside_land(out) && region_has(region, (uint16_t)((uint32_t)out->lon >> 16), (int16_t)(out->lat >> 16))) break;
	}
}

// ---------------------------------------------------------------- AlienMission

static uint8_t wave_count(uint8_t type)
{
	int16_t w[5];
	return get_wave(type, 0xFF, w);
}

static void set_countdown(mission_t *m, uint16_t timer)
{
	uint16_t st = timer / 30;
	m->spawn_cd = (st / 2 + rng_range(0, st)) * 30;
}

// AlienMission::start(initialCount)
static void mission_start(uint8_t mi, uint16_t initial)
{
	mission_t *m = &ST->mission[mi];
	m->next_wave = m->next_ufo = m->live_ufos = 0;
	if (!initial) {
		int16_t w[5];
		get_wave(m->type, 0, w);
		set_countdown(m, (uint16_t)w[3]);
	} else
		m->spawn_cd = initial;
}

static uint8_t mission_new(uint8_t type, uint8_t region, uint8_t race)
{
	rtab_t t;
	uint8_t i;
	for (i = 0; i < MAX_MISSIONS && ST->mission[i].type != NONE8; i++);
	if (i >= MAX_MISSIONS) return NONE8;
	mission_t *m = &ST->mission[i];
	memset(m, 0, sizeof *m);
	ST->nmissions++;
	m->type = type;
	m->race = race;
	m->uid = ++ST->ids[ID_MISSION];
	m->abase = NONE8;
	m->site_zone = -1;
	// setRegion: missionRegion региона, если задан
	rtab_open(RES_RULE_REGIONS, &t);
	uint16_t mr = rtab_word(&t, region, offsetof(r_regions_t, mission_region));
	m->region = mr != RNONE ? (uint8_t)mr : region;
	log2("alien: mission type/region ", type, m->region);
	return i;
}

// Номер волны для getWaypoint/ufoReachedWaypoint: _nextWave - 1 (или последняя)
static uint8_t cur_wave(const mission_t *m, uint8_t nw)
{
	return m->next_wave ? m->next_wave - 1 : nw - 1;
}

// AlienMission::getWaypoint
static void waypoint(uint8_t mi, uint8_t traj, uint8_t next, geo_t *out)
{
	mission_t *m = &ST->mission[mi];
	rtab_t t;
	r_alienMissions_t mr;
	int16_t w[5];
	uint8_t zone, alt, spd, z2, a2, s2;
	mission_rule(m->type, &mr, &t);
	uint8_t nw = wave_count(m->type);
	get_wave(m->type, cur_wave(m, nw), w);
	uint8_t np = traj_point(traj, next, &zone, &alt, &spd);
	uint8_t region = m->region;
	if (m->site_zone >= 0 && w[4] && zone == (uint8_t)mr.spawn_zone) {
		uint16_t first;
		zone_areas(region, (uint8_t)mr.spawn_zone, &first);
		rtab_t ta;
		r_zoneAreas_t a;
		rtab_open(RES_RULE_ZONEAREAS, &ta);
		rtab_get(&ta, first + (uint8_t)m->site_zone, &a);
		out->lon = (int32_t)((uint32_t)a.lon_min << 16);
		out->lat = (int32_t)((uint32_t)(uint16_t)a.lat_min << 16);
		return;
	}
	if (np > next + 1 && traj_point(traj, next + 1, &z2, &a2, &s2) && a2 == ALT_GROUND) land_point(region, zone, out);
	else zone_point(region, zone, out);
}

static uint8_t ufo_alloc(void)
{
	for (uint8_t u = 0; u < MAX_UFOS; u++)
		if (ST->ufo[u].type == NONE8) {
			memset(&ST->ufo[u], 0, sizeof(ufo_t));
			ST->ufo[u].dest_base = ST->ufo[u].shot_by = NONE8;
			ST->nufos++;
			return u;
		}
	return NONE8;
}

// Новое НЛО миссии mi (setMissionInfo): траектория traj, точка 0
static uint8_t ufo_new(uint8_t mi, uint8_t type, uint8_t traj)
{
	uint8_t u = ufo_alloc();
	if (u == NONE8) return NONE8;
	ufo_t *p = &ST->ufo[u];
	p->type = type;
	p->mission = mi;
	p->traj = traj;
	p->altitude = 3;                             // STR_HIGH_UC
	ST->mission[mi].live_ufos++;
	log2("alien: ufo slot/type ", u, type);
	return u;
}

// AlienMission::spawnUfo; NONE8 — миссия НЛО не выпускает
static uint8_t spawn_ufo(uint8_t mi, const int16_t *wave)
{
	mission_t *m = &ST->mission[mi];
	rtab_t t;
	r_alienMissions_t mr;
	uint8_t zone, alt, spd, traj = (uint8_t)wave[2];
	mission_rule(m->type, &mr, &t);
	int16_t rule = (wave[0] >= 0 && wave[0] < WAVE_SITE) ? wave[0] : -1;
	geo_t pos, dst;
	if (mr.objective == OBJ_RETALIATION) {
		for (uint8_t b = 0; b < MAX_BASES; b++) {
			base_t *bs = &ST->base[b];
			if (!bs->name[0] || !(bs->flags & BF_RETALIATION)) continue;
			if (!region_has(m->region, (uint16_t)((uint32_t)bs->pos.lon >> 16), (int16_t)(bs->pos.lat >> 16))) continue;
			// линкор прямо на базу (__RETALIATION_ASSAULT_RUN)
			uint8_t at = assault_traj();
			if (at == NONE8 || mr.spawn_ufo == RNONE) break;
			traj_point(traj, 0, &zone, &alt, &spd);
			if (alt == ALT_GROUND) land_point(m->region, zone, &pos); else zone_point(m->region, zone, &pos);
			uint8_t u = ufo_new(mi, (uint8_t)mr.spawn_ufo, at);
			if (u == NONE8) return NONE8;
			traj_point(at, 0, &zone, &alt, &spd);
			ufo_t *p = &ST->ufo[u];
			p->altitude = alt;
			p->pos = pos;
			p->dest_base = b;
			dst = ST->base[b].pos;
			ufo_set_course(u, &dst, (uint16_t)((uint32_t)ufo_word(p->type, offsetof(r_ufos_t, speed_max)) * spd / 100));
			return u;
		}
	} else if (mr.objective == OBJ_SUPPLY) {
		if (rule < 0 || (wave[4] && m->abase == NONE8)) return NONE8;
		uint8_t u = ufo_new(mi, (uint8_t)rule, traj);
		if (u == NONE8) return NONE8;
		traj_point(traj, 0, &zone, &alt, &spd);
		if (alt == ALT_GROUND) land_point(m->region, zone, &pos); else zone_point(m->region, zone, &pos);
		ST->ufo[u].altitude = alt;
		ST->ufo[u].pos = pos;
		uint16_t sp = (uint16_t)((uint32_t)ufo_word((uint8_t)rule, offsetof(r_ufos_t, speed_max)) * spd / 100);
		uint8_t z1, a1, s1;
		traj_point(traj, 1, &z1, &a1, &s1);
		if (a1 == ALT_GROUND) {
			if (wave[4]) dst = ST->abase[m->abase].pos;       // снабжение садится на базу
			else land_point(m->region, z1, &dst);
		} else
			zone_point(m->region, z1, &dst);
		ufo_set_course(u, &dst, sp);
		return u;
	}
	if (rule < 0) return NONE8;
	uint8_t u = ufo_new(mi, (uint8_t)rule, traj);
	if (u == NONE8) return NONE8;
	waypoint(mi, traj, 0, &pos);
	traj_point(traj, 0, &zone, &alt, &spd);
	ufo_t *p = &ST->ufo[u];
	p->altitude = alt;
	p->pos = pos;
	if (alt == ALT_GROUND) {
		rtab_t tt;
		rtab_open(RES_RULE_UFOTRAJECTORIES, &tt);
		p->secs = (uint32_t)rtab_word(&tt, traj, offsetof(r_ufoTrajectories_t, ground_timer)) * 5;
		p->status = US_LANDED;
	}
	uint16_t sp = (uint16_t)((uint32_t)ufo_word((uint8_t)rule, offsetof(r_ufos_t, speed_max)) * spd / 100);
	waypoint(mi, traj, 1, &dst);
	ufo_set_course(u, &dst, sp);
	return u;
}

// Область зоны места миссии: site_zone или случайная
static uint16_t site_area(uint8_t mi, uint8_t zone)
{
	uint16_t first;
	uint8_t n = zone_areas(ST->mission[mi].region, zone, &first);
	if (!n) return 0xFFFF;
	int8_t sz = ST->mission[mi].site_zone;
	return first + (sz < 0 ? rng_range(0, n - 1) : (uint8_t)sz);
}

// AlienMission::think (каждые 30 минут)
static void mission_think(uint8_t mi)
{
	mission_t *m = &ST->mission[mi];
	rtab_t t;
	r_alienMissions_t mr;
	int16_t w[5];
	mission_rule(m->type, &mr, &t);
	uint8_t nw = wave_count(m->type);
	if (m->next_wave >= nw) return;
	if (m->spawn_cd > 30) { m->spawn_cd -= 30; return; }
	get_wave(m->type, m->next_wave, w);
	uint8_t u = spawn_ufo(mi, w);
	if (u == NONE8 && ((w[0] >= WAVE_SITE) || (mr.objective == OBJ_SITE && w[4]))) {
		uint8_t zone, alt, spd;
		if (mr.spawn_zone < 0) traj_point((uint8_t)w[2], 0, &zone, &alt, &spd); else zone = (uint8_t)mr.spawn_zone;
		uint16_t area = site_area(mi, zone);
		if (area != 0xFFFF) {
			rtab_t ta;
			rtab_open(RES_RULE_ZONEAREAS, &ta);
			int8_t tex = (int8_t)rtab_word(&ta, area, offsetof(r_zoneAreas_t, texture));
			int16_t dep = w[0] >= WAVE_SITE ? w[0] - WAVE_SITE : texture_deployment(tex);
			spawn_site(mi, dep, area);           // не обнаружено: окно — в time1Hour (sites_hour)
		}
	}
	m = &ST->mission[mi];
	if (++m->next_ufo >= (uint8_t)w[1]) { m->next_ufo = 0; m->next_wave++; }
	if (mr.objective == OBJ_INFILTRATION && m->next_wave == nw) {
		// первая страна региона без пакта: новый пакт и база пришельцев
		rtab_t tc;
		r_countries_t c;
		rtab_open(RES_RULE_COUNTRIES, &tc);
		for (uint8_t i = 0; i < tc.n && i < MAX_COUNTRIES; i++) {
			if (ST->country[i].flags & (CF_PACT | CF_NEWPACT)) continue;
			rtab_get(&tc, i, &c);
			if (!region_has(m->region, c.label_lon, c.label_lat)) continue;
			ST->country[i].flags |= CF_NEWPACT;
			geo_t pos;
			uint16_t area;
			base_point(m->region, (uint8_t)mr.spawn_zone, &pos, &area);
			spawn_abase(mi, area, &pos);
			break;
		}
		m->next_wave = 0;                        // бесконечно
	}
	if (mr.objective == OBJ_BASE && m->next_wave == nw) {
		geo_t pos;
		uint16_t area;
		base_point(m->region, (uint8_t)mr.spawn_zone, &pos, &area);
		spawn_abase(mi, area, &pos);
	}
	uint8_t next = ST->mission[mi].next_wave;    // сравнение с локальной копией (SDCC 4.5)
	if (next != nw) {
		m = &ST->mission[mi];
		get_wave(m->type, next, w);
		set_countdown(m, (uint16_t)w[3]);
	}
}

static uint8_t mission_over(uint8_t mi)
{
	rtab_t t;
	r_alienMissions_t mr;
	mission_t *m = &ST->mission[mi];
	mission_rule(m->type, &mr, &t);
	if (mr.objective == OBJ_INFILTRATION) return 0;
	return m->next_wave >= wave_count(m->type) && !m->live_ufos;
}

// GeoscapeState::time30Minutes: миссии думают, законченные удаляются
void alien_30min(void) __banked
{
	if (!ST->nmissions) return;
	for (uint8_t i = 0; i < MAX_MISSIONS; i++)
		if (ST->mission[i].type != NONE8) mission_think(i);
	for (uint8_t i = 0; i < MAX_MISSIONS; i++)
		if (ST->mission[i].type != NONE8 && mission_over(i)) { ST->mission[i].type = NONE8; ST->nmissions--; }
}

// AlienMission::ufoReachedWaypoint. Итог: NONE8 — обычное, номер места миссии (НЛО
// стало местом миссии — окно MissionDetected), UR_ASSAULT — штурм базы dest_base.
uint8_t ufo_reached(uint8_t u) __banked
{
	ufo_t *p = &ST->ufo[u];
	uint8_t mi = p->mission;
	mission_t *m = &ST->mission[mi];
	rtab_t t;
	r_alienMissions_t mr;
	r_ufoTrajectories_t tr;
	int16_t w[5];
	mission_rule(m->type, &mr, &t);
	uint8_t nw = wave_count(m->type);
	get_wave(m->type, cur_wave(m, nw), w);
	uint8_t cur = p->traj_pt, next = cur + 1, zone, alt, spd, czone, calt, cspd;
	uint8_t np = traj_point(p->traj, cur, &czone, &calt, &cspd);
	if (next >= np) { p->flags &= ~UF_DETECTED; p->status = US_DESTROYED; return NONE8; }
	traj_point(p->traj, next, &zone, &alt, &spd);
	p->altitude = alt;
	p->traj_pt = next;
	geo_t dst;
	waypoint(mi, p->traj, next, &dst);
	rtab_open(RES_RULE_UFOTRAJECTORIES, &t);
	rtab_get(&t, p->traj, &tr);
	p = &ST->ufo[u];
	if (alt != ALT_GROUND) {
		p->status = US_FLYING;
		p->land_id = 0;
		ufo_set_course(u, &dst, (uint16_t)((uint32_t)ufo_word(p->type, offsetof(r_ufos_t, speed_max)) * spd / 100));
		return NONE8;
	}
	// сел (скорость 0 уже поставило прибытие); цель — следующая точка, на взлёт
	p->status = US_LANDED;
	if (m->site_zone >= 0 && w[4] && czone == (uint8_t)mr.spawn_zone) {
		// НЛО превращается в место миссии
		geo_t pos = p->pos;
		add_score(mi, &pos);
		ST->ufo[u].status = US_DESTROYED;
		uint16_t first;
		zone_areas(m->region, czone, &first);
		uint16_t area = first + (uint8_t)m->site_zone;
		rtab_t ta;
		rtab_open(RES_RULE_ZONEAREAS, &ta);
		int8_t tex = (int8_t)rtab_word(&ta, area, offsetof(r_zoneAreas_t, texture));
		int16_t dep = mr.site_type != RNONE ? (int16_t)mr.site_type : texture_deployment(tex);
		return spawn_site(mi, dep, area);        // преследователи с десантом — к месту (полёты: §3.3)
	}
	if (tr.assault) {                            // штурм базы X-COM: цель остаётся базой
		p->flags &= ~UF_DETECTED;
		uint8_t b = p->dest_base;
		if (b >= MAX_BASES || !ST->base[b].name[0]) { p->status = US_DESTROYED; return NONE8; }
		return UR_ASSAULT;
	}
	ufo_set_course(u, &dst, 0);
	p = &ST->ufo[u];
	geo_t pos = p->pos;
	if (inside_land(&pos)) {
		p = &ST->ufo[u];
		p->secs = (uint32_t)tr.ground_timer * 5;
		if ((p->flags & UF_DETECTED) && !p->land_id) p->land_id = ++ST->ids[ID_LANDING];
	} else
		ST->ufo[u].secs = 5;                     // садиться некуда
	return NONE8;
}

// AlienMission::ufoLifting (время на земле вышло)
void ufo_lifting(uint8_t u) __banked
{
	ufo_t *p = &ST->ufo[u];
	rtab_t t;
	r_alienMissions_t mr;
	uint8_t zone, alt, spd;
	if (p->status == US_LANDED) {
		mission_rule(ST->mission[p->mission].type, &mr, &t);
		if (mr.points > 0 && mr.objective != OBJ_BASE) { geo_t pos = p->pos; add_score(p->mission, &pos); }
		p = &ST->ufo[u];
		p->altitude = 1;                         // STR_VERY_LOW
		p->status = US_FLYING;
		traj_point(p->traj, p->traj_pt, &zone, &alt, &spd);
		geo_t dst = p->dest;
		ufo_set_course(u, &dst, (uint16_t)((uint32_t)ufo_word(p->type, offsetof(r_ufos_t, speed_max)) * spd / 100));
	} else if (p->status == US_CRASHED) {
		p->flags &= ~UF_DETECTED;
		p->status = US_DESTROYED;
	}
}

// AlienMission::ufoShotDown: следующая волна позже
void ufo_shot_down(uint8_t u) __banked
{
	mission_t *m = &ST->mission[ST->ufo[u].mission];
	if (m->next_wave != wave_count(m->type)) m->spawn_cd += 30 * (rng_range(0, 400) + 48);
}

// Mod::getRandomMission: миссии цели objective по порядку имён, вес — последняя запись
// missionWeights с месяцем <= months (нет записей — 1); RNG(1, сумма). -1 — нет.
static int16_t random_mission(uint8_t objective)
{
	rtab_t t;
	r_alienMissions_t m;
	int16_t w[16];
	uint8_t id[16], rank[16], n = 0;
	rtab_open(RES_RULE_ALIENMISSIONS, &t);
	for (uint8_t i = 0; i < t.n && n < 16; i++) {
		rtab_get(&t, i, &m);
		if (m.objective != objective) continue;
		uint8_t k = tlist(&t, &m.mission_weights, lb2) / 2;
		int16_t wt = k ? 0 : 1;
		for (uint8_t j = 0; j < k && lb2[j * 2] <= ST->months; j++) wt = lb2[j * 2 + 1];
		if (wt <= 0) continue;
		uint8_t p = n++, r = m.sort_rank;
		for (; p && rank[p - 1] > r; p--) { rank[p] = rank[p - 1]; id[p] = id[p - 1]; w[p] = w[p - 1]; }
		rank[p] = r; id[p] = i; w[p] = wt;
	}
	uint16_t total = 0;
	for (uint8_t i = 0; i < n; i++) total += (uint16_t)w[i];
	if (!total) return -1;
	uint16_t pick = rng_range(1, total);
	for (uint8_t i = 0; i < n; i++) {
		if (pick <= (uint16_t)w[i]) return id[i];
		pick -= (uint16_t)w[i];
	}
	return id[n - 1];
}

// BaseDestroyedState: миссия возмездия в регионе уничтоженной базы и её НЛО убираются
void retaliation_cancel(uint8_t region) __banked
{
	rtab_t t;
	rtab_open(RES_RULE_ALIENMISSIONS, &t);
	for (uint8_t i = 0; i < MAX_MISSIONS; i++) {
		mission_t *m = &ST->mission[i];
		if (m->type == NONE8 || m->region != region) continue;
		if ((rtab_word(&t, m->type, offsetof(r_alienMissions_t, objective)) & 0xFF) != OBJ_RETALIATION) continue;
		for (uint8_t u = 0; u < MAX_UFOS; u++)
			if (ST->ufo[u].type != NONE8 && ST->ufo[u].mission == i) ufo_free(u);
		ST->mission[i].type = NONE8;
		if (ST->nmissions) ST->nmissions--;
		log2("alien: retaliation cancelled mission/region ", i, region);
		return;
	}
}

// MonthlyReportState::calculateChanges: pactScore — очки случайной миссии INFILTRATION
// (по весам месяца; RNG расходуется, как в OpenXcom, даже если пактов нет)
int16_t pact_score(void) __banked
{
	int16_t type = random_mission(OBJ_INFILTRATION);
	if (type < 0) return 0;
	rtab_t t;
	rtab_open(RES_RULE_ALIENMISSIONS, &t);
	return (int16_t)rtab_word(&t, (uint16_t)type, offsetof(r_alienMissions_t, points));
}

// DogfightState::update: НЛО сбито — миссия возмездия в регион (если там такой нет),
// первая волна — через spawnTimer волны 0
void mission_retaliation(uint8_t region, uint8_t race) __banked
{
	rtab_t t;
	rtab_open(RES_RULE_ALIENMISSIONS, &t);
	for (uint8_t i = 0; i < MAX_MISSIONS; i++) {
		mission_t *m = &ST->mission[i];
		if (m->type == NONE8 || m->region != region) continue;
		if ((rtab_word(&t, m->type, offsetof(r_alienMissions_t, objective)) & 0xFF) == OBJ_RETALIATION) return;
	}
	int16_t type = random_mission(OBJ_RETALIATION);
	if (type < 0) return;
	uint8_t mi = mission_new((uint8_t)type, region, race);
	if (mi == NONE8) return;
	int16_t w[5];
	get_wave((uint8_t)type, 0, w);
	mission_start(mi, (uint16_t)w[3]);
}

int16_t weights_choose(const int16_t *pairs, uint8_t npairs) __banked
{
	int16_t p[32];                               // пары — из банка вызывающего: копия на стек
	if (npairs > 16) npairs = 16;
	memcpy(p, pairs, npairs * 4);
	return choose(p, npairs);
}

// GenerateSupplyMission: миссия снабжения базы пришельцев ab
void mission_supply(uint8_t type, uint8_t region, uint8_t race, uint8_t ab) __banked
{
	uint8_t mi = mission_new(type, region, race);
	if (mi == NONE8) return;
	ST->mission[mi].abase = ab;
	mission_start(mi, 0);
}

// AlienMission::setWaveCountdown (после штурма базы)
void mission_delay(uint8_t mi, uint16_t minutes) __banked
{
	if (mission_over(mi)) return;
	ST->mission[mi].spawn_cd = minutes;
}

// НЛО убрано из игры: миссия теряет живое НЛО (~Ufo)
void ufo_free(uint8_t u) __banked
{
	ufo_t *p = &ST->ufo[u];
	if (p->type == NONE8) return;
	if (p->mission < MAX_MISSIONS) {
		mission_t *m = &ST->mission[p->mission];
		if (m->type != NONE8 && m->live_ufos) m->live_ufos--;
	}
	p->type = NONE8;
	if (ST->nufos) ST->nufos--;
}

// ---------------------------------------------------------------- determineAlienMissions

static uint8_t mission_in_region(uint8_t type, uint8_t region)
{
	for (uint8_t i = 0; i < MAX_MISSIONS; i++)
		if (ST->mission[i].type == type && ST->mission[i].region == region) return 1;
	return 0;
}

static uint8_t base_regions(uint8_t *out)
{
	uint8_t n = 0;
	for (uint8_t b = 0; b < MAX_BASES; b++) {
		if (!ST->base[b].name[0]) continue;
		uint8_t r = region_at((uint16_t)((uint32_t)ST->base[b].pos.lon >> 16), (int16_t)(ST->base[b].pos.lat >> 16));
		if (r != NONE8) out[n++] = r;
	}
	return n;
}

// Имена из записи по месяцу (getMissionTypes / getRegions) -> out
static uint8_t month_names(const int16_t *l, uint8_t n, int16_t month, uint8_t *out)
{
	const int16_t *p;
	uint8_t np = month_pick(l, n, month, 0, &p);
	for (uint8_t i = 0; i < np; i++) out[i] = (uint8_t)p[i * 2];
	return np;
}

static struct { uint8_t region, area; } valid[64];

// GeoscapeState::processCommand
static uint8_t process_command(const r_missionScripts_t *s, const rtab_t *t)
{
	int16_t month = ST->months;
	int16_t mission_type = -1, race;
	uint8_t region = NONE8;
	int8_t target_zone = -1;
	uint8_t nv = 0, names[16], nnames;
	static int16_t mw[64], rw[64];
	uint8_t nmw = tlist(t, &s->mission_weights, mw), nrw = tlist(t, &s->region_weights, rw);
	uint8_t nr = nregions();
	if (s->site_type) {
		mission_type = month_choose(mw, nmw, month, 0);
		nnames = month_names(mw, nmw, month, names);
		uint8_t target_base = rng_percent(s->target_base_odds), cur = 0;
		while (cur < nnames && names[cur] != (uint8_t)mission_type) cur++;
		for (uint8_t h = 0; h < nnames; h++) {
			uint8_t regs[MAX_REGIONS + MAX_BASES], nreg = 0;
			if (nrw) nreg = month_names(rw, nrw, month, regs);
			else for (uint8_t i = 0; i < nr; i++) regs[nreg++] = i;
			rtab_t tm;
			rtab_open(RES_RULE_ALIENMISSIONS, &tm);
			int8_t spawn_zone = (int8_t)rtab_word(&tm, (uint16_t)mission_type, offsetof(r_alienMissions_t, spawn_zone));
			if (target_base) {                   // только регионы с базами X-COM
				uint8_t br[MAX_BASES], nb = base_regions(br), k = 0;
				for (uint8_t i = 0; i < nreg; i++) {
					uint8_t keep = 0;
					for (uint8_t j = 0; j < nb; j++) if (br[j] == regs[i]) keep = 1;
					if (keep) regs[k++] = regs[i];
				}
				nreg = k;
			}
			for (uint8_t i = 0; i < nreg; i++) {
				uint8_t r = regs[i];
				if (mission_in_region((uint8_t)mission_type, r)) continue;
				if (spawn_zone < 0) continue;
				uint16_t first;
				uint8_t na = zone_areas(r, (uint8_t)spawn_zone, &first);
				for (uint8_t a = 0; a < na; a++)
					if (area_is_point(first + a) && valid_location(s->var_index, r, a) && nv < 64) {
						valid[nv].region = r;
						valid[nv].area = a;
						nv++;
					}
			}
			if (nv) break;
			if (nnames > 1 && ++cur == nnames) cur = 0;
			mission_type = names[cur];
		}
		if (!nv) return 0;
		while (target_zone < 0) {
			region = nrw ? (uint8_t)month_choose(rw, nrw, month, 0) : (uint8_t)rng_range(0, nr - 1);
			uint8_t lo = 0xFF, hi = 0;
			for (uint8_t i = 0; i < nv; i++) {
				if (valid[i].region == region) { if (lo == 0xFF) lo = i; hi = i; }
				else if (lo != 0xFF) break;
			}
			if (lo != 0xFF) target_zone = (int8_t)valid[rng_range(lo, hi)].area;
		}
		add_location(s->var_index, region, (uint8_t)target_zone, s->avoid_repeats);
	} else if (rng_percent(s->target_base_odds)) {
		uint8_t master[MAX_BASES], nm = base_regions(master);
		nnames = month_names(mw, nmw, month, names);
		if (!nnames) {
			uint8_t k = 0;
			for (uint8_t i = 0; i < nm; i++) if (valid_region(master[i])) master[k++] = master[i];
			if (!k) return 0;
			region = master[rng_range(0, k - 1)];
		} else {
			uint8_t entry = (uint8_t)rng_range(0, nnames - 1);
			for (uint8_t i = 0; i < nnames; i++) {
				uint8_t regs[MAX_BASES], k = 0;
				for (uint8_t j = 0; j < nm; j++) if (!mission_in_region(names[entry], master[j])) regs[k++] = master[j];
				if (k) { mission_type = names[entry]; region = regs[rng_range(0, k - 1)]; break; }
				if (nnames > 1 && ++entry == nnames) entry = 0;
			}
		}
	} else if (!nrw)
		region = choose_region();
	else
		region = (uint8_t)month_choose(rw, nrw, month, 0);
	if (region == NONE8) return 0;
	if (mission_type < 0) mission_type = nmw ? month_choose(mw, nmw, month, 0) : choose_mission(region);
	if (mission_type < 0) return 0;
	// раса: из сценария или из миссии по месяцу
	uint8_t nrace = tlist(t, &s->race_weights, lb2);
	if (nrace) race = month_choose(lb2, nrace, month, 0);
	else {
		rtab_t tm;
		r_alienMissions_t mr;
		mission_rule((uint8_t)mission_type, &mr, &tm);
		nrace = tlist(&tm, &mr.race_weights, lb2);
		race = month_choose(lb2, nrace, month, 1);
	}
	if (race < 0) return 0;
	uint8_t mi = mission_new((uint8_t)mission_type, region, (uint8_t)race);
	if (mi == NONE8) return 0;
	ST->mission[mi].site_zone = target_zone;
	if (s->var_index >= 0 && s->var_index < NVARS) ST->strategy.runs[s->var_index]++;
	mission_start(mi, s->start_delay);
	if (s->flags & MISSIONSCRIPTS_F_USE_TABLE) remove_mission(region, (uint8_t)mission_type);
	return 1;
}

// GeoscapeState::determineAlienMissions: сценарии по порядку, метки и условия
void alien_month(void) __banked
{
	rtab_t t;
	r_missionScripts_t s;
	uint8_t known[32], ok[32], avail[32], na = 0;
	int16_t month = ST->months;
	if (alien_off) return;
	memset(known, 0, sizeof known);
	memset(ok, 0, sizeof ok);
	rtab_open(RES_RULE_MISSIONSCRIPTS, &t);
	for (uint8_t i = 0; i < t.n && na < 32; i++) {
		rtab_get(&t, i, &s);
		if (s.first_month > month || (s.last_month != -1 && s.last_month < month)) continue;
		if (s.max_runs != -1 && s.var_index >= 0 && s.var_index < NVARS && s.max_runs <= ST->strategy.runs[s.var_index]) continue;
		if (s.min_difficulty > ST->difficulty) continue;
		uint8_t n = tlist(&t, &s.research_triggers, lb), happy = 1;
		for (uint8_t k = 0; k + 1 < n && happy; k += 2) happy = res_done((uint16_t)lb[k]) == (uint8_t)lb[k + 1];
		if (happy) avail[na++] = i;
	}
	for (uint8_t j = 0; j < na; j++) {
		rtab_get(&t, avail[j], &s);
		uint8_t n = tlist(&t, &s.conditionals, lb), process = 1, success = 0;
		for (uint8_t k = 0; k < n && process; k++) {
			int16_t c = lb[k];
			uint8_t l = (uint8_t)(c < 0 ? -c : c), bit = (uint8_t)(1 << (l & 7)), idx = l >> 3;
			uint8_t found = known[idx] & bit, good = ok[idx] & bit;
			process = !found || (good && c > 0) || (!good && c < 0);
		}
		if (process && rng_percent(s.execution_odds)) success = process_command(&s, &t);
		if (s.label) {
			uint8_t bit = (uint8_t)(1 << (s.label & 7)), idx = s.label >> 3;
			known[idx] |= bit;
			if (success) ok[idx] |= bit;
		}
	}
}
