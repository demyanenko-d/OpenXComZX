// Взрывы (банк 39): гранаты в конце хода (BattlescapeGame::endTurn, REF/OpenXcom/src/Battlescape/
// BattlescapeGame.cpp:374-416), площадной взрыв на экране (ExplosionBState::init, ExplosionBState.cpp,
// Explosion.cpp, Map.cpp:1207-1229) и урон по площади (TileEngine::explode, TileEngine.cpp).
// Экран: power/5 взрывов кадрами X1.PCK (8 кадров с hitAnimation предмета) в точках со сдвигом до
// ±power/2 вокселей, каждый следующий — с задержкой на кадр через power/25 штук; шаг 50 мс
// (DEFAULT_ANIM_SPEED / 2); перед ними кадр вспышки — палитрой (gfx_pal_flash). Кадры — в задний
// буфер глобуса экрана (map.h BAT_SCRATCH_PAGE). Урон: лучи через 3° по горизонтали (вертикальные лучи оригинала при
// battleExplosionHeight 0 гаснут на соседнем этаже), мощность −10 за клетку и −2 · блокировку
// (heBlock стен и объектов, horizontalBlockage); клетку берёт первый дошедший луч: юниту — урон
// (вблизи и сверху — снизу, иначе — со стороны центра), предметы с бронёй меньше мощности — в пыль.
// Чего нет: разрушения местности (setExplosive / detonate), дыма и огня, юнита из клетки снизу,
// глубины TFTD (подводные кадры), камеры по этажам выше.
#include <stdint.h>
#include <string.h>
#include <stddef.h>
#include "far.h"
#include "pages.h"
#include "memmap.h"
#include "res.h"
#include "res_ids.h"
#include "rules.h"
#include "game.h"
#include "gfx.h"
#include "music.h"
#include "pathfind.h"
#include "loader.h"
#include "units.h"
#include "bitems.h"
#include "bview.h"
#include "bfire.h"
#include "bfx.h"
#include "bact.h"
#include "bexpl.h"

extern volatile uint16_t frames;         // crt0.s

#define DT_IN    2
#define DT_HE    3
#define DT_STUN  6
#define BT_MELEE 3
#define EXP_N    8                       // кадров большого взрыва (Explosion::EXPLODE_FRAMES)
#define EXP_MAX  48                      // взрывов на экране (power/5)
#define R_MAX    11                      // радиус (getExplosionRadius ограничен 11)
#define SIDE     (2 * R_MAX + 1)
#define GI_MAX   48                      // предметов на земле в радиусе

// Память банка (build.ps1 ram = #B000)
static uint8_t __at(0xB000) seen[SIDE * SIDE];       // клетка уже взята лучом
static uint8_t __at(0xB220) ux[MAX_BUNITS];          // юниты: клетка, размер, жив
static uint8_t __at(0xB248) uy[MAX_BUNITS];
static uint8_t __at(0xB270) uz[MAX_BUNITS];
static uint8_t __at(0xB298) us[MAX_BUNITS];
static uint8_t __at(0xB2C0) gi_x[GI_MAX];            // предметы на земле рядом: клетка, запись
static uint8_t __at(0xB2F0) gi_y[GI_MAX];
static uint8_t __at(0xB320) gi_i[GI_MAX];
static uint8_t __at(0xB350) gi_n;
static uint8_t __at(0xB351) ex_fr[EXP_MAX];          // взрывы на экране: кадр (0..7), задержка
static uint8_t __at(0xB381) ex_dl[EXP_MAX];
static int16_t __at(0xB3B2) ex_sx[EXP_MAX];          // мировые экранные x, y
static int16_t __at(0xB412) ex_sy[EXP_MAX];
static uint8_t __at(0xB472) ex_te[EXP_N][TE_SIZE];   // кадры X1.PCK во временных страницах
static const exreq_t * __at(0xB4B2) Q;
static uint8_t __at(0xB4B4) dead[MAX_BUNITS];        // выбыл от этого взрыва
static uint8_t __at(0xB4DC) ex_np;                  // страница кадров (от BAT_SCRATCH_PAGE)

// sin(3° · k), Q8
static const int16_t sin8[120] = {
	0, 13, 27, 40, 53, 66, 79, 92, 104, 116, 128, 139, 150, 161, 171,
	181, 190, 199, 207, 215, 222, 228, 234, 239, 243, 247, 250, 253, 255, 256,
	256, 256, 255, 253, 250, 247, 243, 239, 234, 228, 222, 215, 207, 199, 190,
	181, 171, 161, 150, 139, 128, 116, 104, 92, 79, 66, 53, 40, 27, 13,
	0, -13, -27, -40, -53, -66, -79, -92, -104, -116, -128, -139, -150, -161, -171,
	-181, -190, -199, -207, -215, -222, -228, -234, -239, -243, -247, -250, -253, -255, -256,
	-256, -256, -255, -253, -250, -247, -243, -239, -234, -228, -222, -215, -207, -199, -190,
	-181, -171, -161, -150, -139, -128, -116, -104, -92, -79, -66, -53, -40, -27, -13,
};

static void u_get(uint8_t i, unit_t *u) { far_read(UNITS_FAR + (uint16_t)i * sizeof *u, u, sizeof *u); }
static void u_set(uint8_t i, const unit_t *u) { far_write(UNITS_FAR + (uint16_t)i * sizeof *u, u, sizeof *u); }

static far_t bi_at(uint16_t i) { return FAR(Q->page + 1, TL_BITEMS) + i * sizeof(bitem_t); }

static void wait_frame(void)
{
	uint16_t f = frames;
	mus_fill();
	while (frames == f) __asm__("halt");
}

// ---------------------------------------------------------------- экран
// Кадр fr набора — в страницы BAT_SCRATCH_PAGE.. с *pos (кадр целиком в одной странице: пул в бою
// занят весь, а задний буфер глобуса в экране боем не используется), запись для блита
static void copy_frame(const res_t *r, uint16_t fr, uint16_t *pos, uint8_t *te)
{
	uint16_t nfr = far_word(r->phys);
	uint8_t e6[6];
	memset(te, 0, TE_SIZE);
	if (fr >= nfr) return;
	far_read(r->phys + 4 + (uint32_t)fr * 6, e6, 6);
	uint16_t sz = (uint16_t)e6[2] * e6[3];
	if (!sz || sz > 16384) return;
	if (*pos + sz > 16384) {
		if (ex_np + 1 >= BAT_SCRATCH_N) return;
		ex_np++;
		*pos = 0;
	}
	uint8_t pg = BAT_SCRATCH_PAGE + ex_np;
	far_copy(FAR(pg, *pos), r->phys + 4 + (uint32_t)nfr * 6 + ((uint32_t)(e6[4] | (e6[5] << 8)) << 1), sz);
	te[0] = pg; te[1] = (uint8_t)*pos; te[2] = (uint8_t)(*pos >> 8);
	te[3] = (uint8_t)(e6[2] / 2 - 1); te[4] = e6[3];
	te[5] = e6[0]; te[6] = e6[1]; te[7] = e6[2];
	*pos += (sz + 1) & ~1u;
}

// Взрывы на экране: кадры с start, n штук в точке (x, y, z) со сдвигом по мощности
static void show_blast(int16_t x, int16_t y, int16_t z, uint16_t power, int16_t start)
{
	res_t r;
	if (!res_find(RES_X1_PCK, &r)) return;
	uint16_t pos = 0;
	ex_np = 0;
	for (uint8_t f = 0; f < EXP_N; f++) copy_frame(&r, (uint16_t)(start + f), &pos, ex_te[f]);
	uint8_t n = (uint8_t)(power / 5 > EXP_MAX ? EXP_MAX : power / 5), cnt = (uint8_t)(power / 25), dl = 0;
	if (!n) n = 1;
	if (!cnt) cnt = 1;
	int16_t lo = 32767, hi = -32768, top = 32767, bot = -32768;
	for (uint8_t i = 0; i < n; i++) {
		int16_t px = x + (int16_t)rng_range(0, power) - (int16_t)(power / 2);
		int16_t py = y + (int16_t)rng_range(0, power) - (int16_t)(power / 2);
		ex_sx[i] = px - py + 16 - 64;        // Map: кадр 128x64 по центру точки
		ex_sy[i] = ((px + py + 40) >> 1) - z - 32;
		ex_fr[i] = 0; ex_dl[i] = dl;
		if (i > 0 && i % cnt == 0) dl++;
		if (ex_sx[i] < lo) lo = ex_sx[i];
		if (ex_sx[i] + 127 > hi) hi = ex_sx[i] + 127;
		if (ex_sy[i] < top) top = ex_sy[i];
		if (ex_sy[i] + 63 > bot) bot = ex_sy[i] + 63;
	}
	far_t t3 = FAR(Q->page + 2, 0);
	gfx_pal_flash(1);                        // setBlastFlash: кадр вспышки
	wait_frame(); wait_frame();
	gfx_pal_flash(0);
	for (uint8_t step = 0;; step++) {
		uint8_t m = 0;
		for (uint8_t i = 0; i < n; i++) {
			if (ex_dl[i] || ex_fr[i] >= EXP_N || !ex_te[ex_fr[i]][7]) continue;
			uint8_t e[4 + TE_SIZE];
			e[0] = (uint8_t)ex_sx[i]; e[1] = (uint8_t)(ex_sx[i] >> 8);
			e[2] = (uint8_t)ex_sy[i]; e[3] = (uint8_t)(ex_sy[i] >> 8);
			memcpy(e + 4, ex_te[ex_fr[i]], TE_SIZE);
			far_write(t3 + TV_FX + 2 + (uint16_t)m * sizeof e, e, sizeof e);
			m++;
		}
		far_write(t3 + TV_FX, &m, 1);
		bv_fx(m, lo, hi, top, bot);
		wait_frame(); wait_frame();
		if (step & 1) wait_frame();          // 50 мс — 2.5 кадра
		uint8_t live = 0;
		for (uint8_t i = 0; i < n; i++) {    // Explosion::animate
			if (ex_dl[i]) { ex_dl[i]--; live = 1; continue; }
			if (ex_fr[i] < EXP_N) { ex_fr[i]++; if (ex_fr[i] < EXP_N) live = 1; }
		}
		if (!live) break;
	}
	far_fill(t3 + TV_FX, 0, 1);
	bv_fx(0, lo, hi, top, bot);
}

// ---------------------------------------------------------------- урон
static far_t cell_at(uint8_t x, uint8_t y, uint8_t z)
{
	return Q->cells + ((uint32_t)((uint16_t)z * Q->sy + y) * Q->sx + x) * 4;
}

static uint8_t cell(int16_t x, int16_t y, uint8_t z, uint8_t *c)
{
	if (x < 0 || y < 0 || x >= Q->sx || y >= Q->sy) { c[0] = c[1] = c[2] = c[3] = 0; return 0; }
	far_read(cell_at((uint8_t)x, (uint8_t)y, z), c, 4);
	return 1;
}

static uint8_t bigw(uint8_t obj) { return obj ? far_byte(FAR(Q->page + 1, TL_BIGW) + obj) : 0; }

// TileEngine::blockage для HE: heBlock части; у объекта с направлением — большие стены, что
// не стоят на пути в эту сторону, не мешают (и диагональные — из клетки взрыва)
static const uint16_t excl[8] = { 0x1D0, 0x090, 0x2B0, 0x230, 0x270, 0x060, 0x1E0, 0x1C0 };
static uint8_t blk(const uint8_t *c, uint8_t slot, int8_t dir, uint8_t orig)
{
	uint8_t p = c[slot];
	if (!p) return 0;
	if (dir >= 0) {
		uint8_t w = bigw(c[3]);
		if (orig && (w == 2 || w == 3)) return 0;
		if (w < 10 && ((excl[dir] >> w) & 1)) return 0;
	}
	return far_byte(FAR(Q->page + 2, TV_PROPS) + (uint16_t)p * TV_PSIZE + 1);
}

// TileEngine::horizontalBlockage (тип урона не DT_NONE): стены между клетками, на диагонали —
// половины обеих обходных, объекты соседей; объект своей клетки (если не skip) и большая стена
// клетки назначения
static int16_t hblock(int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint8_t z, uint8_t skip)
{
	static const int8_t dirs[9] = { 7, 0, 1, 6, -1, 2, 5, 4, 3 };   // (dx + 1) + (dy + 1) * 3
	int8_t d = dirs[(x1 - x0 + 1) + (y1 - y0 + 1) * 3];
	if (d < 0) return 0;
	uint8_t s[4], e[4], a[4], b[4];
	cell(x0, y0, z, s);
	cell(x1, y1, z, e);
	int16_t r = 0;
	switch (d) {
	case 0: r = blk(s, 2, -1, 0); break;
	case 2: r = blk(e, 1, -1, 0); break;
	case 4: r = blk(e, 2, -1, 0); break;
	case 6: r = blk(s, 1, -1, 0); break;
	case 1:                                  // NE: a — восток, b — север
		cell(x0 + 1, y0, z, a); cell(x0, y0 - 1, z, b);
		r = (blk(s, 2, -1, 0) + blk(e, 1, -1, 0)) / 2 + (blk(a, 1, -1, 0) + blk(a, 2, -1, 0)) / 2
			+ (blk(b, 3, 4, 0) + blk(a, 3, 6, 0)) / 2;
		break;
	case 3:                                  // SE: a — восток, b — юг
		cell(x0 + 1, y0, z, a); cell(x0, y0 + 1, z, b);
		r = (blk(e, 1, -1, 0) + blk(e, 2, -1, 0)) / 2 + (blk(a, 1, -1, 0) + blk(b, 2, -1, 0)) / 2
			+ (blk(b, 3, 0, 0) + blk(a, 3, 6, 0)) / 2;
		break;
	case 5:                                  // SW: a — запад, b — юг
		cell(x0 - 1, y0, z, a); cell(x0, y0 + 1, z, b);
		r = (blk(e, 2, -1, 0) + blk(s, 1, -1, 0)) / 2 + (blk(b, 1, -1, 0) + blk(b, 2, -1, 0)) / 2
			+ (blk(b, 3, 0, 0) + blk(a, 3, 2, 0)) / 2;
		break;
	default:                                 // NW: a — запад, b — север
		cell(x0 - 1, y0, z, a); cell(x0, y0 - 1, z, b);
		r = (blk(s, 1, -1, 0) + blk(s, 2, -1, 0)) / 2 + (blk(b, 1, -1, 0) + blk(a, 2, -1, 0)) / 2
			+ (blk(b, 3, 4, 0) + blk(a, 3, 2, 0)) / 2;
		break;
	}
	if (!skip) r += blk(s, 3, d, 0);
	if (bigw(e[3])) r += blk(e, 3, (int8_t)((d + 4) & 7), 1);
	return r;
}

// Клетка (x, y) взята лучом мощностью p: юниту в ней — урон, предметы слабее — в пыль
static void hit_cell(int16_t x, int16_t y, uint8_t z, int16_t p, uint8_t type, int16_t cx, int16_t cy)
{
	for (uint8_t i = 0; i < Q->n; i++) {
		if (!us[i] || uz[i] != z) continue;
		uint8_t sz = us[i] & 0x7F;
		if (x < ux[i] || y < uy[i] || x >= ux[i] + sz || y >= uy[i] + sz) continue;
		unit_t v;
		dmg_t d;
		u_get(i, &v);
		d.power = (uint16_t)p; d.type = type;
		int16_t dx = x - cx, dy = y - cy;
		if (type == DT_HE ? (dx > -2 && dx < 2 && dy > -2 && dy < 2) : (dx * dx + dy * dy < 4))
			d.rx = d.ry = d.rz = 0;          // в центре взрыва — снизу
		else { d.rx = -dx; d.ry = -dy; d.rz = 5; }
		d.dir = v.dir; d.height = v.kneel ? v.h_kneel : v.h_stand; d.armor_rule = v.armor_rule;
		memcpy(d.armor, v.armor, 5);
		d.hp = v.hp; d.stun = v.stun;
		fire_damage(&d);
		memcpy(v.armor, d.armor, 5);
		v.hp = d.hp; v.stun = d.stun;
		if (!v.hp || v.stun >= v.hp) { v.alive = 0; us[i] = 0; dead[i] = 1; }
		u_set(i, &v);
	}
	if (type != DT_HE) return;
	rtab_t t;
	if (!rtab_open(RES_RULE_ITEMS, &t)) return;
	for (uint8_t k = 0; k < gi_n; k++) {
		if (gi_x[k] != x || gi_y[k] != y || gi_i[k] == 0xFF) continue;
		bitem_t b;
		far_read(bi_at(gi_i[k]), &b, sizeof b);
		if (b.owner != BI_GROUND || b.item >= t.n) continue;
		if (p > (int16_t)(uint8_t)rtab_word(&t, b.item, offsetof(r_items_t, armor))) {
			far_fill(bi_at(gi_i[k]), BI_NONE, 1);
			gi_i[k] = 0xFF;
		}
	}
}

static void explode(int16_t vx, int16_t vy, int16_t vz, int16_t power, uint8_t type, uint8_t radius)
{
	if (type == DT_IN) power /= 2;
	int16_t tx = vx >> 4, ty = vy >> 4;
	uint8_t tz = (uint8_t)(vz / 24);
	memset(seen, 0, sizeof seen);
	uint8_t c[4];
	cell(tx, ty, tz, c);
	uint8_t diag = bigw(c[3]);               // диагональная большая стена в клетке взрыва
	if (diag != 2 && diag != 3) diag = 0;
	int8_t side = 0;
	if (diag == 3) side = (vx % 16 - vy % 16) > 0 ? 1 : -1;
	if (diag == 2) side = (vx % 16 + vy % 16 - 15) > 0 ? 1 : -1;
	for (uint8_t k = 0; k < 120; k++) {      // te = 3° · k
		int16_t sn = sin8[k], cs = sin8[k < 90 ? k + 30 : k - 90];
		int16_t x = tx, y = ty, ox, oy, p = power;
		uint8_t l = 0;
		while (p > 0 && l <= radius) {
			uint16_t si = (uint16_t)(x - tx + R_MAX) + (uint16_t)(y - ty + R_MAX) * SIDE;
			if (!seen[si]) { seen[si] = 1; hit_cell(x, y, tz, p, type, tx, ty); }
			l++;
			ox = x; oy = y;
			x = ((tx << 8) + 128 + l * sn) >> 8;
			y = ((ty << 8) + 128 + l * cs) >> 8;
			if (x < 0 || y < 0 || x >= Q->sx || y >= Q->sy) break;
			p -= 10;
			uint8_t skip = 0;
			if (l == 1) {                    // первый шаг: объект клетки взрыва не мешает (/Volutar)
				uint16_t te = (uint16_t)k * 3;
				skip = !diag;
				if (diag == 2 && ((side < 0 && te >= 135 && te < 315) || (side > 0 && (te < 135 || te > 315)))) skip = 1;
				if (diag == 3 && ((side > 0 && te >= 45 && te < 225) || (side < 0 && (te < 45 || te > 225)))) skip = 1;
			}
			p -= hblock(ox, oy, x, y, tz, skip) * 2;
		}
	}
}

// Площадной взрыв предмета item в точке (вокселях): экран, затем урон; выбывшие — со сцены
static void blast(int16_t vx, int16_t vy, int16_t vz, uint16_t item)
{
	rtab_t t;
	r_items_t w;
	if (!rtab_open(RES_RULE_ITEMS, &t) || item >= t.n) return;
	rtab_get(&t, item, &w);
	int16_t rad = w.blast_radius;            // RuleItem::getExplosionRadius
	if (rad < 0) {
		rad = w.damage_type == DT_IN ? w.power / 30 + 1
			: (w.damage_type == DT_HE || w.damage_type == DT_STUN || w.damage_type == 9) ? w.power / 20 : 0;
		if (rad > R_MAX) rad = R_MAX;
	}
	if (rad > R_MAX) rad = R_MAX;
	if (w.battle_type == BT_MELEE || !rad || !w.power) return;
	uint8_t tz = (uint8_t)(vz / 24);
	if (tz != bv_level()) bv_set_level(tz, 0);
	bv_center((uint8_t)(vx >> 4), (uint8_t)(vy >> 4));   // centerOnPosition
	bv_draw();
	show_blast(vx, vy, vz, w.power, w.hit_animation < 0 ? 0 : w.hit_animation);
	// юниты и предметы на земле рядом — в память банка
	unit_t v;
	for (uint8_t i = 0; i < Q->n; i++) {
		u_get(i, &v);
		ux[i] = v.x; uy[i] = v.y; uz[i] = v.z; dead[i] = 0;
		us[i] = v.alive ? v.size : 0;
	}
	gi_n = 0;
	for (uint16_t i = 0; i < BI_MAX && gi_n < GI_MAX; i++) {
		bitem_t b;
		far_read(bi_at(i), &b, sizeof b);
		if (b.owner != BI_GROUND || b.z != tz) continue;
		int16_t dx = (int16_t)b.x - (vx >> 4), dy = (int16_t)b.y - (vy >> 4);
		if (dx < -R_MAX || dx > R_MAX || dy < -R_MAX || dy > R_MAX) continue;
		gi_x[gi_n] = b.x; gi_y[gi_n] = b.y; gi_i[gi_n] = (uint8_t)i; gi_n++;
	}
	explode(vx, vy, vz, (int16_t)w.power, w.damage_type, (uint8_t)rad);
	fl_rebuild(Q->page);                     // сгоревшее — с пола
	bv_sync(Q->n);
	for (uint8_t k = 0; k < gi_n; k++)
		if (gi_i[k] == 0xFF) bv_cell(gi_x[k], gi_y[k], tz);
	for (uint8_t i = 0; i < Q->n; i++)       // checkForCasualties
		if (dead[i]) ba_kill(Q->page, i, Q->n);
}

uint8_t ex_turn(const exreq_t *q) __banked
{
	rtab_t t;
	uint8_t any = 0;
	Q = q;
	if (!rtab_open(RES_RULE_ITEMS, &t)) return 0;
	for (uint16_t i = 0; i < BI_MAX; i++) {  // горячие гранаты на земле
		bitem_t b;
		far_read(bi_at(i), &b, sizeof b);
		if (b.owner != BI_GROUND || b.rounds != 1 || b.item >= t.n) continue;
		if ((uint8_t)rtab_word(&t, b.item, offsetof(r_items_t, battle_type)) != BT_GRENADE) continue;
		far_fill(bi_at(i), BI_NONE, 1);      // removeItem
		fl_rebuild(q->page);
		bv_sync(q->n);
		bv_cell(b.x, b.y, b.z);
		uint8_t c[4];
		far_read(cell_at(b.x, b.y, b.z), c, 4);
		int8_t tl = c[0] ? (int8_t)far_byte(FAR(q->page, PF_TLEVEL) + c[0]) : 0;
		if (c[3]) { int8_t o = (int8_t)far_byte(FAR(q->page, PF_TLEVEL) + c[3]); if (o < tl) tl = o; }
		blast((int16_t)b.x * 16 + 8, (int16_t)b.y * 16 + 8, (int16_t)b.z * 24 - tl, b.item);
		any = 1;
	}
	for (uint16_t i = 0; i < BI_MAX; i++) {  // таймеры прочих — на ход меньше
		bitem_t b;
		far_read(bi_at(i), &b, sizeof b);
		if (b.owner == BI_NONE || b.rounds < 2 || b.item >= t.n) continue;
		uint8_t bt = (uint8_t)rtab_word(&t, b.item, offsetof(r_items_t, battle_type));
		if (bt != BT_GRENADE && bt != BT_PROXIMITYGRENADE) continue;
		b.rounds--;
		far_write(bi_at(i), &b, sizeof b);
	}
	return any;
}
