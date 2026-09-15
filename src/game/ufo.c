// НЛО и объекты пришельцев на глобусе (банк 19): движение (Ufo::think, MovingTarget::
// move) макрошагами, time5Seconds для НЛО, очки и обнаружение (time30Minutes),
// обломки, места миссий (processMissionSite, окно в time1Hour), обнаружение баз X-COM
// миссиями возмездия (time10Minutes), базы пришельцев (time1Day: очки, снабжение).
// Источник — REF/OpenXcom/src/Geoscape/GeoscapeState.cpp, Savegame/Ufo.cpp, Base.cpp.
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "res.h"
#include "rules.h"
#include "state.h"
#include "game.h"
#include "dbg.h"

#define OBJ_RETALIATION 4

static void log2(const char *s, uint16_t a, uint16_t b)
{
	dbg_puts(s);
	dbg_dec(a);
	dbg_puts(" ");
	dbg_dec(b);
	dbg_puts("\n");
}

static void aim(uint8_t u)
{
	ufo_t *p = &ST->ufo[u];
	geo_vel_t v;
	geo_t pos = p->pos, d = p->dest;
	geo_aim(&pos, &d, geo_speed(p->speed), &v);
	p = &ST->ufo[u];
	p->vlon = v.dlon;
	p->vlat = v.dlat;
	p->steps = v.steps;
	p->dir = geo_heading(&v);
}

// setDestination + setSpeed (calculateSpeed)
void ufo_set_course(uint8_t u, const geo_t *dst, uint16_t knots) __banked
{
	geo_t d = *dst;
	ST->ufo[u].dest = d;
	ST->ufo[u].speed = knots;
	aim(u);
}

// Корабли, летящие к НЛО (getFollowers)
static uint8_t followers(uint8_t u)
{
	for (uint8_t c = 0; c < MAX_CRAFTS; c++) {
		craft_t *cr = &ST->craft[c];
		if (cr->type != NONE8 && cr->status == CS_OUT && cr->dest_kind == DK_UFO && cr->dest == u) return 1;
	}
	return 0;
}

// ---------------------------------------------------------------- макрошаги

// Сколько шагов 5 с можно пройти (не больше k), чтобы событие (прибытие, взлёт)
// пришлось не раньше последнего шага.
uint16_t ufo_limit(uint16_t k) __banked
{
	if (!ST->nufos) return k;
	ufo_t *p = ST->ufo;
	for (uint8_t u = 0; u < MAX_UFOS; u++, p++) {
		if (p->type == NONE8 || p->mission == NONE8) continue;
		if (p->status == US_FLYING && p->speed) { if (p->steps < k) k = p->steps + 1; }
		else if (p->status == US_LANDED && p->secs < (uint32_t)(k * 5)) {   // деление — только если ближе
			uint16_t s = (uint16_t)p->secs / 5;
			k = s ? s : 1;
		}
	}
	return k;
}

// n шагов без событий (n < ufo_limit)
void ufo_advance(uint16_t n) __banked
{
	if (!ST->nufos) return;
	ufo_t *p = ST->ufo;
	for (uint8_t u = 0; u < MAX_UFOS; u++, p++) {
		if (p->type == NONE8 || p->mission == NONE8) continue;
		if (p->status == US_FLYING && p->speed) {
			geo_vel_t v;
			v.dlon = p->vlon; v.dlat = p->vlat;
			geo_move(&p->pos, &v, n);
			p->steps -= n;
		} else if (p->status == US_LANDED)
			p->secs -= (uint16_t)(n * 5);            // n < 120
	}
}

// Оборона базы (GeoscapeState::time5Seconds, НЛО у базы после штурма)
static void base_attack(uint8_t u)
{
	ufo_t *p = &ST->ufo[u];
	mission_delay(p->mission, 30 * (rng_range(0, 400) + 48));
	ST->speed = 0;                               // timerReset
	gev_push(GE_BASE_ATTACK, p->dest_base, p->dest_base, u);
}

// time5Seconds: шаг НЛО и их события
void ufo_step(void) __banked
{
	if (!ST->nufos) return;
	ufo_t *p = ST->ufo;
	uint8_t dead = 0;
	for (uint8_t u = 0; u < MAX_UFOS; u++, p++) {
		if (p->type == NONE8 || p->mission == NONE8) continue;   // NONE8 — подставное (окна)
		switch (p->status) {
		case US_FLYING:
			if (!p->speed) break;
			if (p->steps) {                      // MovingTarget::move
				geo_vel_t v;
				v.dlon = p->vlon; v.dlat = p->vlat;
				geo_move(&p->pos, &v, 1);
				// курс заново (OpenXcom — на каждом шаге): у цели (< 30 мин) — на каждом
				// макрошаге, дальше — раз в 3 (30 минут игры)
				if (p->steps <= 360 || ++p->aim_n >= 3) { p->aim_n = 0; aim(u); }
				else p->steps--;
				break;
			}
			{
				p->pos = p->dest;                // прибыл: скорость 0 (Ufo::think)
				p->speed = 0;
				p->vlon = p->vlat = 0;
				uint8_t was = p->flags & UF_DETECTED;
				uint8_t r = ufo_reached(u);
				p = &ST->ufo[u];
				log2("ufo: reached slot/status*10+pt ", u, p->status * 10 + p->traj_pt);
				if (was && !(p->flags & UF_DETECTED) && followers(u) && r != UR_ASSAULT)
					gev_push(GE_UFO_LOST, NONE8, u, 0);
				if (r < MAX_SITES) {
					ST->site[r].flags |= SITE_DETECTED;
					gev_push(GE_SITE, NONE8, r, 0);
				}
				if (r == UR_ASSAULT) base_attack(u);
			}
			break;
		case US_LANDED:
			if (p->secs > 5) { p->secs -= 5; break; }
			p->secs = 0;
			{
				uint8_t was = p->flags & UF_DETECTED;
				ufo_lifting(u);
				p = &ST->ufo[u];
				if (was && !(p->flags & UF_DETECTED) && followers(u)) gev_push(GE_UFO_LOST, NONE8, u, 0);
			}
			break;
		case US_CRASHED:
			p->flags |= UF_DETECTED;
			break;
		}
		if (p->status == US_DESTROYED) dead = 1;
	}
	if (!dead) return;
	p = ST->ufo;
	for (uint8_t u = 0; u < MAX_UFOS; u++, p++)  // уборка уничтоженных
		if (p->type != NONE8 && p->status == US_DESTROYED) ufo_free(u);
}

// ---------------------------------------------------------------- обнаружение

// Ufo::getVisibility: размер (радиус 2..6) и высота
static int8_t visibility(const ufo_t *p)
{
	rtab_t t;
	rtab_open(RES_RULE_UFOS, &t);
	int8_t size = (int8_t)(15 * ((int8_t)(rtab_word(&t, p->type, offsetof(r_ufos_t, radius)) & 0xFF) - 4));
	switch (p->altitude) {
	case 0: return -30;
	case 1: return size - 20;
	case 2: return size - 10;
	case 3: return size;
	default: return size - 10;
	}
}

// cos угла радиуса радара (мили) — угол = мили / 60 градусов
static int16_t range_cos(uint16_t miles)
{
	uint32_t a = (uint32_t)miles * 65536ul / 21600ul;
	return a >= 16384 ? -16384 : icos((uint16_t)a);
}

// Base::detect (радары баз): 0 нет, 1 обычный, 2 гиперволновой
static uint8_t base_detect(uint8_t b, const ufo_t *p, uint8_t only_range)
{
	rtab_t t;
	r_facilities_t f;
	base_t *bs = &ST->base[b];
	geo_t bp = bs->pos, up = p->pos;
	int16_t c = geo_cos(&bp, &up);
	uint16_t chance = 0;
	uint8_t inside = 0;
	rtab_open(RES_RULE_FACILITIES, &t);
	for (uint8_t i = 0; i < MAX_FACILITIES; i++) {
		facility_t *fc = &ST->base[b].fac[i];
		if (fc->type == NONE8 || fc->days) continue;
		uint8_t type = fc->type;
		rtab_get(&t, type, &f);
		if (!f.radar_range || c < range_cos(f.radar_range)) continue;
		inside = 1;
		if (f.flags & FACILITIES_F_HYPER) {
			if (only_range || f.radar_chance == 100 || rng_percent(f.radar_chance)) return 2;
		} else
			chance += f.radar_chance;
	}
	if (only_range) return inside;
	if (!chance) return 0;
	chance = (uint16_t)((int16_t)chance * (100 + visibility(p)) / 100);
	return rng_percent(chance > 255 ? 255 : (uint8_t)chance) ? 1 : 0;
}

// Craft::detect / insideRadarRange: корабли в полёте
static uint8_t craft_detect(uint8_t b, const ufo_t *p, uint8_t only_range)
{
	rtab_t t;
	r_crafts_t r;
	rtab_open(RES_RULE_CRAFTS, &t);
	for (uint8_t c = 0; c < MAX_CRAFTS; c++) {
		craft_t *cr = &ST->craft[c];
		if (cr->type == NONE8 || cr->base != b || cr->status != CS_OUT || cr->transit) continue;
		uint8_t type = cr->type;
		geo_t cp = cr->pos, up = p->pos;
		rtab_get(&t, type, &r);
		if (!r.radar_range || geo_cos(&cp, &up) < range_cos(r.radar_range)) continue;
		if (only_range || r.radar_chance == 100) return 1;
		int16_t ch = (int16_t)r.radar_chance * (100 + visibility(p)) / 100;
		if (rng_percent(ch < 0 ? 0 : (uint8_t)ch)) return 1;
	}
	return 0;
}

// time30Minutes: очки пришельцам за НЛО, обнаружение и потеря
static void ufo_detect(uint8_t u)
{
	ufo_t *p = &ST->ufo[u];
	rtab_t t;
	rtab_open(RES_RULE_UFOS, &t);
	int16_t pts = (int16_t)rtab_word(&t, p->type, offsetof(r_ufos_t, mission_score));
	if (p->status == US_LANDED) pts *= 2;
	geo_t pos = p->pos;
	add_activity(&pos, pts, 0);
	p = &ST->ufo[u];
	uint8_t det = 0, hyper = 0;
	if (!(p->flags & UF_DETECTED)) {
		for (uint8_t b = 0; b < MAX_BASES && !hyper; b++) {
			if (!ST->base[b].name[0]) continue;
			switch (base_detect(b, p, 0)) {
			case 2: p->flags |= UF_HYPER; hyper = 1;   // и обычное тоже
			case 1: det = 1;
			}
			if (!det && craft_detect(b, p, 0)) det = 1;
		}
		if (det) {
			p->flags |= UF_DETECTED;
			log2("ufo: detected slot/hyper ", u, (p->flags & UF_HYPER) ? 1 : 0);
			if (!p->id) p->id = ++ST->ids[ID_UFO];   // UfoDetectedState: номер при первом обнаружении
			if (p->status == US_LANDED && !p->land_id) p->land_id = ++ST->ids[ID_LANDING];
			gev_push(GE_UFO_DETECTED, NONE8, u, (p->flags & UF_HYPER) ? 1 : 0);
		}
	} else {
		for (uint8_t b = 0; b < MAX_BASES && !hyper; b++) {
			if (!ST->base[b].name[0]) continue;
			switch (base_detect(b, p, 1)) {
			case 2: det = 1; hyper = 1; p->flags |= UF_HYPER; break;
			case 1: det = 1; break;
			}
			if (!det && craft_detect(b, p, 1)) det = 1;
		}
		if (!det) {
			p->flags &= ~(UF_DETECTED | UF_HYPER);
			if (followers(u)) gev_push(GE_UFO_LOST, NONE8, u, 0);
		}
	}
}

void ufo_30min(void) __banked
{
	if (!ST->nufos) return;
	for (uint8_t u = 0; u < MAX_UFOS; u++) {    // expireCrashedUfo
		ufo_t *p = &ST->ufo[u];
		if (p->type == NONE8 || p->status != US_CRASHED) continue;
		if (p->secs >= 1800) p->secs -= 1800;
		else p->status = US_DESTROYED;
	}
}

void ufo_detect_all(void) __banked
{
	if (!ST->nufos) return;
	for (uint8_t u = 0; u < MAX_UFOS; u++) {
		ufo_t *p = &ST->ufo[u];
		if (p->type == NONE8 || (p->status != US_FLYING && p->status != US_LANDED)) continue;
		ufo_detect(u);
	}
}

// ---------------------------------------------------------------- места миссий

// processMissionSite (каждые 30 минут): очки или штраф, истечение
void sites_30min(void) __banked
{
	rtab_t t;
	r_alienDeployments_t d;
	if (!ST->nsites) return;
	rtab_open(RES_RULE_ALIENDEPLOYMENTS, &t);
	for (uint8_t s = 0; s < MAX_SITES; s++) {
		site_t *st = &ST->site[s];
		if (!st->id) continue;
		uint8_t remove = st->secs < 1800;
		if (!remove) st->secs -= 1800;
		else {                                   // кто-то летит — остаётся (CHEEKY EXPLOIT)
			for (uint8_t c = 0; c < MAX_CRAFTS && remove; c++) {
				craft_t *cr = &ST->craft[c];
				if (cr->type != NONE8 && cr->status == CS_OUT && cr->dest_kind == DK_SITE && cr->dest == s) remove = 0;
			}
		}
		uint8_t dep = st->deployment;
		rtab_get(&t, dep, &d);
		geo_t pos = ST->site[s].pos;
		add_activity(&pos, remove ? d.despawn_penalty : d.points, 0);
		if (remove) { ST->site[s].id = 0; ST->nsites--; }
	}
}

// time1Hour: первое не обнаруженное место миссии — окно
void sites_hour(void) __banked
{
	if (!ST->nsites) return;
	for (uint8_t s = 0; s < MAX_SITES; s++) {
		site_t *st = &ST->site[s];
		if (!st->id || (st->flags & SITE_DETECTED)) continue;
		st->flags |= SITE_DETECTED;
		gev_push(GE_SITE, NONE8, s, 0);
		break;
	}
}

// ---------------------------------------------------------------- базы

// Base::getDetectionChance: (Σ size² готовых / 6 + 15) / (щиты разума + 1) %
static uint8_t detection_chance(uint8_t b)
{
	rtab_t t;
	r_facilities_t f;
	uint16_t sq = 0;
	uint8_t mind = 0;
	rtab_open(RES_RULE_FACILITIES, &t);
	for (uint8_t i = 0; i < MAX_FACILITIES; i++) {
		facility_t *fc = &ST->base[b].fac[i];
		if (fc->type == NONE8) continue;
		uint8_t type = fc->type, days = fc->days;
		rtab_get(&t, type, &f);
		if (f.flags & FACILITIES_F_MIND) mind++;
		if (!days) sq += f.size * f.size;
	}
	return (uint8_t)((sq / 6 + 15) / (mind + 1));
}

// DetectXCOMBase, первая часть: НЛО миссии возмездия (после 2-й точки, не в зоне 5,
// не штурм, не разбито) ищет базы; дальность sightRange -> cos (0 — не ищет).
static int16_t ufo_seeker(uint8_t u, int16_t *cosr)
{
	ufo_t *p = &ST->ufo[u];
	rtab_t t;
	if (p->type == NONE8 || p->traj_pt <= 1 || p->status == US_CRASHED || p->status == US_DESTROYED) return 0;
	uint8_t tr = p->traj, pt = p->traj_pt, type = p->type;
	rtab_open(RES_RULE_ALIENMISSIONS, &t);
	if ((rtab_word(&t, ST->mission[p->mission].type, offsetof(r_alienMissions_t, objective)) & 0xFF) != OBJ_RETALIATION) return 0;
	rtab_open(RES_RULE_UFOTRAJECTORIES, &t);
	r_ufoTrajectories_t r;
	rtab_get(&t, tr, &r);
	if (r.assault) return 0;
	int16_t wp[3];
	if (pt * 3 + 2 >= r.waypoints.n) return 0;
	rtab_tail(&t, r.waypoints.off + pt * 6, wp, 6);
	if (wp[0] == 5) return 0;
	rtab_open(RES_RULE_UFOS, &t);
	*cosr = range_cos(rtab_word(&t, type, offsetof(r_ufos_t, sight_range)));
	return 1;
}

// time10Minutes: последняя замеченная база в каждом регионе — цель возмездия
void bases_10min(void) __banked
{
	uint8_t found[MAX_REGIONS], seek[MAX_UFOS], ns = 0;
	int16_t cosr[MAX_UFOS];
	if (!ST->nufos) return;
	ufo_t *p = ST->ufo;
	for (uint8_t u = 0; u < MAX_UFOS; u++, p++)  // сначала — кто вообще ищет (обычно никто)
		if (p->type != NONE8 && p->mission != NONE8 && p->traj_pt > 1 && ufo_seeker(u, &cosr[ns])) seek[ns++] = u;
	if (!ns) return;
	memset(found, NONE8, sizeof found);
	for (uint8_t b = 0; b < MAX_BASES; b++) {
		if (!ST->base[b].name[0]) continue;
		for (uint8_t k = 0; k < ns; k++) {
			geo_t bp = ST->base[b].pos, up = ST->ufo[seek[k]].pos;
			if (geo_cos(&bp, &up) <= cosr[k] || !rng_percent(detection_chance(b))) continue;
			uint8_t r = region_at((uint16_t)((uint32_t)ST->base[b].pos.lon >> 16), (int16_t)(ST->base[b].pos.lat >> 16));
			if (r < MAX_REGIONS) found[r] = b;
			break;
		}
	}
	for (uint8_t r = 0; r < MAX_REGIONS; r++)
		if (found[r] != NONE8) ST->base[found[r]].flags |= BF_RETALIATION;
}

// time1Month: 20 % — агенты находят первую не обнаруженную базу пришельцев
void abases_month(void) __banked
{
	uint8_t any = 0;
	for (uint8_t b = 0; b < MAX_ALIEN_BASES; b++) any |= ST->abase[b].id != 0;
	if (!any || !rng_percent(20)) return;
	for (uint8_t b = 0; b < MAX_ALIEN_BASES; b++) {
		alienbase_t *ab = &ST->abase[b];
		if (!ab->id || (ab->flags & AB_DISCOVERED)) continue;
		ab->flags |= AB_DISCOVERED;
		gev_push(GE_ALIEN_BASE, NONE8, b, 0);
		break;
	}
}

// time1Day: очки базам пришельцев, снабжение (genMission с шансом genMissionFreq)
void abases_day(void) __banked
{
	rtab_t t;
	r_alienDeployments_t d;
	static int16_t gm[16];
	for (uint8_t b = 0; b < MAX_ALIEN_BASES; b++) {
		if (!ST->abase[b].id) continue;
		uint8_t dep = ST->abase[b].deployment;
		rtab_open(RES_RULE_ALIENDEPLOYMENTS, &t);
		rtab_get(&t, dep, &d);
		geo_t pos = ST->abase[b].pos;
		add_activity(&pos, d.points, 0);
	}
	for (uint8_t b = 0; b < MAX_ALIEN_BASES; b++) {
		if (!ST->abase[b].id) continue;
		uint8_t dep = ST->abase[b].deployment;
		rtab_open(RES_RULE_ALIENDEPLOYMENTS, &t);
		rtab_get(&t, dep, &d);
		uint8_t n = d.gen_mission.n < 16 ? d.gen_mission.n : 16;
		if (!n) continue;
		rtab_tail(&t, d.gen_mission.off, gm, n * 2);
		int16_t type = weights_choose(gm, n / 2);
		if (type < 0 || !rng_percent(d.gen_mission_freq)) continue;
		geo_t pos = ST->abase[b].pos;
		uint8_t r = region_at((uint16_t)((uint32_t)pos.lon >> 16), (int16_t)(pos.lat >> 16));
		if (r != NONE8) mission_supply((uint8_t)type, r, ST->abase[b].race, b);
	}
}
