// Банк 22: логика воздушного (подводного) боя — DogfightState::update / think и
// GeoscapeState::handleDogfights / startDogfight OpenXcom (REF/OpenXcom/src/Geoscape/
// DogfightState.cpp, GeoscapeState.cpp, Savegame/CraftWeaponProjectile.cpp). 14_todo §3.5.
// Окна и рисование — src/geoscape/earth/scr_dogf.c (банк 23), данные — dogfight.h (Win1).
//
// До DF_MAX боёв. Развёрнутые — окна экрана SCR_DOGFIGHT (время стоит, пока развёрнут
// хоть один), свёрнутые — значки на глобусе (время идёт, бой лишь следит за условиями
// конца). Тик — 30 мс (Options::dogfightSpeed) по счётчику кадров, сразу все бои.
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "res.h"
#include "rules.h"
#include "state.h"
#include "game.h"
#include "ui.h"
#include "ui_ids.h"
#include "str_ids.h"
#include "screens.h"
#include "dogfight.h"
#include "dbg.h"

// Журнал боя в консоль эмулятора (проверка сценариев): «df: что a b»
static void log2(const char *s, uint16_t a, uint16_t b)
{
	dbg_puts(s);
	dbg_dec(a);
	dbg_puts(" ");
	dbg_dec(b);
	dbg_puts("\n");
}

extern volatile uint16_t frames;         // crt0.s

dogfight_t df[DF_MAX];
uint8_t df_count, df_nmax, df_ticks;
uint8_t c_craft0, c_craft1, c_radar0, c_radar1, c_dmg0, c_dmg1, c_blob, c_meter;
uint8_t c_disw, c_disr, c_disa, c_num, c_dist, c_text, c_minnum, c_btn, tftd;
uint8_t df_dbg_type;                     // отладочный бой (сценарии, poke): тип НЛО,
uint8_t df_dbg_alt = 1, df_dbg_n = 1;    // его высота (ALT_*), число кораблей против него
static uint16_t ufo_done;                // _interceptionProcessed за тик (бит НЛО)
static uint8_t shooting[MAX_UFOS];       // Ufo::_shootingAt: номер перехвата под огнём
static uint16_t df_frame;
static uint8_t df_acc;                   // мс сверх целых тиков

static const uint16_t mode_str[5] = { STR_STANDOFF, STR_CAUTIOUS_ATTACK, STR_STANDARD_ATTACK, STR_AGGRESSIVE_ATTACK, STR_DISENGAGING };

static uint8_t coef(void)
{
	return gv.diff_coef[ST->difficulty < 5 ? ST->difficulty : 4];
}

static uint8_t ufo_crashed(const dogfight_t *d)
{
	return ST->ufo[d->ufo].damage > d->udmax / 2;
}

static uint8_t ufo_destroyed(const dogfight_t *d)
{
	return ST->ufo[d->ufo].damage >= d->udmax;
}

static uint8_t craft_dead(const dogfight_t *d)
{
	return ST->craft[d->craft].damage >= d->cdmax;
}

static uint8_t dmg_pct(const dogfight_t *d)
{
	return (uint8_t)((uint32_t)ST->craft[d->craft].damage * 100 / d->cdmax);
}

static uint8_t weapon(const dogfight_t *d, uint8_t i)
{
	return i < d->nweap && ST->craft[d->craft].weap[i].type != NONE8;
}

static void set_status(dogfight_t *d, uint16_t s)
{
	if (d->status != s) d->dirty |= DR_STATUS;
	d->status = s;
	d->timeout = 50;
}

static void count_max(void)
{
	uint8_t n = 0, m = 0;
	for (uint8_t k = 0; k < DF_MAX; k++) {
		if (df[k].craft == NONE8) continue;
		n++;
		if (!(df[k].flags & DFF_MIN)) m++;
	}
	df_count = n;
	df_nmax = m;
}

// calculateWindowPosition: по числу боёв и номеру перехвата (x/2, y/2)
static void place(void)
{
	static const uint8_t pos[4][4][2] = {
		{ { 40, 26 }, { 40, 26 }, { 40, 26 }, { 40, 26 } },
		{ { 40, 0 }, { 40, 52 }, { 40, 52 }, { 40, 52 } },
		{ { 40, 0 }, { 0, 52 }, { 80, 52 }, { 80, 52 } },
		{ { 0, 0 }, { 80, 0 }, { 0, 52 }, { 80, 52 } },
	};
	uint8_t n = df_count;
	for (uint8_t k = 0; k < DF_MAX; k++) {
		dogfight_t *d = &df[k];
		if (d->craft == NONE8) continue;
		uint8_t i = d->num - 1;
		d->x = pos[n - 1][i][0] * 2;
		d->y = pos[n - 1][i][1] * 2;
	}
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

// ---------------------------------------------------------------- снаряды, огонь

static proj_t *proj_new(dogfight_t *d)
{
	for (uint8_t i = 0; i < NPROJ; i++) {
		proj_t *p = &d->p[i];
		if (p->type != NONE8) continue;
		memset(p, 0, sizeof *p);
		return p;
	}
	return 0;
}

// CraftWeaponProjectile::move
static void proj_move(proj_t *p)
{
	if (p->type >= CWPT_BEAM) {
		p->state /= 2;
		if (p->state == 1) p->flags |= PF_REMOVE;
		return;
	}
	uint16_t ch = p->speed, r8 = (uint16_t)p->range * 8;
	if (p->covered / 8 < p->range && (p->covered + ch) / 8 >= p->range) ch = r8 - p->covered;
	if (p->covered / 8 >= p->range) p->flags |= PF_MISSED;
	if (p->dir == D_UP) p->pos += ch;
	else p->pos -= ch;
	p->covered += ch;
}

// fireWeapon1/2 + CraftWeapon::fire
static void fire(dogfight_t *d, uint8_t i)
{
	proj_t *p = proj_new(d);
	if (!p) return;
	ST->craft[d->craft].weap[i].ammo--;
	log2("df: fire weapon/ammo ", i, ST->craft[d->craft].weap[i].ammo);
	d->wcd[i] = d->wint[i];
	const wrule_t *r = &d->w[i];
	p->type = r->ptype; p->speed = r->pspeed; p->acc = r->acc; p->dmg = r->damage; p->range = r->range;
	p->dir = D_UP;
	p->hpos = i ? 1 : -1;
	if (p->type >= CWPT_BEAM) p->state = 8;
	d->dirty |= DR_BATTLE | DR_AMMO;
}

// ufoFireWeapon: плазменный луч, точность 60
static void ufo_fire(dogfight_t *d)
{
	int16_t fc = (int16_t)d->ureload - 2 * coef();
	if (fc < 1) fc = 1;
	ST->ufo[d->ufo].fire_cd = rng_range(0, (uint16_t)fc) + fc;
	set_status(d, STR_UFO_RETURN_FIRE);
	proj_t *p = proj_new(d);
	if (!p) return;
	p->type = CWPT_PLASMA; p->acc = 60; p->dmg = d->upower;
	p->dir = D_DOWN; p->state = 8;
	p->pos = d->dist;
}

// minimumDistance / maximumDistance: наибольшая / наименьшая дальность с боезапасом
static void set_range(dogfight_t *d, uint8_t longest)
{
	uint8_t best = longest ? 0 : 0xFF;
	for (uint8_t i = 0; i < 2; i++) {
		if (!weapon(d, i) || !ST->craft[d->craft].weap[i].ammo) continue;
		uint8_t r = d->w[i].range;
		if (longest ? r > best : r < best) best = r;
	}
	d->target = best && best != 0xFF ? (int16_t)best * 8 : STANDOFF;
}

// endDogfight
static void end_df(dogfight_t *d)
{
	if (d->flags & DFF_OVER) return;
	craft_t *cr = &ST->craft[d->craft];
	cr->flags &= ~CRF_BATTLE;
	cr->order = 0;
	d->flags |= DFF_OVER;
}

// Ufo::setDamage
static void ufo_damage(dogfight_t *d, uint16_t dmg)
{
	ufo_t *u = &ST->ufo[d->ufo];
	u->damage += dmg;
	if (u->damage >= d->udmax) u->status = US_DESTROYED;
	else if (u->damage > d->udmax / 2) u->status = US_CRASHED;
}

// update: НЛО разбито — очки, возмездие, обломки или уничтожение над водой
static void shot_down(dogfight_t *d)
{
	uint8_t c = d->craft, u = d->ufo;
	ufo_t *uf = &ST->ufo[u];
	uint8_t mi = uf->mission, k = coef();
	if (mi < MAX_MISSIONS && ST->mission[mi].type != NONE8) {
		ufo_shot_down(u);
		rtab_t t;
		rtab_open(RES_RULE_ALIENMISSIONS, &t);
		int16_t odds = (int8_t)(rtab_word(&t, ST->mission[mi].type, offsetof(r_alienMissions_t, retaliation_odds)) & 0xFF);
		if (odds == -1) odds = 100 - 4 * (24 - k);
		if (odds > 0 && rng_percent(odds > 100 ? 100 : (uint8_t)odds)) {
			uint8_t region;
			if (rng_percent(50 - 6 * k)) region = ST->mission[mi].region;
			else {                                   // регион базы корабля
				geo_t bp = ST->base[ST->craft[c].base].pos;
				region = region_at((uint16_t)((uint32_t)bp.lon >> 16), (int16_t)(bp.lat >> 16));
			}
			if (region != NONE8) mission_retaliation(region, ST->mission[mi].race);
		}
	}
	uf = &ST->ufo[u];
	geo_t pos = uf->pos;
	uint8_t mine = uf->shot_by == c;
	if (ufo_destroyed(d)) {
		if (mine) { add_activity(&pos, d->uscore * 2, 1); set_status(d, STR_UFO_DESTROYED); }
		d->flags |= DFF_KILLUFO;
	} else {
		if (mine) { set_status(d, STR_UFO_CRASH_LANDS); add_activity(&pos, d->uscore, 1); }
		if (!inside_land(&pos)) {
			ST->ufo[u].status = US_DESTROYED;
			d->flags |= DFF_KILLUFO;
		} else {
			uint32_t s = (uint32_t)rng_range(24, 96) * 3600;
			uf = &ST->ufo[u];
			uf->secs = s;
			uf->altitude = ALT_GROUND;
			if (!uf->crash_id) uf->crash_id = ++ST->ids[ID_CRASH];
		}
	}
	d->timeout += 30;
	if (!mine) { d->timeout += 50; d->hit = 3; }
}

// Снаряды: движение, попадания, промахи; 1 — свои в полёте
static uint8_t projectiles(dogfight_t *d, uint8_t *fin)
{
	uint8_t fly = 0;
	for (uint8_t i = 0; i < NPROJ; i++) {
		proj_t *p = &d->p[i];
		if (p->type == NONE8) continue;
		d->dirty |= DR_BATTLE;
		proj_move(p);
		uint8_t beam = p->type >= CWPT_BEAM;
		if (p->dir == D_UP) {
			if ((p->pos >= d->dist || (beam && (p->flags & PF_REMOVE))) && !ufo_crashed(d) && !(p->flags & PF_MISSED)) {
				// попадание: (точность · (100 + 300 / (5 − размер)) + 100) / 200 %
				uint16_t ch = ((uint16_t)p->acc * (100 + 300 / (5 - d->size)) + 100) / 200;
				if (rng_percent(ch > 100 ? 100 : (uint8_t)ch)) {
					ufo_damage(d, rng_range(p->dmg / 2, p->dmg));
					log2("df: ufo hit dist/damage ", d->dist, ST->ufo[d->ufo].damage);
					if (ufo_crashed(d)) {
						ufo_t *uf = &ST->ufo[d->ufo];
						uf->shot_by = d->craft;
						uf->speed = 0;
						uf->vlon = uf->vlat = 0;
						d->flags &= ~(DFF_BREAK | DFF_END);
						*fin = 0;
					}
					if (!d->hit) { d->flags |= DFF_ANIMHIT; d->hit = 3; }
					set_status(d, STR_UFO_HIT);
					p->flags |= PF_REMOVE;
				} else if (beam)
					p->flags |= PF_REMOVE;
				else
					p->flags |= PF_MISSED;
			}
			if (!beam && p->pos / 8 >= (int16_t)p->range) p->flags |= PF_REMOVE;
			else if (!ufo_crashed(d)) fly = 1;
		} else if (!beam || (p->flags & PF_REMOVE)) {
			if (rng_percent(p->acc)) {
				uint16_t dm = rng_range(0, d->upower);
				if (dm) {
					ST->craft[d->craft].damage += dm;
					log2("df: craft hit dist/damage ", d->dist, ST->craft[d->craft].damage);
					d->dirty |= DR_DAMAGE;
					set_status(d, STR_INTERCEPTOR_DAMAGED);
					if (d->mode == 1 && dmg_pct(d) >= 50) d->target = STANDOFF;
				}
			}
			p->flags |= PF_REMOVE;
		}
	}
	for (uint8_t i = 0; i < NPROJ; i++) {
		proj_t *p = &d->p[i];
		if (p->type != NONE8 && ((p->flags & PF_REMOVE) || ((p->flags & PF_MISSED) && p->pos <= 0))) p->type = NONE8;
	}
	return fly;
}

// ---------------------------------------------------------------- тик боя

// DogfightState::update (с логической частью animate)
static void update(dogfight_t *d)
{
	uint8_t c = d->craft, u = d->ufo, fin = 0, fly = 0;
	craft_t *cr = &ST->craft[c];
	ufo_t *uf = &ST->ufo[u];
	uint8_t mini = (d->flags & DFF_MIN) != 0;
	if (cr->type == NONE8 || cr->dest_kind != DK_UFO || cr->dest != u || !(cr->flags & CRF_BATTLE)
	    || (cr->flags & CRF_LOWFUEL) || (mini && ufo_crashed(d))) {
		end_df(d);
		return;
	}
	if (!mini) {
		// animate: текст гаснет, кадры попадания, пятно разбитого НЛО тает
		if (!d->timeout) { if (d->status != NOSTR) { d->status = NOSTR; d->dirty |= DR_STATUS; } }
		else d->timeout--;
		uint8_t last = 0;
		if ((d->flags & DFF_ANIMHIT) && d->hit) {
			if (!--d->hit) { d->flags &= ~DFF_ANIMHIT; last = 1; }
			d->dirty |= DR_BATTLE;
		}
		if (ufo_crashed(d) && !d->hit && !last && d->size >= 0) { d->size--; d->dirty |= DR_BATTLE; }
		uint16_t bit = 1u << u;
		if (!ufo_crashed(d) && !craft_dead(d) && !(ufo_done & bit)) {
			ufo_done |= bit;
			if (uf->escape_cd && !--uf->escape_cd) {  // уход: полная скорость
				geo_t dst = uf->dest;
				ufo_set_course(u, &dst, d->usmax);
			}
			uf = &ST->ufo[u];
			if (uf->fire_cd) uf->fire_cd--;
		}
	}
	if (ST->ufo[u].speed > d->csmax) {           // корабль не догоняет
		d->flags |= DFF_BREAK;
		fin = 1;
		set_status(d, STR_UFO_OUTRUNNING_INTERCEPTOR);
	} else
		d->flags &= ~DFF_BREAK;

	if (!mini) {
		uint8_t crashed = ufo_crashed(d), dead = craft_dead(d);
		int16_t ch = 0;
		if (!(d->flags & DFF_BREAK)) {
			if (d->dist < d->target && !crashed && !dead) {
				ch = 4;
				if (d->dist + ch > d->target) ch = d->target - d->dist;
			} else if (d->dist > d->target && !crashed && !dead)
				ch = -2;
			for (uint8_t i = 0; i < NPROJ; i++) {    // свои ракеты с кораблём не сдвигаются
				proj_t *p = &d->p[i];
				if (p->type < CWPT_BEAM && p->dir == D_UP) p->pos += ch;
			}
		} else
			ch = 4;
		if (ch) { d->dist += ch; d->dirty |= DR_DIST | DR_BATTLE; }
		fly = projectiles(d, &fin);
		for (uint8_t i = 0; i < 2; i++) {           // огонь и дистанция по режиму
			if (!weapon(d, i)) continue;
			uint16_t ammo = ST->craft[c].weap[i].ammo;
			uint8_t wt = d->wcd[i], m = d->mode;
			if (!wt && d->dist <= (int16_t)d->w[i].range * 8 && ammo && m != 0 && m != 4 && !ufo_crashed(d) && !craft_dead(d)) {
				if (!(d->flags & (i ? DFF_W2OFF : DFF_W1OFF))) { fire(d, i); fly = 1; }
			} else if (wt)
				d->wcd[i] = wt - 1;
			if (!ST->craft[c].weap[i].ammo && !fly && !craft_dead(d)) {
				if (m == 1) set_range(d, 1);
				else if (m == 2) set_range(d, 0);
			}
		}
		uint8_t sh = shooting[u], me = d->num;     // огонь НЛО — по одному перехвату
		if (d->dist <= (int16_t)d->urange * 8 && !ufo_crashed(d) && !craft_dead(d)) {
			if (!sh) shooting[u] = sh = me;
			if (sh == me && !ST->ufo[u].fire_cd) ufo_fire(d);
		} else if (sh == me)
			shooting[u] = 0;
	}

	uint8_t crashed = ufo_crashed(d), dead = craft_dead(d);
	uint16_t f = d->flags;
	if ((f & DFF_END) && (((d->dist > 640 || (f & DFF_MIN)) && (d->mode == 4 || (f & DFF_BREAK)))
	    || (!d->timeout && (crashed || dead)))) {
		if (f & DFF_BREAK) craft_set_dest(c, DK_UFO, u);
		if (!(f & DFF_KILLCRAFT) && ((f & DFF_KILLUFO) || d->mode == 4)) craft_return(c);
		if (crashed)                                 // преследователи без экипажа — домой
			for (uint8_t k = 0; k < MAX_CRAFTS; k++) {
				craft_t *o = &ST->craft[k];
				if (o->type == NONE8 || o->status != CS_OUT || o->dest_kind != DK_UFO || o->dest != u) continue;
				if (!crew(k)) craft_return(k);
			}
		end_df(d);
	}
	if (d->dist > 640 && (d->flags & DFF_BREAK)) fin = 1;
	if (!(d->flags & DFF_END)) {
		if (dead) {
			set_status(d, STR_INTERCEPTOR_DESTROYED);
			d->timeout += 30;
			fin = 1;
			d->flags |= DFF_KILLCRAFT;
			shooting[u] = 0;
		}
		if (crashed) { shot_down(d); fin = 1; }
	}
	if (!fly && fin) d->flags |= DFF_END;
}

static void think(dogfight_t *d)
{
	if (!(d->flags & DFF_OVER)) {
		update(d);
		// _craftDamageAnimTimer (500 мс): цвет повреждений мигает
		if (!(d->flags & DFF_MIN) && dmg_pct(d) && ++d->dtimer >= 17) {
			d->dtimer = 0;
			if (--d->dcolor < c_dmg0) d->dcolor = c_dmg1;
			d->dirty |= DR_DAMAGE;
		}
	}
	craft_t *cr = &ST->craft[d->craft];
	ufo_t *uf = &ST->ufo[d->ufo];
	if (cr->type == NONE8 || uf->type == NONE8 || !(cr->flags & CRF_BATTLE) || cr->dest_kind != DK_UFO
	    || cr->dest != d->ufo || uf->status == US_LANDED)
		end_df(d);
}

// handleDogfights: все бои за тик; 1 — бой кончился или развернулся
static uint8_t df_tick(void)
{
	uint8_t changed = 0;
	ufo_done = 0;
	for (uint8_t k = 0; k < DF_MAX; k++) {
		dogfight_t *d = &df[k];
		if (d->craft == NONE8) continue;
		if (d->flags & DFF_MIN) {                    // TFTD: ждал глубины / воды
			ufo_t *uf = &ST->ufo[d->ufo];
			if ((d->flags & DFF_WAITPOLY) && uf->type != NONE8 && inside_land(&uf->pos)) {
				d->flags &= ~(DFF_MIN | DFF_WAITPOLY); changed = 1;
			} else if ((d->flags & DFF_WAITALT) && (int8_t)ST->ufo[d->ufo].altitude <= d->calt) {
				d->flags &= ~(DFF_MIN | DFF_WAITALT); changed = 1;
			}
		}
		think(d);
		if (!(d->flags & DFF_OVER)) continue;
		uint8_t c = d->craft, u = d->ufo;
		log2("df: end craft/ufo status ", c, ST->ufo[u].status);
		if ((d->flags & DFF_KILLCRAFT) && ST->craft[c].type != NONE8) craft_destroyed(c);
		d->craft = NONE8;
		uint8_t other = 0;
		for (uint8_t j = 0; j < DF_MAX; j++) if (df[j].craft != NONE8 && df[j].ufo == u) other = 1;
		if (!other) shooting[u] = 0;
		changed = 1;
	}
	count_max();
	return changed;
}

uint8_t df_run(void) __banked
{
	uint16_t now = frames, el = (uint16_t)(now - df_frame);
	uint8_t changed = 0, n = 0;
	df_frame = now;
	if (el > 3) el = 3;                          // долгий кадр — не больше двух тиков
	uint16_t acc = df_acc + el * 20;
	while (acc >= 30 && n < 2) { acc -= 30; n++; changed |= df_tick(); }
	df_acc = acc >= 30 ? 0 : (uint8_t)acc;
	df_ticks = n;
	return changed;
}

// ---------------------------------------------------------------- начало и кнопки

static void load_colors(void)
{
	c_craft0 = ui_color_in(UI_SCR_DOGFIGHT, UI_EL_CRAFTRANGE, 0);
	c_craft1 = ui_color_in(UI_SCR_DOGFIGHT, UI_EL_CRAFTRANGE, 1);
	c_radar0 = ui_color_in(UI_SCR_DOGFIGHT, UI_EL_RADARRANGE, 0);
	c_radar1 = ui_color_in(UI_SCR_DOGFIGHT, UI_EL_RADARRANGE, 1);
	c_dmg0 = ui_color_in(UI_SCR_DOGFIGHT, UI_EL_DAMAGERANGE, 0);
	c_dmg1 = ui_color_in(UI_SCR_DOGFIGHT, UI_EL_DAMAGERANGE, 1);
	c_blob = ui_color_in(UI_SCR_DOGFIGHT, UI_EL_RADARDETAIL, 0);
	c_meter = ui_color_in(UI_SCR_DOGFIGHT, UI_EL_RADARDETAIL, 1);
	c_disw = ui_color_in(UI_SCR_DOGFIGHT, UI_EL_DISABLEDWEAPON, 0);
	c_disr = ui_color_in(UI_SCR_DOGFIGHT, UI_EL_DISABLEDWEAPON, 1);
	c_disa = ui_color_in(UI_SCR_DOGFIGHT, UI_EL_DISABLEDAMMO, 0);
	c_num = ui_color_in(UI_SCR_DOGFIGHT, UI_EL_NUMBERS, 0);
	c_dist = ui_color_in(UI_SCR_DOGFIGHT, UI_EL_DISTANCE, 0);
	c_text = ui_color_in(UI_SCR_DOGFIGHT, UI_EL_TEXT, 0);
	c_minnum = ui_color_in(UI_SCR_DOGFIGHT, UI_EL_MINIMIZEDNUMBER, 0);
	c_btn = ui_color_in(UI_SCR_DOGFIGHT, UI_EL_STANDOFFBUTTON, 0);
	tftd = res_game() == 2;
}

// DogfightState(craft, ufo) + GeoscapeState::time5Seconds (глубина, вода) + startDogfight
uint8_t df_start(uint8_t c, uint8_t u) __banked
{
	rtab_t t;
	uint8_t k;
	if (!df_count) {
		for (k = 0; k < DF_MAX; k++) df[k].craft = NONE8;
		memset(shooting, 0, sizeof shooting);
		load_colors();
		df_frame = frames;
		df_acc = 0;
	}
	for (k = 0; k < DF_MAX && df[k].craft != NONE8; k++);
	if (k >= DF_MAX) return 0;
	dogfight_t *d = &df[k];
	memset(d, 0, sizeof *d);
	for (uint8_t i = 0; i < NPROJ; i++) d->p[i].type = NONE8;
	d->craft = c; d->ufo = u;
	d->dist = 640; d->target = STANDOFF;
	d->status = STR_STANDOFF; d->timeout = 50;
	d->dcolor = c_dmg0;
	d->height = NONE8;
	{
		r_crafts_t r;
		rtab_open(RES_RULE_CRAFTS, &t);
		rtab_get(&t, ST->craft[c].type, &r);
		d->nweap = r.weapons < 2 ? r.weapons : 2;
		d->csprite = (uint8_t)r.sprite; d->calt = r.max_altitude;
		d->cdmax = r.damage_max ? r.damage_max : 1; d->csmax = r.speed_max;
	}
	{
		r_ufos_t r;
		rtab_open(RES_RULE_UFOS, &t);
		rtab_get(&t, ST->ufo[u].type, &r);
		d->udmax = r.damage_max; d->usmax = r.speed_max; d->upower = r.power; d->ureload = r.reload;
		d->urange = (uint8_t)r.range; d->usprite = (uint8_t)r.sprite; d->uscore = r.score;
		uint16_t s = r.size;
		d->size = s == STR_VERY_SMALL ? 0 : s == STR_SMALL ? 1 : s == STR_MEDIUM_UC ? 2 : s == STR_LARGE ? 3 : 4;
		ufo_t *uf = &ST->ufo[u];
		if (!uf->escape_cd) {                        // НЛО ещё не в бою
			int32_t e = (int32_t)r.break_off_time + rng_range(0, r.break_off_time) - 30 * coef();
			uf = &ST->ufo[u];
			uf->fire_cd = 0;
			uf->escape_cd = e < 1 ? 1 : (uint16_t)e;
		}
	}
	{
		r_craftWeapons_t r;
		rtab_open(RES_RULE_CRAFTWEAPONS, &t);
		for (uint8_t i = 0; i < d->nweap; i++) {
			uint8_t type = ST->craft[c].weap[i].type;
			if (type == NONE8) continue;
			rtab_get(&t, type, &r);
			wrule_t *w = &d->w[i];
			w->range = (uint8_t)r.range; w->acc = r.accuracy; w->ptype = r.projectile_type; w->pspeed = r.projectile_speed;
			w->reload[0] = r.reload_cautious; w->reload[1] = r.reload_standard; w->reload[2] = r.reload_aggressive;
			w->sprite = (uint8_t)r.sprite; w->damage = r.damage;
			d->wint[i] = r.reload_standard;
		}
	}
	// номер перехвата — наименьший свободный; порядок корабля — следующий за наибольшим
	uint8_t used = 0;
	for (uint8_t j = 0; j < DF_MAX; j++) if (df[j].craft != NONE8 && j != k) used |= 1 << (df[j].num - 1);
	for (d->num = 1; used & (1 << (d->num - 1)); d->num++);
	craft_t *cr = &ST->craft[c];
	cr->flags |= CRF_BATTLE;
	if (!cr->order) {
		uint8_t mx = 0;
		for (uint8_t j = 0; j < MAX_CRAFTS; j++) if (ST->craft[j].type != NONE8 && ST->craft[j].order > mx) mx = ST->craft[j].order;
		ST->craft[c].order = mx + 1;
	}
	uint8_t ret = 1;
	if (d->calt > -1) {                              // только под водой: глубина, вода под кораблём
		geo_t cp = ST->craft[c].pos;
		if ((int8_t)ST->ufo[u].altitude > d->calt) { d->flags |= DFF_MIN | DFF_WAITALT; ret = 2; }
		else if (!inside_land(&cp)) { d->flags |= DFF_MIN | DFF_WAITPOLY; ret = 3; }
	}
	count_max();
	place();
	ctx.globe_lon = (uint16_t)((uint32_t)ST->craft[c].pos.lon >> 16);   // globe->center + timerReset
	ctx.globe_lat = (int16_t)(ST->craft[c].pos.lat >> 16);
	ST->speed = 0;
	return ret;
}

void df_reset(void) __banked
{
	for (uint8_t k = 0; k < DF_MAX; k++) df[k].craft = NONE8;
	df_count = df_nmax = 0;
	for (uint8_t c = 0; c < MAX_CRAFTS; c++) ST->craft[c].flags &= ~CRF_BATTLE;
}

// Кнопки режимов (ImageButton: группа переключается всегда, действие — если бой идёт)
void df_press(uint8_t k, uint8_t m) __banked
{
	dogfight_t *d = &df[k];
	d->mode = m;
	d->dirty |= DR_BTN;
	if (ufo_crashed(d) || craft_dead(d) || (d->flags & DFF_BREAK)) return;
	set_status(d, mode_str[m]);
	d->dirty |= DR_STATUS;
	if (m == 4) { d->flags |= DFF_END; d->target = 800; return; }
	d->flags &= ~DFF_END;
	if (!m) { d->target = STANDOFF; return; }
	for (uint8_t i = 0; i < 2; i++) if (weapon(d, i)) d->wint[i] = d->w[i].reload[m - 1];
	if (m == 1) set_range(d, 1);
	else if (m == 2) set_range(d, 0);
	else d->target = 64;
}

// btnMinimizeClick: только на дистанции standoff
uint8_t df_minimize(uint8_t k) __banked
{
	dogfight_t *d = &df[k];
	if (ufo_crashed(d) || craft_dead(d) || (d->flags & DFF_BREAK)) return 0;
	if (d->dist >= STANDOFF) { d->flags |= DFF_MIN; count_max(); return 1; }
	set_status(d, STR_MINIMISE_AT_STANDOFF_RANGE_ONLY);
	return 0;
}

// btnMinimizedIconClick: развернуть (TFTD — если глубина и вода позволяют)
uint8_t df_maximize(uint8_t k) __banked
{
	dogfight_t *d = &df[k];
	if (d->calt > -1) {
		geo_t cp = ST->craft[d->craft].pos;
		uint8_t e = (int8_t)ST->ufo[d->ufo].altitude > d->calt ? 1 : !inside_land(&cp) ? 2 : 0;
		if (e) {
			d->flags |= e == 1 ? DFF_WAITALT : DFF_WAITPOLY;
			ctx.craft = d->craft;
			ctx.item = e - 1;
			UI_GO(A_PUSH, SCR_DOGFIGHT_ERROR);
			return 2;
		}
	}
	d->flags &= ~DFF_MIN;
	count_max();
	return 1;
}

// Отладочное меню окон: бой без настоящих объектов — корабль с оружием у первой базы
// против подставного НЛО рядом (mission NONE8 — ход времени его не трогает, ufo.c)
void df_debug(void) __banked
{
	uint8_t u, n = 0;
	for (u = 0; u < MAX_UFOS && ST->ufo[u].type != NONE8; u++);
	if (u >= MAX_UFOS) return;
	for (uint8_t c = 0; c < MAX_CRAFTS && n < df_dbg_n; c++) {
		craft_t *cr = &ST->craft[c];
		if (cr->type == NONE8 || cr->weap[0].type == NONE8 || (cr->flags & CRF_BATTLE)) continue;
		if (!n++) {                               // подставное НЛО рядом с базой первого корабля
			ufo_t *p = &ST->ufo[u];
			memset(p, 0, sizeof *p);
			ST->nufos++;
			p->type = df_dbg_type; p->id = ++ST->ids[ID_UFO]; p->altitude = df_dbg_alt;
			p->status = US_FLYING; p->flags = UF_DETECTED; p->dest_base = p->shot_by = NONE8;
			p->mission = NONE8;
			p->pos = ST->base[ST->craft[c].base].pos;
			p->pos.lon += 0x01000000l;
			p->dest = p->pos;
		}
		cr = &ST->craft[c];
		if (cr->status != CS_OUT) ST->ncraft_out++;
		cr->status = CS_OUT;
		cr->pos = ST->ufo[u].pos;
		cr->dest_kind = DK_UFO; cr->dest = u; cr->takeoff = 0;
		uint8_t e = df_start(c, u);
		if (e > 1) gev_push(GE_DOGFIGHT, e - 1, c, u);   // TFTD: глубина / вода — окно ошибки
	}
	count_max();
}
