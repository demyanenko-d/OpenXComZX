// Выстрел на экране (банк 32): полёт пули и вспышка попадания.
// Пуля (Projectile, REF/OpenXcom/src/Battlescape/Projectile.cpp:52-99, 421-437) — 35 частиц
// 3x3 из BulletSprites (extraSprites Projectiles; тип пули — bulletSprite · 35) на позициях
// траектории position + 1 - i, у каждой тень на полу (Map.cpp:812-858); за тик 1/60 с пуля
// проходит battleFireSpeed (6) + bulletSpeed обоймы и оружия вокселей; у нас тик — кадр 1/50 с,
// шаг · 6/5. Вспышка попадания (ExplosionBState.cpp:145-160, Explosion.cpp) — 10 кадров
// SMOKE.PCK с hitAnimation обоймы (оружия без обоймы), кадр раз в max(1, 50 - 10 · explosionSpeed)
// мс, со сдвигом (-15, -15) от точки (Map.cpp:1235). Траектория — прямая от точки вылета до
// точки попадания (calculateLine по ней же и шёл, bfire.c). Спрайты рисует вид поверх карты
// (bview.c bv_fx): закрытия стенами и проверки isVoxelVisible пока нет.
// Площадные взрывы (X1.PCK) — когда будут ракеты и гранаты.
#include <stdint.h>
#include <string.h>
#include "far.h"
#include "pages.h"
#include "res.h"
#include "res_ids.h"
#include "rules.h"
#include "music.h"
#include "loader.h"
#include "units.h"
#include "bitems.h"
#include "bview.h"
#include "bfx.h"

extern volatile uint16_t frames;         // crt0.s

#define PART_N   35                      // частиц на пулю (Map::BULLET_SPRITES)
#define HIT_N    10                      // кадров вспышки пули (Explosion::BULLET_FRAMES)
#define FXB_SIZE (2 + 2 * PART_N * TE_SIZE)
#define FXH_SIZE (2 + HIT_N * TE_SIZE)

typedef struct { int16_t bs, anim; uint8_t speed, es; } fxp_t;

// Что летит и как вспыхивает: тип пули — обоймы, иначе оружия; скорость — обеих
// (у оружия без обоймы — только его); вспышка — того, чем стреляют (обойма или оружие)
static void params(const rtab_t *t, uint16_t weapon, uint16_t ammo, fxp_t *p)
{
	r_items_t w, a;
	rtab_get(t, weapon, &w);
	int16_t sp = 6;
	p->bs = -1;
	if (ammo != 0xFFFF && ammo < t->n) {
		rtab_get(t, ammo, &a);
		p->bs = a.bullet_sprite;
		sp += a.bullet_speed;
	} else memcpy(&a, &w, sizeof a);
	if (p->bs == -1) p->bs = w.bullet_sprite;
	sp += w.bullet_speed;
	p->speed = sp < 1 ? 1 : sp > 255 ? 255 : (uint8_t)sp;
	p->anim = a.hit_animation;
	p->es = a.explosion_speed;
}

// Кадр fr набора (SPRSET в слоте кэша) — пиксели подряд в страницу pg с *pos, запись для блита
static void copy_frame(const res_t *r, uint16_t fr, uint8_t pg, uint16_t *pos, uint8_t *te)
{
	uint16_t nfr = far_word(r->phys);
	uint8_t e6[6];
	memset(te, 0, TE_SIZE);
	if (fr >= nfr) return;
	far_read(r->phys + 4 + (uint32_t)fr * 6, e6, 6);
	uint16_t sz = (uint16_t)e6[2] * e6[3];
	if (!sz || *pos + sz > 16384) return;
	far_t fdata = r->phys + 4 + (uint32_t)nfr * 6;
	far_copy(FAR(pg, *pos), fdata + ((uint32_t)(e6[4] | (e6[5] << 8)) << 1), sz);
	te[0] = pg; te[1] = (uint8_t)*pos; te[2] = (uint8_t)(*pos >> 8);
	te[3] = (uint8_t)(e6[2] / 2 - 1); te[4] = e6[3];
	te[5] = e6[0]; te[6] = e6[1]; te[7] = e6[2];
	*pos += (sz + 1) & ~1u;
}

void fx_build(uint8_t page, uint8_t n) __banked
{
	far_t t3 = FAR(page + 2, 0);
	uint8_t pg, nb = 0, nh = 0;
	uint16_t pos = 0;
	int16_t key;
	rtab_t t;
	far_read(t3 + TV_FXPG, &pg, 1);
	key = -1;                                    // слоты пусты
	for (uint8_t k = 0; k < FXB_MAX; k++) far_write(t3 + TV_FXB + k * FXB_SIZE, &key, 2);
	for (uint8_t k = 0; k < FXH_MAX; k++) far_write(t3 + TV_FXH + k * FXH_SIZE, &key, 2);
	if (pg == PG_NONE || !rtab_open(RES_RULE_ITEMS, &t)) return;
	if (n > MAX_UNITS) n = MAX_UNITS;
	int16_t bkeys[FXB_MAX], hkeys[FXH_MAX];
	for (uint8_t u = 0; u < n; u++)              // что стреляет у отряда: типы пуль и вспышек
		for (uint8_t side = 0; side < 2; side++) {
			bitem_t b;
			fxp_t p;
			if (!bi_hand(page, u, side == 0, &b) || b.item >= t.n) continue;
			params(&t, b.item, b.ammo, &p);
			uint8_t k;
			if (p.bs >= 0) {
				for (k = 0; k < nb && bkeys[k] != p.bs; k++) ;
				if (k == nb && nb < FXB_MAX) bkeys[nb++] = p.bs;
			}
			if (p.anim >= 0) {
				for (k = 0; k < nh && hkeys[k] != p.anim; k++) ;
				if (k == nh && nh < FXH_MAX) hkeys[nh++] = p.anim;
			}
		}
	res_t r;
	uint8_t te[TE_SIZE];
	if (nb && res_find(RES_BULLETS, &r)) {      // частицы, за ними тени (BULLETS: 385 + 385)
		uint16_t half = far_word(r.phys) / 2;
		for (uint8_t k = 0; k < nb; k++) {
			far_t o = t3 + TV_FXB + k * FXB_SIZE;
			far_write(o, &bkeys[k], 2);
			for (uint8_t i = 0; i < 2 * PART_N; i++) {
				uint16_t fr = (uint16_t)bkeys[k] * PART_N + (i % PART_N) + (i >= PART_N ? half : 0);
				copy_frame(&r, fr, pg, &pos, te);
				far_write(o + 2 + i * TE_SIZE, te, TE_SIZE);
			}
		}
	}
	if (nh && res_find(RES_SMOKE_PCK, &r))
		for (uint8_t k = 0; k < nh; k++) {
			far_t o = t3 + TV_FXH + k * FXH_SIZE;
			far_write(o, &hkeys[k], 2);
			for (uint8_t i = 0; i < HIT_N; i++) {
				copy_frame(&r, (uint16_t)hkeys[k] + i, pg, &pos, te);
				far_write(o + 2 + i * TE_SIZE, te, TE_SIZE);
			}
		}
}

// Слот кадров с ключом key в таблице (TV_FXB / TV_FXH); #FF — нет
static uint8_t slot_of(far_t base, uint16_t size, uint8_t n, int16_t key)
{
	for (uint8_t k = 0; k < n; k++) {
		int16_t v;
		far_read(base + k * size, &v, 2);
		if (v == key) return k;
	}
	return 0xFF;
}

static void wait_frame(void)
{
	uint16_t f = frames;
	mus_fill();
	while (frames == f) __asm__("halt");
}

// Прямоугольник спрайтов (мировые): накопление и объединение с прошлым
typedef struct { int16_t lo, hi, top, bot; } rect_t;
static void rect_add(rect_t *r, int16_t x, int16_t y, int16_t w, int16_t h)
{
	if (x < r->lo) r->lo = x;
	if (y < r->top) r->top = y;
	if (x + w - 1 > r->hi) r->hi = x + w - 1;
	if (y + h - 1 > r->bot) r->bot = y + h - 1;
}

// Записи спрайтов готовы (n) — вид перерисовывает их прямоугольник вместе с прошлым
static void show(far_t t3, uint8_t n, rect_t *cur, rect_t *prev)
{
	rect_t u = *cur;
	if (prev->lo <= prev->hi) {
		if (prev->lo < u.lo) u.lo = prev->lo;
		if (prev->hi > u.hi) u.hi = prev->hi;
		if (prev->top < u.top) u.top = prev->top;
		if (prev->bot > u.bot) u.bot = prev->bot;
	}
	far_write(t3 + TV_FX, &n, 1);
	if (u.lo <= u.hi) bv_fx(n, u.lo, u.hi, u.top, u.bot);
	*prev = *cur;
}

static void put(far_t t3, uint8_t i, int16_t x, int16_t y, const uint8_t *te)
{
	uint8_t e[4 + TE_SIZE];
	e[0] = (uint8_t)x; e[1] = (uint8_t)(x >> 8); e[2] = (uint8_t)y; e[3] = (uint8_t)(y >> 8);
	memcpy(e + 4, te, TE_SIZE);
	far_write(t3 + TV_FX + 2 + (uint16_t)i * sizeof e, e, sizeof e);
}

void fx_shot(const fxshot_t *q) __banked
{
	far_t t3 = FAR(q->page + 2, 0);
	rtab_t t;
	fxp_t p;
	if (!rtab_open(RES_RULE_ITEMS, &t) || q->weapon >= t.n) return;
	params(&t, q->weapon, q->ammo, &p);
	rect_t prev, cur;
	prev.lo = 1; prev.hi = 0;
	uint8_t te[TE_SIZE];
	uint8_t bk = p.bs >= 0 ? slot_of(t3 + TV_FXB, FXB_SIZE, FXB_MAX, p.bs) : 0xFF;
	if (bk != 0xFF) {
		int16_t dx = q->ex - q->ox, dy = q->ey - q->oy, dz = q->ez - q->oz;
		int16_t ax = dx < 0 ? -dx : dx, ay = dy < 0 ? -dy : dy, az = dz < 0 ? -dz : dz;
		int16_t len = ax > ay ? ax : ay;
		if (az > len) len = az;
		if (!len) len = 1;
		uint8_t rev = dx + dy >= 0;              // Projectile::isReversed: частицы — с дальней
		far_t frm = t3 + TV_FXB + bk * FXB_SIZE + 2;
		uint16_t pos5 = 0;
		for (;;) {
			pos5 += (uint16_t)p.speed * 6;
			int16_t pos = (int16_t)(pos5 / 5);
			uint8_t done = pos >= len;
			if (done) pos = len;
			cur.lo = cur.top = 32767; cur.hi = cur.bot = -32768;
			uint8_t n = 0;
			for (uint8_t k = 0; k < PART_N; k++) {
				uint8_t i = rev ? (uint8_t)(PART_N - 1 - k) : k;
				int16_t ps = pos + 1 - i;
				if (ps < 0) ps = 0;
				if (ps > len) ps = len;
				int16_t vx = q->ox + (int16_t)((int32_t)dx * ps / len);
				int16_t vy = q->oy + (int16_t)((int32_t)dy * ps / len);
				int16_t vz = q->oz + (int16_t)((int32_t)dz * ps / len);
				int16_t sx = vx - vy + 16 - 1, sxy = (vx + vy + 40) >> 1;
				int16_t fz = vz < 0 ? 0 : vz / 24 * 24;   // тень — на пол этажа (castedShade)
				far_read(frm + (PART_N + i) * TE_SIZE, te, TE_SIZE);
				if (te[7]) { put(t3, n++, sx, sxy - fz - 1, te); rect_add(&cur, sx, sxy - fz - 1, 4, 3); }
				far_read(frm + i * TE_SIZE, te, TE_SIZE);
				if (te[7]) { put(t3, n++, sx, sxy - vz - 1, te); rect_add(&cur, sx, sxy - vz - 1, 4, 3); }
			}
			show(t3, n, &cur, &prev);
			wait_frame();
			if (done) break;
		}
		cur.lo = 1; cur.hi = 0;
		show(t3, 0, &cur, &prev);                // пуля долетела
	}
	uint8_t hk = p.anim >= 0 ? slot_of(t3 + TV_FXH, FXH_SIZE, FXH_MAX, p.anim) : 0xFF;
	if (hk == 0xFF) return;
	int16_t sx = q->ex - q->ey + 16 - 15, sy = ((q->ex + q->ey + 40) >> 1) - q->ez - 15;
	int16_t ms = 50 - 10 * (int16_t)p.es;
	uint8_t wait = ms <= 20 ? 1 : (uint8_t)((ms + 19) / 20);
	for (uint8_t f = 0; f < HIT_N; f++) {
		far_read(t3 + TV_FXH + hk * FXH_SIZE + 2 + f * TE_SIZE, te, TE_SIZE);
		cur.lo = sx; cur.top = sy; cur.hi = sx + 31; cur.bot = sy + 39;
		uint8_t n = 0;
		if (te[7]) put(t3, n++, sx, sy, te);
		show(t3, n, &cur, &prev);
		for (uint8_t w = 0; w < wait; w++) wait_frame();
	}
	cur.lo = 1; cur.hi = 0;
	show(t3, 0, &cur, &prev);
}
