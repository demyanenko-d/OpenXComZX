// Видимость в бою (банк 32): кто из пришельцев виден отряду.
// Порт TileEngine::calculateFOV (сектор и дальность), TileEngine::visible и тайловой линии
// взгляда TileEngine::calculateLine (без вокселей) с horizontalBlockage / verticalBlockage /
// blockage для DT_NONE (REF/OpenXcom/src/Battlescape/TileEngine.cpp:228, :393, :1631, :1691,
// :1860, :2225). Юнит оригинал проверяет воксельными лучами (canTargetUnit) — у нас та же тайловая
// линия, что и для тумана войны; тьмы и дыма пока нет (день, 16 §5.1–5.2).
//
// Чтобы линия не разбирала на каждом шаге четыре части клеток по дальней памяти, при загрузке
// карты строится маска обзора — байт на клетку (vm_*): стены W и N, объект и его «большая
// стена», пол. Двери меняют её по клетке (vis_cell).
#include <stdint.h>
#include "far.h"
#include "pages.h"
#include "pathfind.h"
#include "loader.h"
#include "units.h"
#include "vision.h"

#define VM_W   1               // западная стена держит обзор (MCD.Stop_LOS)
#define VM_N   2               // северная стена
#define VM_O   4               // объект
#define VM_F   8               // пол: глухой (есть и не No_Floor) или держит обзор — 256
// старшая тетрада — MCD.Big_Wall объекта (Pathfinding::bigWallTypes: 1 BLOCK, 2 NESW, 3 NWSE,
// 4 WEST, 5 NORTH, 6 EAST, 7 SOUTH, 8 EAST_AND_SOUTH, 9 WEST_AND_NORTH)
#define MAX_VIEW 20            // TileEngine.h MAX_VIEW_DISTANCE

static uint8_t vm_page = PG_NONE, vm_np, vm_cur;
static uint8_t vm_sx, vm_sy, vm_sz;
static uint8_t *const vm = (uint8_t *)0xC000;   // подключённая страница маски

static const int8_t vd_x[8] = { 0, 1, 1, 1, 0, -1, -1, -1 };
static const int8_t vd_y[8] = { -1, -1, 0, 1, 1, 1, 0, -1 };

static uint8_t in_map(int8_t x, int8_t y, int8_t z)
{
	return x >= 0 && y >= 0 && z >= 0 && (uint8_t)x < vm_sx && (uint8_t)y < vm_sy && (uint8_t)z < vm_sz;
}

// Маска клетки (вне поля — 0: клетки нет, оригинал считает её блокировку нулём)
static uint8_t mk(int8_t x, int8_t y, int8_t z)
{
	if (!in_map(x, y, z)) return 0;
	uint16_t i = (uint16_t)(((uint16_t)(uint8_t)z * vm_sy + (uint8_t)y) * vm_sx + (uint8_t)x);
	uint8_t p = (uint8_t)(vm_page + (i >> 14));
	if (p != vm_cur) { pg_map3(p); vm_cur = p; }
	return vm[i & 0x3FFF];
}

// TileEngine::blockage(tile, O_OBJECT, DT_NONE, direction, checkingFromOrigin)
static int16_t obj_blk(uint8_t m, uint8_t dir, uint8_t origin)
{
	if (!(m & VM_O)) return 0;
	uint8_t w = m >> 4;
	if (origin && (w == 2 || w == 3)) return 0;
	switch (dir) {
	case 0: if (w == 4 || w == 6 || w == 7 || w == 8) return 0; break;
	case 1: if (w == 4 || w == 7) return 0; break;
	case 2: if (w == 5 || w == 7 || w == 4 || w == 9) return 0; break;
	case 3: if (w == 5 || w == 4 || w == 9) return 0; break;
	case 4: if (w == 4 || w == 6 || w == 5 || w == 9) return 0; break;
	case 5: if (w == 5 || w == 6) return 0; break;
	case 6: if (w == 5 || w == 7 || w == 6 || w == 8) return 0; break;
	case 7: if (w == 7 || w == 6 || w == 8) return 0; break;
	default: if (w != 0 && w != 1) return 0; break;   // 8 вверх, 9 вниз
	}
	return 255;
}

#define WB(m) (((m) & VM_W) ? 255 : 0)
#define NB(m) (((m) & VM_N) ? 255 : 0)
#define FB(m) (((m) & VM_F) ? 256 : 0)

// TileEngine::horizontalBlockage(start, end, DT_NONE, skipObject): -1 — упёрлись в большую стену
static int16_t hblk(int8_t sx, int8_t sy, int8_t z, int8_t ex, int8_t ey, uint8_t skip)
{
	if (!in_map(sx, sy, z) || !in_map(ex, ey, z)) return 0;
	int8_t dx = ex - sx, dy = ey - sy;
	uint8_t dir;
	for (dir = 0; dir < 8; dir++)
		if (vd_x[dir] == dx && vd_y[dir] == dy) break;
	if (dir == 8) return 0;
	uint8_t s = mk(sx, sy, z), e = mk(ex, ey, z), t;
	int16_t b = 0;
	switch (dir) {
	case 0: b = NB(s); break;
	case 1:                              // двусторонняя проверка наискось, как в оригинале
		t = mk(sx, sy - 1, z);
		b = NB(s) + WB(e) + ((t >> 4) != 2 ? obj_blk(t, 3, 0) : 0);
		if (!b) break;
		t = mk(sx + 1, sy, z);
		b = NB(t) + WB(t) + ((t >> 4) != 2 ? obj_blk(t, 7, 0) : 0);
		break;
	case 2: b = WB(e); break;
	case 3:
		t = mk(sx, sy + 1, z);
		b = NB(t) + WB(e) + ((t >> 4) != 3 ? obj_blk(t, 1, 0) : 0);
		if (!b) break;
		t = mk(sx + 1, sy, z);
		b = WB(t) + NB(e) + ((t >> 4) != 3 ? obj_blk(t, 5, 0) : 0);
		break;
	case 4: b = NB(e); break;
	case 5:
		t = mk(sx, sy + 1, z);
		b = NB(t) + WB(t) + ((t >> 4) != 2 ? obj_blk(t, 7, 0) : 0);
		if (!b) break;
		t = mk(sx - 1, sy, z);
		b = WB(s) + NB(e) + ((t >> 4) != 2 ? obj_blk(t, 3, 0) : 0);
		break;
	case 6: b = WB(s); break;
	default:
		t = mk(sx, sy - 1, z);
		b = NB(s) + WB(t) + ((t >> 4) != 3 ? obj_blk(t, 5, 0) : 0);
		if (!b) break;
		t = mk(sx - 1, sy, z);
		b = WB(s) + NB(t) + ((t >> 4) != 3 ? obj_blk(t, 1, 0) : 0);
		break;
	}
	if (!skip || (s >> 4)) b += obj_blk(s, dir, 0);
	if (b <= 127 && obj_blk(e, (dir + 4) & 7, 1) > 127) return -1;
	return b;
}

// TileEngine::verticalBlockage(start, end, DT_NONE)
static int16_t vblk(int8_t sx, int8_t sy, int8_t sz, int8_t ex, int8_t ey, int8_t ez)
{
	int16_t b = 0;
	if (!in_map(sx, sy, sz) || !in_map(ex, ey, ez) || ez == sz) return 0;
	if (ez < sz) {                       // вниз: пол и объект своей клетки, потом соседней
		uint8_t m = mk(sx, sy, sz);
		b = FB(m) + obj_blk(m, 9, 0);
		if (sx != ex || sy != ey) {
			m = mk(ex, ey, sz);
			b += hblk(sx, sy, sz, ex, ey, 0) + FB(m) + obj_blk(m, 9, 0);
		}
	} else {                             // вверх: пол клетки этажом выше
		uint8_t m = mk(sx, sy, sz + 1);
		b = FB(m) + obj_blk(m, 8, 0);
		if (sx != ex || sy != ey) {
			m = mk(ex, ey, sz + 1);
			b += FB(m) + obj_blk(m, 8, 0);   // horizontalBlockage разных этажей — 0
		}
	}
	return b;
}

static int8_t iabs(int8_t v) { return v < 0 ? -v : v; }

// TileEngine::calculateLine в клетках (doVoxelCheck = false): 1 — линия до цели свободна
static uint8_t los(int8_t x0, int8_t y0, int8_t z0, int8_t x1, int8_t y1, int8_t z1)
{
	int8_t t, lx = x0, ly = y0, lz = z0;
	uint8_t swap_xy = iabs(y1 - y0) > iabs(x1 - x0);
	if (swap_xy) { t = x0; x0 = y0; y0 = t; t = x1; x1 = y1; y1 = t; }
	uint8_t swap_xz = iabs(z1 - z0) > iabs(x1 - x0);
	if (swap_xz) { t = x0; x0 = z0; z0 = t; t = x1; x1 = z1; z1 = t; }
	int8_t ddx = iabs(x1 - x0), ddy = iabs(y1 - y0), ddz = iabs(z1 - z0);
	int8_t dxy = ddx / 2, dxz = ddx / 2;
	int8_t stx = x0 > x1 ? -1 : 1, sty = y0 > y1 ? -1 : 1, stz = z0 > z1 ? -1 : 1;
	int8_t y = y0, z = z0;
	uint8_t steps = 0;
	for (int8_t x = x0;; x += stx) {
		int8_t cx = x, cy = y, cz = z;
		if (swap_xz) { t = cx; cx = cz; cz = t; }
		if (swap_xy) { t = cx; cx = cy; cy = t; }
		int16_t tv = vblk(lx, ly, lz, cx, cy, cz);
		int16_t r = hblk(lx, ly, lz, cx, cy, steps < 2);
		steps++;
		if (r == -1) {
			if (tv <= 127) return 0;     // большая стена
			r = 0;
		}
		if (r + tv > 127) return 0;
		lx = cx; ly = cy; lz = cz;
		if (x == x1) break;
		dxy -= ddy;
		dxz -= ddz;
		if (dxy < 0) { y += sty; dxy += ddx; }
		if (dxz < 0) { z += stz; dxz += ddx; }
	}
	return 1;
}

// Сектор обзора TileEngine::calculateFOV: по направлению d перебираются x = 0..20 и y в
// [-x, x] (прямо) или [0, 20] (наискось), клетка — центр + знаки направления (оси меняются
// местами на севере и юге); дальность — x² + y² <= 400
static const int8_t sg_x[8] = { 1, 1, 1, 1, -1, -1, -1, -1 };
static const int8_t sg_y[8] = { -1, -1, -1, 1, 1, 1, -1, -1 };

static uint8_t in_cone(uint8_t d, int8_t dx, int8_t dy)
{
	int8_t x, y;
	if (d == 0 || d == 4) { y = dx * sg_x[d]; x = dy * sg_y[d]; }
	else { x = dx * sg_x[d]; y = dy * sg_y[d]; }
	if (x < 0 || x > MAX_VIEW) return 0;
	if (d & 1) { if (y < 0 || y > MAX_VIEW) return 0; }
	else if (y < -x || y > x) return 0;
	return (int16_t)x * x + (int16_t)y * y <= MAX_VIEW * MAX_VIEW;
}

// Видит ли v клетку (x, y, z) с юнитом: сектор, дальность и линия взгляда (TileEngine::visible)
static uint8_t sees(const vu_t *v, uint8_t x, uint8_t y, uint8_t z)
{
	int8_t dx = (int8_t)(x - v->x), dy = (int8_t)(y - v->y);
	if (!in_cone(v->dir & 7, dx, dy)) return 0;
	return los((int8_t)v->x, (int8_t)v->y, (int8_t)v->z, (int8_t)x, (int8_t)y, (int8_t)z);
}

uint8_t vis_seen(vu_t *u, uint8_t n, uint8_t nsold, uint8_t *list, uint8_t max) __banked
{
	uint8_t old = pg_win3(), cnt = 0;
	vm_cur = old;
	if (vm_page == PG_NONE) {            // маски нет (не хватило страниц) — видны все
		for (uint8_t i = nsold; i < n; i++)
			if ((u[i].flags & (VU_ALIVE | VU_SEEN)) == VU_ALIVE) { u[i].flags |= VU_SEEN; if (cnt < max) list[cnt++] = i; }
		return cnt;
	}
	for (uint8_t i = nsold; i < n; i++) {
		vu_t *a = &u[i];
		if ((a->flags & (VU_ALIVE | VU_SEEN)) != VU_ALIVE) continue;   // в свой ход отряд не теряет замеченных
		uint8_t nq = (a->flags & VU_BIG) ? 4 : 1, seen = 0;
		for (uint8_t s = 0; s < nsold && !seen; s++) {
			const vu_t *v = &u[s];
			if (!(v->flags & VU_ALIVE)) continue;
			for (uint8_t q = 0; q < nq && !seen; q++)
				seen = sees(v, (uint8_t)(a->x + (q & 1)), (uint8_t)(a->y + (q >> 1)), a->z);
		}
		if (!seen) continue;
		a->flags |= VU_SEEN;
		if (cnt < max) list[cnt++] = i;
	}
	if (vm_cur != old) pg_map3(old);
	return cnt;
}

// Маска клетки по её частям c[4] и их свойствам (pathfind.h PF_FLAGS: 1 нет пола, 2 держит
// обзор; loader.h TL_BIGW — большая стена): fl, bw — по части клетки
static uint8_t cell_mask(const uint8_t *c, const uint8_t *fl, const uint8_t *bw)
{
	uint8_t m = 0;
	if (c[1] && (fl[1] & 2)) m |= VM_W;
	if (c[2] && (fl[2] & 2)) m |= VM_N;
	if (c[3]) { if (fl[3] & 2) m |= VM_O; m |= (uint8_t)(bw[3] << 4); }
	if (c[0] && ((fl[0] & 2) || !(fl[0] & 1))) m |= VM_F;
	return m;
}

void vis_free(void) __banked
{
	if (vm_page != PG_NONE) pg_free(vm_page, vm_np);
	vm_page = PG_NONE;
}

uint8_t vis_build(far_t cells, uint8_t sx, uint8_t sy, uint8_t sz, uint8_t page) __banked
{
	uint8_t fl[256], bw[256], row[64 * 4], out[64];
	if (vm_page != PG_NONE) pg_free(vm_page, vm_np);
	vm_page = PG_NONE;
	uint16_t total = (uint16_t)sx * sy * sz;
	vm_np = (uint8_t)((total + 16383) >> 14);
	vm_page = pg_alloc(vm_np, 1);
	if (vm_page == PG_NONE) return 0;
	vm_sx = sx; vm_sy = sy; vm_sz = sz;
	far_read(FAR(page, PF_FLAGS), fl, 256);
	far_read(FAR(page + 1, TL_BIGW), bw, 256);
	uint16_t i = 0;
	for (uint8_t z = 0; z < sz; z++)
		for (uint8_t y = 0; y < sy; y++, i += sx) {
			far_read(cells + (uint32_t)i * 4, row, (uint16_t)sx * 4);
			for (uint8_t x = 0; x < sx; x++) {
				const uint8_t *c = row + x * 4;
				uint8_t f4[4], b4[4];
				for (uint8_t k = 0; k < 4; k++) { f4[k] = fl[c[k]]; b4[k] = bw[c[k]]; }
				out[x] = cell_mask(c, f4, b4);
			}
			far_write(FAR(vm_page, 0) + i, out, sx);   // far_t + смещение переходит через страницы
		}
	return 1;
}

void vis_cell(far_t cells, uint8_t x, uint8_t y, uint8_t z, uint8_t page) __banked
{
	uint8_t c[4], fl[4], bw[4];
	if (vm_page == PG_NONE || x >= vm_sx || y >= vm_sy || z >= vm_sz) return;
	uint16_t i = (uint16_t)(((uint16_t)z * vm_sy + y) * vm_sx + x);
	far_read(cells + (uint32_t)i * 4, c, 4);
	for (uint8_t k = 0; k < 4; k++) {    // нужны только четыре части клетки
		fl[k] = far_byte(FAR(page, PF_FLAGS) + c[k]);
		bw[k] = far_byte(FAR(page + 1, TL_BIGW) + c[k]);
	}
	uint8_t m = cell_mask(c, fl, bw);
	far_write(FAR(vm_page, 0) + i, &m, 1);
}
