// Выстрел (банк 35): траектория в вокселях и урон — порт Projectile::calculateTrajectory /
// applyAccuracy (REF/OpenXcom/src/Battlescape/Projectile.cpp:120, :334), TileEngine::getOriginVoxel
// (TileEngine.cpp:3139), calculateLine в вокселях (:2225) с voxelCheck (:2525), TileEngine::hit (:1106)
// и BattleUnit::damage (REF/OpenXcom/src/Savegame/BattleUnit.cpp:1096).
// Воксель — 16x16x24 на клетку, слой маски — 2 вокселя высоты; маски — LOFTEMPS (слово на строку,
// бит 15 - x). Продление линии за цель (extendLine) сделано без тригонометрии: вектор на цель
// умножается так, чтобы длинная ось стала 2048 вокселей — направление то же, линия уходит за карту.
// Чем у нас проще: цель-юнит — середина его роста (canTargetUnit перебирает видимые точки);
// гравилифт в voxelCheck и смертельные раны в damage — позже.
#include <stdint.h>
#include <string.h>
#include "far.h"
#include "pages.h"
#include "res.h"
#include "res_ids.h"
#include "rules.h"
#include "game.h"
#include "pathfind.h"
#include "loader.h"
#include "bmenu.h"
#include "bfire.h"

// Память банка (build.ps1 ram = #B000): z -> этаж и слой маски, маски юнитов
static uint8_t __at(0xB000) zlev[256];
static uint8_t __at(0xB100) zlay[256];
static uint8_t __at(0xB200) uloft[40][4];

static const shot_t *S;
static const fu_t *U;
static far_t lofts;                      // LOFTEMPS
static int16_t c_x = -1, c_y, c_z;       // кэш клетки voxelCheck
static uint8_t c_part[4], c_unit;
static int8_t c_tl;                      // T_Level клетки (Tile::getTerrainLevel)

static far_t cell_at(uint8_t x, uint8_t y, uint8_t z)
{
	return S->cells + ((uint32_t)((uint16_t)z * S->sy + y) * S->sx + x) * 4;
}

// Tile::getTerrainLevel: меньший T_Level пола и объекта
static int8_t tlevel(const uint8_t *c)
{
	int8_t a = c[0] ? (int8_t)far_byte(FAR(S->page, PF_TLEVEL) + c[0]) : 0;
	if (c[3]) { int8_t b = (int8_t)far_byte(FAR(S->page, PF_TLEVEL) + c[3]); if (b < a) a = b; }
	return a;
}

// Юнит в клетке (у крупного — любая из четырёх)
static uint8_t unit_in(uint8_t x, uint8_t y, uint8_t z)
{
	for (uint8_t i = 0; i < S->n; i++) {
		const fu_t *u = &U[i];
		if (!(u->flags & FU_ALIVE) || u->z != z) continue;
		uint8_t s = (u->flags & FU_BIG) ? 2 : 1;
		if (x >= u->x && x < u->x + s && y >= u->y && y < u->y + s) return i;
	}
	return 0xFF;
}

static uint8_t mask_bit(uint8_t loft, uint8_t row, uint8_t col)
{
	uint16_t w = far_word(lofts + ((uint16_t)loft * 16 + row) * 2);
	return (w >> (15 - col)) & 1;
}

// TileEngine::voxelCheck
static int8_t vcheck(int16_t x, int16_t y, int16_t z, uint8_t exclude)
{
	if (x < 0 || y < 0 || z < 0 || z > 255) return V_OUTOFBOUNDS;
	uint8_t tx = (uint8_t)(x >> 4), ty = (uint8_t)(y >> 4), tz = zlev[z];
	if ((x >> 4) >= S->sx || (y >> 4) >= S->sy || tz >= S->sz) return V_OUTOFBOUNDS;
	if (tx != c_x || ty != c_y || tz != c_z) {
		c_x = tx; c_y = ty; c_z = tz;
		far_read(cell_at(tx, ty, tz), c_part, 4);
		c_tl = tlevel(c_part);
		c_unit = unit_in(tx, ty, tz);
		// голова юнита снизу торчит в клетку без пола
		if (c_unit == 0xFF && tz && (!c_part[0] || (far_byte(FAR(S->page, PF_FLAGS) + c_part[0]) & 1)))
			c_unit = unit_in(tx, ty, tz - 1);
	}
	uint8_t lay = zlay[z], col = (uint8_t)(x & 15), row = (uint8_t)(y & 15);
	for (uint8_t i = 0; i < 4; i++) {
		uint8_t p = c_part[i];
		if (!p) continue;
		uint8_t lf = far_byte(FAR(S->page + 2, TV_LOFT) + (uint16_t)p * 12 + lay);
		if (mask_bit(lf, row, col)) return (int8_t)i;
	}
	uint8_t ui = c_unit;
	if (ui != 0xFF && ui != exclude) {
		const fu_t *u = &U[ui];
		uint8_t pc[4];
		far_read(cell_at(u->x, u->y, u->z), pc, 4);
		int16_t tzb = (int16_t)u->z * 24 - tlevel(pc);   // нижний воксель юнита (без парения)
		if (z > tzb && z <= tzb + u->height) {
			uint8_t part = 0;
			if (u->flags & FU_BIG) {
				static const uint8_t parts[4] = { 1, 0, 3, 2 };
				part = parts[(tx - u->x) + (ty - u->y) * 2];
			}
			if (mask_bit(uloft[ui][part], row, col)) return V_UNIT;
		}
	}
	return V_EMPTY;
}

static int16_t iabs(int16_t v) { return v < 0 ? -v : v; }

// calculateLine в вокселях: шаг по длинной оси, проверка и в промежуточных точках диагоналей;
// вернёт тип первого непустого вокселя, точка — в s->vx..vz
static int8_t trace(shot_t *s, int16_t x0, int16_t y0, int16_t z0, int16_t x1, int16_t y1, int16_t z1)
{
	int16_t t;
	uint8_t sxy = iabs(y1 - y0) > iabs(x1 - x0);
	if (sxy) { t = x0; x0 = y0; y0 = t; t = x1; x1 = y1; y1 = t; }
	uint8_t sxz = iabs(z1 - z0) > iabs(x1 - x0);
	if (sxz) { t = x0; x0 = z0; z0 = t; t = x1; x1 = z1; z1 = t; }
	int16_t dx = iabs(x1 - x0), dy = iabs(y1 - y0), dz = iabs(z1 - z0);
	int16_t dxy = dx / 2, dxz = dx / 2;
	int8_t stx = x0 > x1 ? -1 : 1, sty = y0 > y1 ? -1 : 1, stz = z0 > z1 ? -1 : 1;
	int16_t y = y0, z = z0;
	c_x = -1;
	for (int16_t x = x0;; x += stx) {
		for (uint8_t k = 0; k < 3; k++) {    // точка и промежуточные после шага по y и по z
			if (k == 1) { dxy -= dy; dxz -= dz; if (dxy >= 0) continue; y += sty; dxy += dx; }
			if (k == 2) { if (dxz >= 0) continue; z += stz; dxz += dx; }
			int16_t cx = x, cy = y, cz = z;
			if (sxz) { t = cx; cx = cz; cz = t; }
			if (sxy) { t = cx; cx = cy; cy = t; }
			int8_t r = vcheck(cx, cy, cz, s->shooter);
			if (r != V_EMPTY) { s->vx = cx; s->vy = cy; s->vz = cz; return r; }
		}
		if (x == x1) break;
	}
	s->vx = x1; s->vy = y1; s->vz = z1;
	return V_EMPTY;
}

static uint16_t isqrt(uint32_t v)
{
	uint16_t r = 0;
	for (uint16_t b = 0x8000; b; b >>= 1) {
		uint16_t t = r | b;
		if ((uint32_t)t * t <= v) r = t;
	}
	return r;
}

static const uint8_t dir_xs[8] = { 8, 14, 15, 15, 8, 1, 1, 1 };   // getOriginVoxel dirXshift
static const uint8_t dir_ys[8] = { 1, 1, 8, 15, 15, 15, 8, 1 };

int8_t fire_shot(shot_t *s, const fu_t *u) __banked
{
	rtab_t t;
	r_items_t w;
	res_t lr;
	S = s; U = u;
	s->hit = 0xFF;
	s->res = V_EMPTY;
	if (!res_find(RES_LOFTEMPS, &lr) || !rtab_open(RES_RULE_ITEMS, &t) || s->weapon >= t.n) return V_EMPTY;
	lofts = lr.phys;
	rtab_get(&t, s->weapon, &w);
	for (uint16_t z = 0; z < 256; z++) { zlev[z] = (uint8_t)(z / 24); zlay[z] = (uint8_t)((z % 24) / 2); }
	if (rtab_open(RES_RULE_ARMORS, &t))  // маски юнитов: loftempsSet брони (у крупного — по четверти)
		for (uint8_t i = 0; i < s->n; i++) {
			r_armors_t a;
			int16_t l[4] = { 0, 0, 0, 0 };
			if (u[i].armor < t.n) {
				rtab_get(&t, u[i].armor, &a);
				uint8_t k = a.loftemps_set.n > 4 ? 4 : a.loftemps_set.n;
				if (k) rtab_tail(&t, a.loftemps_set.off, l, (uint16_t)k * 2);
				for (uint8_t j = k; j < 4; j++) l[j] = l[0];
			}
			for (uint8_t j = 0; j < 4; j++) uloft[i][j] = (uint8_t)l[j];
		}
	const fu_t *me = &u[s->shooter];
	uint8_t sz = (me->flags & FU_BIG) ? 2 : 1, c[4];
	// TileEngine::getOriginVoxel
	far_read(cell_at(me->x, me->y, me->z), c, 4);
	int16_t ox = (int16_t)me->x * 16, oy = (int16_t)me->y * 16, oz = (int16_t)me->z * 24 - tlevel(c) + me->height - 4;
	if (oz >= ((int16_t)me->z + 1) * 24) {
		uint8_t a[4] = { 0, 0, 0, 0 };
		if (me->z + 1 < s->sz) far_read(cell_at(me->x, me->y, me->z + 1), a, 4);
		if (!(me->z + 1 < s->sz && (!a[0] || (far_byte(FAR(s->page, PF_FLAGS) + a[0]) & 1)))) {
			while (oz >= ((int16_t)me->z + 1) * 24) oz--;
			oz -= 4;
		}
	}
	ox += dir_xs[s->dir & 7] * sz; oy += dir_ys[s->dir & 7] * sz;
	// цель: юнит — середина его роста, иначе объект, стены, пол (ProjectileFlyBState::init)
	far_read(cell_at(s->tx, s->ty, s->tz), c, 4);
	int16_t tx = (int16_t)s->tx * 16 + 8, ty = (int16_t)s->ty * 16 + 8, tz = (int16_t)s->tz * 24;
	uint8_t tu = unit_in(s->tx, s->ty, s->tz);
	if (tu != 0xFF && tu != s->shooter) {
		tz += -tlevel(c) + u[tu].height / 2;
		if (u[tu].flags & FU_BIG) { tx = (int16_t)u[tu].x * 16 + 16; ty = (int16_t)u[tu].y * 16 + 16; }
	} else if (c[3]) tz += 10;
	else if (c[2]) { ty -= 8; tz += 9; }
	else if (c[1]) { tx -= 8; tz += 9; }
	else tz += 2;
	// Projectile::applyAccuracy: дальность сверх aimRange (и ближе minRange) снижает меткость на
	// dropoff % за клетку; разброс растёт с расстоянием, промах — заметно шире
	int16_t dx = tx - ox, dy = ty - oy, dz = tz - oz;
	uint16_t rd = isqrt((uint32_t)((int32_t)dx * dx + (int32_t)dy * dy));   // в вокселях
	int16_t acc = s->acc, mod = 0;
	if (s->action != BA_THROW && s->action != BA_HIT) {
		if (rd < (uint16_t)w.min_range * 16) mod = (int16_t)((uint16_t)w.dropoff * ((uint16_t)w.min_range * 16 - rd) / 16);
		else if (rd > (uint16_t)w.aim_range * 16) mod = (int16_t)((uint32_t)w.dropoff * (rd - (uint16_t)w.aim_range * 16) / 16);
		acc -= mod;
		if (acc < 0) acc = 0;
	}
	int16_t xd = iabs(dx), yd = iabs(dy), zd = iabs(dz), xys, zs;
	xys = xd / 2 <= yd ? xd / 4 + yd : (xd + yd) / 2;
	zs = xys <= zd ? xys / 2 + zd : xys + zd / 2;
	int16_t dev = (int16_t)rng_range(0, 100) - acc;
	dev += dev >= 0 ? 50 : 10;
	dev = (int16_t)((int32_t)zs * dev / 200);
	if (dev < 1) dev = 1;
	tx += (int16_t)rng_range(0, (uint16_t)dev) - dev / 2;
	ty += (int16_t)rng_range(0, (uint16_t)dev) - dev / 2;
	tz += (int16_t)rng_range(0, (uint16_t)(dev / 2)) / 2 - dev / 8;
	// extendLine: тот же вектор, длинная ось — 2048 вокселей
	dx = tx - ox; dy = ty - oy; dz = tz - oz;
	int16_t m = iabs(dx) > iabs(dy) ? iabs(dx) : iabs(dy);
	if (iabs(dz) > m) m = iabs(dz);
	if (!m) m = 1;
	tx = ox + (int16_t)((int32_t)dx * 2048 / m);
	ty = oy + (int16_t)((int32_t)dy * 2048 / m);
	tz = oz + (int16_t)((int32_t)dz * 2048 / m);
	s->res = trace(s, ox, oy, oz, tx, ty, tz);
	if (s->res == V_UNIT) s->hit = c_unit;
	return s->res;
}

// ---------------------------------------------------------------- урон
#define DT_HE 3
#define DT_IN 2
#define DT_STUN 6
#define DT_SMOKE 9

uint8_t fire_damage(dmg_t *d) __banked
{
	rtab_t t;
	r_armors_t a;
	// TileEngine::hit: мощность ±DAMAGE_RANGE (difficulty.rul: UFO 100 %, TFTD 50 %; взрыв — 50 %)
	uint8_t rng = d->type == DT_HE || res_game() == 2 ? 50 : 100;
	int16_t power = (int16_t)rng_range((uint16_t)((uint32_t)d->power * (100 - rng) / 100), (uint16_t)((uint32_t)d->power * (100 + rng) / 100));
	if (power <= 0) return 0;
	uint8_t type = d->type;
	if (rtab_open(RES_RULE_ARMORS, &t) && d->armor_rule < t.n) {   // Armor::getDamageModifier
		rtab_get(&t, d->armor_rule, &a);
		if (type < a.damage_modifier.n) {
			int16_t m;
			rtab_tail(&t, a.damage_modifier.off + type * 2, &m, 2);
			power = (int16_t)((int32_t)power * m / 100);
		}
	}
	if (type == DT_SMOKE) type = DT_STUN;
	// сторона: относительное направление на точку попадания минус направление юнита
	uint8_t side = 0;                    // SIDE_FRONT, LEFT 1, RIGHT 2, REAR 3, UNDER 4
	if (!d->rx && !d->ry && !d->rz) side = 4;
	else {
		int16_t ax = iabs(d->rx), ay = iabs(d->ry);
		uint8_t rel;
		if (ay > ax * 2) rel = 8 + 4 * (d->ry > 0);
		else if (ax > ay * 2) rel = 10 + 4 * (d->rx < 0);
		else rel = d->rx < 0 ? (d->ry > 0 ? 13 : 15) : (d->ry > 0 ? 11 : 9);
		switch ((uint8_t)(rel - d->dir) & 7) {
		case 0: side = 0; break;
		case 1: side = rng_range(0, 2) < 2 ? 0 : 2; break;
		case 2: side = 2; break;
		case 3: side = rng_range(0, 2) < 2 ? 3 : 2; break;
		case 4: side = 3; break;
		case 5: side = rng_range(0, 2) < 2 ? 3 : 1; break;
		case 6: side = 1; break;
		default: side = rng_range(0, 2) < 2 ? 0 : 1; break;
		}
	}
	power -= d->armor[side];
	if (power <= 0) return 0;
	if (type == DT_STUN) {
		uint16_t s = d->stun + power;
		d->stun = s > 255 ? 255 : (uint8_t)s;
	} else {
		d->hp = power >= d->hp ? 0 : (uint8_t)(d->hp - power);
		if (type != DT_IN) {                 // обычное оружие ещё и оглушает
			uint16_t s = d->stun + rng_range(0, (uint16_t)(power / 4));
			d->stun = s > 255 ? 255 : (uint8_t)s;
		}
		int16_t na = (int16_t)d->armor[side] - power / 10 - 1;   // броня изнашивается
		d->armor[side] = na < 0 ? 0 : (uint8_t)na;
	}
	return power > 255 ? 255 : (uint8_t)power;
}
