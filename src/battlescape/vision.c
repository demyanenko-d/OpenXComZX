// Видимость в бою (банк 40, страница #0A): кто из пришельцев виден отряду и туман войны.
// Порт TileEngine::calculateFOV (сектор и дальность), TileEngine::visible и тайловой линии
// взгляда TileEngine::calculateLine (без вокселей) с horizontalBlockage / verticalBlockage /
// blockage для DT_NONE (REF/OpenXcom/src/Battlescape/TileEngine.cpp:228, :393, :1631, :1691,
// :1860, :2225). Юнит оригинал проверяет воксельными лучами (canTargetUnit) — у нас та же тайловая
// линия, что и для тумана войны; тьмы и дыма пока нет (день, 16 §5.1–5.2).
//
// Чтобы линия не разбирала на каждом шаге четыре части клеток по дальней памяти, при загрузке
// карты строится маска обзора — байт на клетку (vm_*): стены W и N, объект и его «большая
// стена», пол. Двери меняют её по клетке (vis_cell).
//
// Туман войны (Tile::_discovered, 16 §5.2): байт на клетку в своих страницах пула (fog_pg), биты
// FOG_W / FOG_N / FOG_C — западная стена, северная, пол и содержимое. Открывает их calculateFOV
// бойца: все клетки линий ко всем клеткам сектора (все этажи) до преграды; у открытой клетки
// восточный сосед получает FOG_W, южный — FOG_N. Линии — из таблицы FOGTAB_PAGE
// (tools/gen_fog_tab.js) в порядке обхода дерева путей: общее начало с предыдущей линией не
// проверяется заново, а за преградой в нём линия пропускается целиком — результат тот же, что у
// оригинала (прототип tools/proto/fog_fast.js: 0 расхождений), шагов втрое-вчетверо меньше.
#include <stdint.h>
#include "far.h"
#include "pages.h"
#include "memmap.h"
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

static uint8_t vm_page = PG_NONE, vm_np, vm_cur;   // vm_cur — что сейчас в Win3 (маска или туман)
static uint8_t vm_sx, vm_sy, vm_sz;
static uint8_t *const vm = (uint8_t *)0xC000;   // подключённая страница маски (или тумана)

// Память банка (build.ps1 ram = #BC00): номер клетки без умножений — строка и этаж таблицами
static uint16_t __at(0xBC00) ytab[64];          // y * sx
static uint16_t __at(0xBC80) ztab[8];           // z * sx * sy
// Туман войны: где каждый боец смотрел в прошлый раз (x, y, z, dir) — сектор пересчитывается
// только у сдвинувшегося или повернувшегося (UnitWalkBState / UnitTurnBState оригинала)
static uint8_t __at(0xBC90) fl_last[MAX_UNITS * 4];
// Прямоугольник вновь открытого (в осях вида: x - y, x + y, этаж) — его надо перерисовать
static int16_t __at(0xBCC0) bb_d0;
static int16_t __at(0xBCC2) bb_d1;
static int16_t __at(0xBCC4) bb_s0;
static int16_t __at(0xBCC6) bb_s1;
static uint8_t __at(0xBCC8) bb_z0;
static uint8_t __at(0xBCC9) bb_z1;
static uint8_t __at(0xBCCA) bb_on;
// Win1 полон — и свои переменные тумана здесь же
static uint8_t __at(0xBCCB) fog_pg;     // первая страница тумана (PG_NONE — нет, видно всё)
static uint8_t __at(0xBCCC) fog_np;
static uint8_t __at(0xBCCD) fog_all;    // пересчитать сектора всех (дверь сменила обзор, загрузка)
uint16_t __at(0xBCCE) vis_fog_n;        // открытых клеток (FOG_C); сценарии — 0A:3CCE
// Замер для сценариев: сколько кадров занял последний пересчёт тумана и сколько секторов в нём
static uint16_t __at(0xBCD0) fog_frames;   // 0A:3CD0
static uint8_t __at(0xBCD2) fog_units;     // 0A:3CD2
extern volatile uint16_t frames;           // crt0.s
#define FT_E 5                                  // байт на запись таблицы линий (gen_fog_tab.js)
static uint8_t __at(0xB800) tb[64 * FT_E];      // кусок таблицы линий
static uint8_t __at(0xBCD3) fd_x;               // дверь, сменившая обзор (fog_all = 2)
static uint8_t __at(0xBCD4) fd_y;
// Клетки линии, открытые за этот проход, и путь текущей линии (смещения от бойца) — по оси в
// своём массиве: индекс без умножения (SDCC звал __mul16 на каждый шаг)
static uint8_t __at(0xBE00) ml_x[24];
static uint8_t __at(0xBE18) ml_y[24];
static uint8_t __at(0xBE30) ml_z[24];
static int8_t __at(0xBE48) pp_x[24];
static int8_t __at(0xBE60) pp_y[24];
static int8_t __at(0xBE78) pp_z[24];

// Направление шага по (dx + 1) * 3 + (dy + 1): как vd_x / vd_y оригинала (0 — север, по часовой)
static const uint8_t dir_of[9] = { 7, 6, 5, 0, 8, 4, 1, 2, 3 };

// На поле ли клетка: отрицательное как беззнаковое — больше любого размера (поле до 64)
#define in_map(x, y, z) ((uint8_t)(x) < vm_sx && (uint8_t)(y) < vm_sy && (uint8_t)(z) < vm_sz)
#define iabs(v) ((int8_t)(v) < 0 ? (int8_t)-(v) : (int8_t)(v))

// Маска клетки (вне поля — 0: клетки нет, оригинал считает её блокировку нулём)
static uint8_t mk(int8_t x, int8_t y, int8_t z)
{
	if (!in_map(x, y, z)) return 0;
	uint16_t i = ztab[(uint8_t)z] + ytab[(uint8_t)y] + (uint8_t)x;
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
	uint8_t dx = (uint8_t)(ex - sx + 1), dy = (uint8_t)(ey - sy + 1);
	if (dx > 2 || dy > 2) return 0;
	uint8_t dir = dir_of[(uint8_t)((dx << 1) + dx + dy)];   // направление шага (0 — север, по часовой; 8 — на месте)
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

// ---------------------------------------------------------------- туман войны

// Клетка стала видна (целиком или стеной-дверью) — расширить прямоугольник перерисовки
static void fog_bb(uint8_t x, uint8_t y, uint8_t z)
{
	int16_t d = (int16_t)x - y, s = (int16_t)x + y;
	if (!bb_on) { bb_d0 = bb_d1 = d; bb_s0 = bb_s1 = s; bb_z0 = bb_z1 = z; bb_on = 1; return; }
	if (d < bb_d0) bb_d0 = d;
	if (d > bb_d1) bb_d1 = d;
	if (s < bb_s0) bb_s0 = s;
	if (s > bb_s1) bb_s1 = s;
	uint8_t z0 = bb_z0, z1 = bb_z1;      // сравнения байтов — с копиями (SDCC 4.5)
	if (z < z0) bb_z0 = z;
	if (z > z1) bb_z1 = z;
}

// Биты тумана клетки i (Tile::setDiscovered): новое — счёт и прямоугольник перерисовки
static void fset(uint16_t i, uint8_t bits, uint8_t x, uint8_t y, uint8_t z)
{
	uint8_t p = (uint8_t)(fog_pg + (i >> 14)), c = vm_cur;
	if (p != c) { pg_map3(p); vm_cur = p; }
	uint8_t *d = vm + (i & 0x3FFF);
	uint8_t o = *d, nw = (uint8_t)(o | bits);
	if (nw == o) return;
	*d = nw;
	if (o & FOG_C) return;               // клетка уже видна — стены на картинку не влияют
	if (bits & FOG_C) vis_fog_n++;
	fog_bb(x, y, z);
}

// Клетка на линии взгляда: она сама (пол, содержимое, обе свои стены), у восточного соседа —
// западная стена, у южного — северная (TileEngine.cpp:328-333)
static void fog_mark(uint8_t x, uint8_t y, uint8_t z)
{
	uint16_t i = ztab[z] + ytab[y] + x;
	fset(i, FOG_W | FOG_N | FOG_C, x, y, z);
	if ((uint8_t)(x + 1) < vm_sx) fset(i + 1, FOG_W, (uint8_t)(x + 1), y, z);
	if ((uint8_t)(y + 1) < vm_sy) fset(i + vm_sx, FOG_N, x, (uint8_t)(y + 1), z);
}

// Линия calculateLine от бойца (ux, uy, uz) к смещению (x1, y1, z1) с позиции st: позиции до st
// общие с прошлой линией и лежат в pp. Открытые клетки — в ml, потом разом в туман (чтобы Win3 не
// металось между маской и туманом на каждом шаге). Вернёт номер позиции упора (255 — дошла).
static uint8_t fov_line(uint8_t ux, uint8_t uy, uint8_t uz, int8_t x1, int8_t y1, int8_t z1, uint8_t st)
{
	int8_t a1 = x1, b1 = y1, c1 = z1, t;          // в осях линии: a — главная, b и c — ведомые
	uint8_t sxy = iabs(y1) > iabs(x1);
	if (sxy) { t = a1; a1 = b1; b1 = t; }
	uint8_t sxz = iabs(c1) > iabs(a1);
	if (sxz) { t = a1; a1 = c1; c1 = t; }
	uint8_t ddx = (uint8_t)iabs(a1), ddy = (uint8_t)iabs(b1), ddz = (uint8_t)iabs(c1);
	if (st > ddx) return 255;                      // путь целиком общий с прошлой линией
	int8_t stx = a1 < 0 ? -1 : 1, sty = b1 < 0 ? -1 : 1, stz = c1 < 0 ? -1 : 1;
	int8_t x = 0, y = 0, z = 0, lx = 0, ly = 0, lz = 0;
	int16_t dxy = ddx >> 1, dxz = ddx >> 1;
	uint8_t i = st;
	if (st) {
		// позиция st - 1 известна; ведомые оси на ней дают и наклон: после позиции k
		// drift = ddx/2 - k*ddy + |y_k|*ddx (до шага), — без прохода от начала линии
		uint8_t j = (uint8_t)(st - 1);
		lx = pp_x[j]; ly = pp_y[j]; lz = pp_z[j];
		int8_t px = lx, py = ly, pz = lz;
		if (sxy) { t = px; px = py; py = t; }
		if (sxz) { t = px; px = pz; pz = t; }
		x = px; y = py; z = pz;
		uint8_t k = (uint8_t)(st - 1);
		dxy = (int16_t)(ddx >> 1) - (int16_t)((uint16_t)k * ddy) + (int16_t)((uint16_t)(uint8_t)iabs(y) * ddx);
		dxz = (int16_t)(ddx >> 1) - (int16_t)((uint16_t)k * ddz) + (int16_t)((uint16_t)(uint8_t)iabs(z) * ddx);
		dxy -= ddy; dxz -= ddz;
		if (dxy < 0) { y += sty; dxy += ddx; }
		if (dxz < 0) { z += stz; dxz += ddx; }
		x += stx;
	}
	uint8_t nm = 0, b = 255;
	for (;;) {
		int8_t cx = x, cy = y, cz = z;
		if (sxz) { t = cx; cx = cz; cz = t; }
		if (sxy) { t = cx; cx = cy; cy = t; }
		pp_x[i] = cx; pp_y[i] = cy; pp_z[i] = cz;
		int8_t ax = (int8_t)(ux + cx), ay = (int8_t)(uy + cy), az = (int8_t)(uz + cz);
		if (i) {                                 // позиция 0 — клетка бойца: блокировок нет
			int8_t bx = (int8_t)(ux + lx), by = (int8_t)(uy + ly), bz = (int8_t)(uz + lz);
			int16_t tv = cz != lz ? vblk(bx, by, bz, ax, ay, az) : 0;   // этаж тот же — vblk 0
			int16_t r = hblk(bx, by, bz, ax, ay, i < 2);
			if (r == -1) {
				if (tv <= 127) {                 // большая стена: клетка в траектории, дальше нет
					ml_x[nm] = (uint8_t)ax; ml_y[nm] = (uint8_t)ay; ml_z[nm] = (uint8_t)az; nm++;
					b = i;
					break;
				}
				r = 0;
			}
			if (r + tv > 127) { b = i; break; }  // упёрлась: эта клетка не открывается
		}
		ml_x[nm] = (uint8_t)ax; ml_y[nm] = (uint8_t)ay; ml_z[nm] = (uint8_t)az; nm++;
		lx = cx; ly = cy; lz = cz;
		if (i == ddx) break;
		dxy -= ddy; dxz -= ddz;
		if (dxy < 0) { y += sty; dxy += ddx; }
		if (dxz < 0) { z += stz; dxz += ddx; }
		x += stx;
		i++;
	}
	for (uint8_t k = 0; k < nm; k++) fog_mark(ml_x[k], ml_y[k], ml_z[k]);
	return b;
}

// Сектор бойца (calculateFOV для тумана): линии таблицы FOGTAB_PAGE по порядку. Общее начало с
// последней посчитанной линией — минимум L с неё (цели вне карты пропускаются, но в минимум идут);
// если оно уже упёрлось, вся ветка до J (записи с L не меньше этой) упирается там же — прыжок
static void fov_unit(uint8_t ux, uint8_t uy, uint8_t uz, uint8_t dir)
{
	uint8_t swap = (dir & 3) == 0;                 // направления 0 и 4: оси сектора меняются местами
	int8_t gx = sg_x[dir], gy = sg_y[dir];
	uint16_t n0 = far_word(FAR(FOGTAB_PAGE, 0));
	uint16_t n = (dir & 1) ? far_word(FAR(FOGTAB_PAGE, 2)) : n0;
	uint16_t off = 4 + ((dir & 1) ? n0 * FT_E : 0);
	uint16_t tbk = 0xFF00, k = 0;                  // tbk — первая запись в tb (0xFF00 — пусто)
	uint8_t have = 0, pb = 0, leff = 0;
	while (k < n) {
		uint16_t r = k - tbk;
		if (r >= 64) {
			uint16_t m = n - k;
			if (m > 64) m = 64;
			far_read(FAR(FOGTAB_PAGE, 0) + (uint16_t)(off + k * FT_E), tb, m * FT_E);
			tbk = k;
			r = 0;
		}
		const uint8_t *e = tb + (((uint16_t)r << 2) + r);   // r * FT_E без умножения
		uint8_t l = e[2];
		if (l < leff) leff = l;
		if (have) {
			uint8_t le = leff, p = pb;
			if (p < le) { k = e[3] | ((uint16_t)e[4] << 8); continue; }   // упор внутри общего начала
		}
		k++;
		int8_t lx = (int8_t)(e[0] & 31), ly = (int8_t)e[1], dz = (int8_t)((e[0] >> 5) - 3);
		int8_t wx = swap ? ly : lx, wy = swap ? lx : ly;
		if (gx < 0) wx = -wx;
		if (gy < 0) wy = -wy;
		if (!in_map((int8_t)(ux + wx), (int8_t)(uy + wy), (int8_t)(uz + dz))) continue;
		pb = fov_line(ux, uy, uz, wx, wy, dz, have ? leff : 0);
		have = 1;
		leff = 255;
	}
}

// Туман: сектора бойцов, сдвинувшихся или повернувшихся с прошлого раза (у крупного — с каждой
// из четырёх клеток, «четыре пары глаз»)
static void fog_update(const vu_t *u, uint8_t nsold)
{
	if (fog_pg == PG_NONE) return;
	uint8_t all = fog_all, nu = 0;
	uint16_t f0 = frames;
	for (uint8_t s = 0; s < nsold && s < MAX_UNITS; s++) {
		const vu_t *v = &u[s];
		if (!(v->flags & VU_ALIVE)) continue;
		uint8_t *l = fl_last + s * 4;
		uint8_t x = v->x, y = v->y, z = v->z, d = v->dir & 7;
		uint8_t l0 = l[0], l1 = l[1], l2 = l[2], l3 = l[3];
		uint8_t again = all == 1;
		if (all == 2) {                    // дверь: только тем, кому она в пределах обзора
			uint8_t ax = fd_x, ay = fd_y;
			uint8_t ddx = x > ax ? x - ax : ax - x, ddy = y > ay ? y - ay : ay - y;
			again = ddx <= MAX_VIEW && ddy <= MAX_VIEW;
		}
		if (!again && l0 == x && l1 == y && l2 == z && l3 == d) continue;
		l[0] = x; l[1] = y; l[2] = z; l[3] = d;
		uint8_t nq = (v->flags & VU_BIG) ? 4 : 1;
		for (uint8_t q = 0; q < nq; q++) fov_unit((uint8_t)(x + (q & 1)), (uint8_t)(y + (q >> 1)), z, d);
		nu++;
	}
	fog_all = 0;
	if (nu) { fog_frames = frames - f0; fog_units = nu; }
}

uint8_t vis_fog_page(void) __banked
{
	return fog_pg;
}

uint8_t vis_fog_rect(int16_t *r) __banked
{
	if (!bb_on) return 0;
	r[0] = bb_d0 * 16;
	r[1] = bb_d1 * 16 + 31;
	r[2] = bb_s0 * 8 - (int16_t)bb_z1 * 24 - 24;   // часть клетки поднята MCD.P_Level до 24 точек
	r[3] = bb_s1 * 8 - (int16_t)bb_z0 * 24 + 39;
	bb_on = 0;
	return 1;
}

void vis_reveal(uint8_t x0, uint8_t y0, uint8_t x1, uint8_t y1) __banked
{
	if (fog_pg == PG_NONE) return;
	uint8_t old = pg_win3();
	vm_cur = old;
	if (x1 >= vm_sx) x1 = vm_sx - 1;
	if (y1 >= vm_sy) y1 = vm_sy - 1;
	for (uint8_t z = 0; z < vm_sz; z++)
		for (uint8_t y = y0; y <= y1; y++)
			for (uint8_t x = x0; x <= x1; x++)
				fset(ztab[z] + ytab[y] + x, FOG_W | FOG_N | FOG_C, x, y, z);
	if (vm_cur != old) pg_map3(old);
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
	fog_update(u, nsold);
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

// Кого из пришельцев видит юнит sel прямо сейчас (BattleUnit::getVisibleUnits после calculateFOV):
// номера — в list (до max), вернёт их число
uint8_t vis_list(vu_t *u, uint8_t n, uint8_t nsold, uint8_t sel, uint8_t *list, uint8_t max) __banked
{
	uint8_t old = pg_win3(), cnt = 0;
	vm_cur = old;
	const vu_t *v = &u[sel];
	if (vm_page == PG_NONE || !(v->flags & VU_ALIVE)) return 0;
	for (uint8_t i = nsold; i < n && cnt < max; i++) {
		const vu_t *a = &u[i];
		if (!(a->flags & VU_ALIVE)) continue;
		uint8_t nq = (a->flags & VU_BIG) ? 4 : 1, seen = 0;
		for (uint8_t q = 0; q < nq && !seen; q++)
			seen = sees(v, (uint8_t)(a->x + (q & 1)), (uint8_t)(a->y + (q >> 1)), a->z);
		if (seen) list[cnt++] = i;
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
	// fog_pg — в памяти банка, начального значения у неё нет: верить ей можно, только когда
	// маска есть (туман берётся вместе с ней в vis_build)
	if (vm_page != PG_NONE) {
		pg_free(vm_page, vm_np);
		if (fog_pg != PG_NONE) pg_free(fog_pg, fog_np);
	}
	vm_page = PG_NONE;
	fog_pg = PG_NONE;
}

uint8_t vis_build(far_t cells, uint8_t sx, uint8_t sy, uint8_t sz, uint8_t page) __banked
{
	uint8_t fl[256], bw[256], row[64 * 4], out[64];
	vis_free();
	uint16_t total = (uint16_t)sx * sy * sz;
	vm_np = (uint8_t)((total + 16383) >> 14);
	vm_page = pg_alloc(vm_np, 1);
	if (vm_page == PG_NONE) return 0;
	vm_sx = sx; vm_sy = sy; vm_sz = sz;
	for (uint8_t y = 0; y < sy && y < 64; y++) ytab[y] = (uint16_t)y * sx;
	for (uint8_t z = 0; z < sz && z < 8; z++) ztab[z] = (uint16_t)z * sx * sy;
	// Туман войны: всё закрыто; страниц не нашлось — видно всё (вид проверяет fog_pg)
	fog_np = vm_np;
	fog_pg = pg_alloc(fog_np, 1);
	if (fog_pg != PG_NONE) far_fill(FAR(fog_pg, 0), 0, total);
	vis_fog_n = 0;
	fog_all = 1;
	bb_on = 0;
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
	// обзор сменился — сектора бойцов в пределах обзора от двери заново (calculateFOV(pos));
	// вторая дверь до пересчёта — тогда уже всех
	if (fog_all) fog_all = 1;
	else { fog_all = 2; fd_x = x; fd_y = y; }
}
