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
#include "mapgen.h"

#define VM_W   1               // западная стена держит обзор (MCD.Stop_LOS)
#define VM_N   2               // северная стена
#define VM_O   4               // объект
#define VM_F   8               // пол: глухой (есть и не No_Floor) или держит обзор — 256
// старшая тетрада — MCD.Big_Wall объекта (Pathfinding::bigWallTypes: 1 BLOCK, 2 NESW, 3 NWSE,
// 4 WEST, 5 NORTH, 6 EAST, 7 SOUTH, 8 EAST_AND_SOUTH, 9 WEST_AND_NORTH)
#define MAX_VIEW 20            // TileEngine.h MAX_VIEW_DISTANCE

static uint8_t vm_page = PG_NONE, vm_np;
static uint8_t __at(0xBCD5) vm_cur;     // что сейчас в Win3 (маска или туман); его читает vision_s.s
static uint8_t vm_sx, vm_sy, vm_sz;
static uint8_t *const vm = (uint8_t *)0xC000;   // подключённая страница маски (или тумана)

// Память банка (build.ps1 ram = #BC00): номер клетки без умножений — строка и этаж таблицами
// (поле до 80 клеток — MAX_MOD 8 модулей: суда TFTD 3x7 модулей, поле квадратное — 70x70;
// ytab и ztab — в одной странице #BC, vision_s.s aidx на это рассчитан)
#define VM_MAXW 80
static uint16_t __at(0xBC00) ytab[VM_MAXW];     // y * sx (до #BC9F)
static uint16_t __at(0xBCA0) ztab[8];           // z * sx * sy
// Туман войны: где каждый боец смотрел в прошлый раз (x, y, z, dir) — сектор пересчитывается
// только у сдвинувшегося или повернувшегося (UnitWalkBState / UnitTurnBState оригинала); на всех
// юнитов миссии — смотрит и захваченный пришелец (до #BD9F)
static uint8_t __at(0xBD00) fl_last[MAX_BUNITS * 4];
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
// #B800..#B93F — кусок таблицы линий (vision_s.s fu_run, TB)
static uint8_t __at(0xBCD3) fd_x;               // дверь, сменившая обзор (fog_all = 2)
static uint8_t __at(0xBCD4) fd_y;
// Клетки карты и рабочая страница (свойства частей) — для глаз юнита (eye_bits); задаёт vis_build
static far_t __at(0xBCDE) vm_cells;
static uint8_t __at(0xBCE2) vm_wpg;
uint8_t __at(0xBCE3) vis_eye_dbg;          // глаза последнего бойца 0 в vis_seen — сценарии, 0A:3CE3
uint16_t __at(0xBCE4) vis_rev_n;           // открытых клеток после revealedFloors (vis_blocks) — 0A:3CE4
#define EYE_FOG 1                       // сектор тумана — с этажа выше (calculateFOV:255)
#define EYE_SEE 2                       // линия взгляда на юнитов — с этажа выше (getSightOriginVoxel)
#define EYE_CONE 4                      // sees: только в секторе обзора (calculateFOV); без него — visible
// Нарисованная область вида (vis_view): открытое вне её не перерисовывается
static int16_t __at(0xBCD6) vw_lo;
static int16_t __at(0xBCD8) vw_hi;
static int16_t __at(0xBCDA) vw_top;
static int16_t __at(0xBCDC) vw_bot;
// Клетки линии, открытые за этот проход (ml_x / ml_y / ml_z по #BE00, #BE18, #BE30 — по оси в своём
// массиве) — их пишет и читает только vision_s.s; #BE48..#BE56 — заготовки линии (vision_s.s FX).
// Пути прошлой линии хранить не нужно: позицию перед стартом даёт прогон Брезенхема (fl_line)
// Состояние линии для цикла на ассемблере (vision_s.s, поля — те же смещения)
typedef struct {
	uint8_t ux, uy, uz;        // клетка бойца
	int8_t x, y, z;            // позиция в осях линии
	int8_t lx, ly, lz;         // прошлая позиция (смещение от бойца)
	uint8_t i, ddx, ddy, ddz;
	int8_t stx, sty, stz;
	uint8_t sxy, sxz;
	int16_t dxy, dxz;
	uint8_t nm, b, pad;        // открытых клеток в ml_*, позиция упора
	uint8_t bx, by, bz, ax, ay, az, skip;   // шаг: откуда, куда, пропуск объекта
	int8_t cx, cy, cz;
	int8_t x1, y1, z1;         // fl_line: цель (смещение от бойца)
	uint8_t sv;                // H.i и H.s — номер и маска прошлой клетки линии верны
	uint16_t ai;               // номер текущей клетки
	uint8_t k, bbx, bby, bbz;  // fm_run: номер в ml_*, клетка для fog_bbn
	uint16_t fi;               // fm_run: номер клетки
	uint8_t tv;                // fl_zstep: vblk держит
} fls_t;
static fls_t __at(0xBE90) F;           // до #BEBF
void fl_line(void);                     // vision_s.s: линия к F.x1..z1 с позиции F.i
void fm_run(void);                      // vision_s.s: клетки ml_* — в туман (fog_mark)
// hblk на ассемблере (vision_s.s hb_fast): 0 — свободно, 1 — упор, #FF — большая стена (-1)
typedef struct {
	int8_t sx, sy, z, ex, ey;
	uint8_t skip;
	uint8_t vmp, msx, msy, msz;            // копии vm_page и размеров поля (vis_build)
	uint8_t dir, s, e, t, org, b;          // рабочие (vision_s.s)
	uint16_t i;                            // номер клетки s
	int16_t nsx, wsx;                      // -sx и sx: соседи по y смещением номера (vis_build)
	uint8_t work[6];
} hb_t;
static hb_t __at(0xBEC0) H;
// Обход таблицы линий сектора (vision_s.s fu_run): вход, за ним — рабочие поля
typedef struct {
	uint16_t n, off;           // записей, начало таблицы (байт от начала FOGTAB_PAGE)
	uint8_t sw, nx, ny;        // оси сектора обменены, x и y — со знаком минус
} fu_t;
static fu_t __at(0xBEE0) U;
void fu_run(void);

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

// Глаза юнита этажом выше: рост (с парением) над уровнем пола клетки (Tile::getTerrainLevel — меньший
// MCD.T_Level пола и объекта; лестница опускает пол — глаза выше) доходит до следующего этажа, а над
// клеткой нет пола (hasNoFloor). Туман — от 28 (TileEngine.cpp:255-262), линия на юнитов — глаз на
// воксель ниже роста, у крупного — на воксель выше, от 25 / 24 (getSightOriginVoxel, :362-385).
// far_* возвращают Win3 в логическую страницу — её и держит vm_cur (pg_map3)
static uint8_t eye_bits(const vu_t *v)
{
	uint8_t z = v->z;
	if ((uint8_t)(z + 1) >= vm_sz) return 0;
	uint16_t i = ztab[z] + ytab[v->y] + v->x;
	uint8_t c[4];
	far_read(vm_cells + ((uint32_t)i << 2), c, 4);
	far_t tl_tab = FAR(vm_wpg, PF_TLEVEL);
	int8_t tl = c[0] ? (int8_t)far_byte(tl_tab + c[0]) : 0;
	if (c[3]) { int8_t t = (int8_t)far_byte(tl_tab + c[3]); if (t < tl) tl = t; }
	int16_t e = (int16_t)(v->flags >> 3) - tl;
	if (v->flags & VU_BIG) e++;
	if (e < 25) return 0;
	uint8_t a = far_byte(vm_cells + ((uint32_t)(i + ztab[1]) << 2));   // пол клетки выше
	if (a && !(far_byte(FAR(vm_wpg, PF_FLAGS) + a) & 1)) return 0;
	if (v->flags & VU_BIG) e--;
	return e >= 28 ? EYE_FOG | EYE_SEE : EYE_SEE;
}

// Видит ли v клетку (x, y, z) с юнитом: сектор (eye & EYE_CONE — calculateFOV), дальность и линия
// взгляда (TileEngine::visible: сектора там нет); eye — глаза v (eye_bits): EYE_SEE — линия от
// клетки этажом выше
static uint8_t sees(const vu_t *v, uint8_t eye, uint8_t x, uint8_t y, uint8_t z)
{
	int8_t dx = (int8_t)(x - v->x), dy = (int8_t)(y - v->y);
	if (eye & EYE_CONE) { if (!in_cone(v->dir & 7, dx, dy)) return 0; }
	else if ((int16_t)dx * dx + (int16_t)dy * dy > MAX_VIEW * MAX_VIEW) return 0;
	return los((int8_t)v->x, (int8_t)v->y, (int8_t)(v->z + ((eye & EYE_SEE) ? 1 : 0)), (int8_t)x, (int8_t)y, (int8_t)z);
}

// ---------------------------------------------------------------- туман войны

// Клетка стала видна (целиком или стеной-дверью) — расширить прямоугольник перерисовки. Клетка вне
// нарисованной области вида (спрайт 32x40, часть поднята MCD.P_Level до 24 точек — как vis_fog_rect)
// не в счёт: на открытом поле новое — дуга в 20 клетках от бойца, а рамка с ближними клетками
// перерисовывала бы весь экран на каждом шаге
static void fog_bb(uint8_t x, uint8_t y, uint8_t z)
{
	int16_t d = (int16_t)x - y, s = (int16_t)x + y;
	int16_t px = d * 16, py = s * 8 - (int16_t)z * 24;
	if (px + 31 < vw_lo || px > vw_hi || py + 39 < vw_top || py - 24 > vw_bot) return;
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
// западная стена, у южного — северная (TileEngine.cpp:328-333) — vision_s.s fm_run (бит FOG_L —
// уже отмечена линией, и соседи тоже). Новое видимое — сюда, в прямоугольник перерисовки
void fog_bbn(void)
{
	fog_bb(F.bbx, F.bby, F.bbz);
}

// Линия calculateLine от бойца F.ux..uz к смещению F.x1..z1 с позиции F.i — vision_s.s fl_line:
// позиции до F.i общие с прошлой линией и лежат в pp. Открытые клетки — в ml, потом разом в туман
// (fm_run — чтобы Win3 не металось между маской и туманом на каждом шаге); F.b — позиция упора
// (255 — дошла). Шаги со сменой этажа — vision_s.s fl_zstep (vblk и hblk, как los).

// Сектор бойца (calculateFOV для тумана): линии таблицы FOGTAB_PAGE по порядку — vision_s.s fu_run.
// Общее начало с последней посчитанной линией — минимум L с неё (цели вне карты пропускаются, но в
// минимум идут); если оно уже упёрлось, вся ветка до J (записи с L не меньше этой) упирается там же —
// прыжок. Здесь — какая из двух таблиц (прямо / наискось) и знаки осей направления
static void fov_unit(uint8_t ux, uint8_t uy, uint8_t uz, uint8_t dir)
{
	F.ux = ux; F.uy = uy; F.uz = uz;               // боец для fl_line
	uint16_t n0 = far_word(FAR(FOGTAB_PAGE, 0));
	U.n = (dir & 1) ? far_word(FAR(FOGTAB_PAGE, 2)) : n0;
	U.off = 4 + ((dir & 1) ? n0 * FT_E : 0);
	U.sw = (dir & 3) == 0;                         // направления 0 и 4: оси сектора меняются местами
	U.nx = sg_x[dir] < 0;
	U.ny = sg_y[dir] < 0;
	fu_run();
}

// Туман: сектора бойцов, сдвинувшихся или повернувшихся с прошлого раза (у крупного — с каждой
// из четырёх клеток, «четыре пары глаз»)
static void fog_update(const vu_t *u, uint8_t n)
{
	if (fog_pg == PG_NONE) return;
	uint8_t all = fog_all, nu = 0;
	uint16_t f0 = frames;
	for (uint8_t s = 0; s < n && s < MAX_BUNITS; s++) {
		const vu_t *v = &u[s];
		uint8_t *l = fl_last + s * 4;
		// не смотрит за отряд — сектор забыт: станет своим (контроль разума) — посчитать заново
		if (!(v->flags & VU_ALIVE) || !(v->dir & VU_XD)) { l[3] = 0xFF; continue; }
		// глаза высокого — этажом выше: в fl_last этаж глаз (присел / встал — сектор заново)
		uint8_t x = v->x, y = v->y, z = (uint8_t)(v->z + (eye_bits(v) & EYE_FOG)), d = v->dir & 7;
		uint8_t l0 = l[0], l1 = l[1], l2 = l[2], l3 = l[3];
		uint8_t again = all == 1;
		// дверь: только тем, у кого она в секторе обзора — туман растёт только в секторе, а полный
		// пересчёт шестерых в поле после двери стоил ~450 кадров
		if (all == 2) again = in_cone(d, (int8_t)(fd_x - x), (int8_t)(fd_y - y));
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

void vis_view(const int16_t *r) __banked
{
	if (r) { vw_lo = r[0]; vw_hi = r[1]; vw_top = r[2]; vw_bot = r[3]; }
	else { vw_lo = vw_top = -32768; vw_hi = vw_bot = 32767; }
}

// Открыть клетки прямоугольника на этажах из маски zm (бит z). Бит FOG_L не ставится: стены
// восточного и южного соседей откроет линия обзора, как в оригинале (setDiscovered(…, 2))
static void reveal_box(uint8_t x0, uint8_t y0, uint8_t x1, uint8_t y1, uint8_t zm)
{
	if (fog_pg == PG_NONE) return;
	uint8_t old = pg_win3();
	vm_cur = old;
	if (x1 >= vm_sx) x1 = vm_sx - 1;
	if (y1 >= vm_sy) y1 = vm_sy - 1;
	for (uint8_t z = 0; z < vm_sz; z++, zm >>= 1) {
		if (!(zm & 1)) continue;
		for (uint8_t y = y0; y <= y1; y++)
			for (uint8_t x = x0; x <= x1; x++)
				fset(ztab[z] + ytab[y] + x, FOG_W | FOG_N | FOG_C, x, y, z);
	}
	if (vm_cur != old) pg_map3(old);
}

void vis_reveal(uint8_t x0, uint8_t y0, uint8_t x1, uint8_t y1) __banked
{
	reveal_box(x0, y0, x1, y1, 0xFF);
}

void vis_blocks(void) __banked
{
	uint8_t rv[64];
	uint8_t mx = mapgen_reveal(rv);
	for (uint8_t my = 0; my < mx && my < 8; my++)
		for (uint8_t k = 0; k < mx && k < 8; k++) {
			uint8_t zm = rv[my * 8 + k];
			if (zm) reveal_box((uint8_t)(k * 10), (uint8_t)(my * 10), (uint8_t)(k * 10 + 9), (uint8_t)(my * 10 + 9), zm);
		}
	vis_rev_n = vis_fog_n;
}

// Видит ли v (глаза ey) юнита a — любую его клетку
static uint8_t sees_u(const vu_t *v, uint8_t ey, const vu_t *a)
{
	uint8_t nq = (a->flags & VU_BIG) ? 4 : 1;
	for (uint8_t q = 0; q < nq; q++)
		if (sees(v, ey, (uint8_t)(a->x + (q & 1)), (uint8_t)(a->y + (q >> 1)), a->z)) return 1;
	return 0;
}

// Смотрит ли юнит за отряд: живой и на стороне игрока
#define XEYE(v) (((v)->flags & VU_ALIVE) && ((v)->dir & VU_XD))

uint8_t vis_seen(vu_t *u, uint8_t n, uint8_t *list, uint8_t max) __banked
{
	uint8_t old = pg_win3(), cnt = 0;
	vm_cur = old;
	if (vm_page == PG_NONE) {            // маски нет (не хватило страниц) — видны все
		for (uint8_t i = 0; i < n; i++)
			if ((u[i].flags & (VU_ALIVE | VU_SEEN)) == VU_ALIVE && !(u[i].dir & VU_XD)) { u[i].flags |= VU_SEEN; if (cnt < max) list[cnt++] = i; }
		return cnt;
	}
	fog_update(u, n);
	uint8_t vw[MAX_BUNITS], ey[MAX_BUNITS], nv = 0;   // свои смотрящие и их глаза — раз на вызов
	for (uint8_t s = 0; s < n; s++)
		if (XEYE(&u[s])) { vw[nv] = s; ey[nv++] = eye_bits(&u[s]) | EYE_CONE; }
	vis_eye_dbg = nv && !vw[0] ? ey[0] & ~EYE_CONE : 0;
	for (uint8_t i = 0; i < n; i++) {
		vu_t *a = &u[i];
		if ((a->flags & (VU_ALIVE | VU_SEEN)) != VU_ALIVE || (a->dir & VU_XD)) continue;   // в свой ход отряд не теряет замеченных
		uint8_t seen = 0;
		for (uint8_t k = 0; k < nv && !seen; k++)
			seen = sees_u(&u[vw[k]], ey[k], a);
		if (!seen) continue;
		a->flags |= VU_SEEN;
		if (cnt < max) list[cnt++] = i;
	}
	if (vm_cur != old) pg_map3(old);
	return cnt;
}

// Кого из пришельцев видит юнит sel прямо сейчас (BattleUnit::getVisibleUnits после calculateFOV):
// номера — в list (до max), вернёт их число
uint8_t vis_list(vu_t *u, uint8_t n, uint8_t sel, uint8_t *list, uint8_t max) __banked
{
	uint8_t old = pg_win3(), cnt = 0;
	vm_cur = old;
	const vu_t *v = &u[sel];
	if (vm_page == PG_NONE || !(v->flags & VU_ALIVE)) return 0;
	uint8_t ey = eye_bits(v) | EYE_CONE;
	for (uint8_t i = 0; i < n && cnt < max; i++) {
		if ((u[i].flags & VU_ALIVE) && !(u[i].dir & VU_XD) && sees_u(v, ey, &u[i])) list[cnt++] = i;
	}
	if (vm_cur != old) pg_map3(old);
	return cnt;
}

// Видит ли кто-то из своих (VU_XD) юнита a (calculateFOV после шага пришельца)
uint8_t vis_by_xcom(vu_t *u, uint8_t n, uint8_t a) __banked
{
	uint8_t old = pg_win3(), seen = 0;
	vm_cur = old;
	if (vm_page == PG_NONE) return 1;
	for (uint8_t s = 0; s < n && !seen; s++)
		if (XEYE(&u[s])) seen = sees_u(&u[s], eye_bits(&u[s]) | EYE_CONE, &u[a]);
	if (vm_cur != old) pg_map3(old);
	return seen;
}

// Видит ли юнит s юнита t: cone — в секторе обзора (calculateFOV), иначе без сектора — дальность и
// линия взгляда (TileEngine::visible, так ИИ выбирает цель)
uint8_t vis_pair(vu_t *u, uint8_t s, uint8_t t, uint8_t cone) __banked
{
	uint8_t old = pg_win3(), seen = 0;
	vm_cur = old;
	if (vm_page == PG_NONE) return 1;
	if ((u[s].flags & VU_ALIVE) && (u[t].flags & VU_ALIVE))
		seen = sees_u(&u[s], eye_bits(&u[s]) | (cone ? EYE_CONE : 0), &u[t]);
	if (vm_cur != old) pg_map3(old);
	return seen;
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
	uint8_t fl[256], bw[256], row[VM_MAXW * 4], out[VM_MAXW];   // строка поля — до 80 клеток
	vis_free();
	uint16_t total = (uint16_t)sx * sy * sz;
	vm_np = (uint8_t)((total + 16383) >> 14);
	vm_page = pg_alloc(vm_np, 1);
	if (vm_page == PG_NONE) return 0;
	vm_sx = sx; vm_sy = sy; vm_sz = sz;
	H.vmp = vm_page; H.msx = sx; H.msy = sy; H.msz = sz;   // для hb_fast
	H.wsx = sx; H.nsx = -(int16_t)sx;
	vm_cells = cells; vm_wpg = page;      // для глаз юнита (eye_bits)
	for (uint8_t y = 0; y < sy && y < VM_MAXW; y++) ytab[y] = (uint16_t)y * sx;
	for (uint8_t z = 0; z < sz && z < 8; z++) ztab[z] = (uint16_t)z * sx * sy;
	// Туман войны: всё закрыто; страниц не нашлось — видно всё (вид проверяет fog_pg)
	fog_np = vm_np;
	fog_pg = pg_alloc(fog_np, 1);
	if (fog_pg != PG_NONE) far_fill(FAR(fog_pg, 0), 0, total);
	vis_fog_n = 0;
	fog_all = 1;
	bb_on = 0;
	vis_view(0);
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
	// обзор сменился — сектора бойцов, у кого дверь в секторе, заново (calculateFOV(pos)); вторая
	// дверь до пересчёта — всех, кроме соседней створки той же двери (пара створок НЛО)
	uint8_t ax = fd_x, ay = fd_y, all = fog_all;   // сравнения — с копиями (SDCC 4.5)
	if (!all) { fog_all = 2; fd_x = x; fd_y = y; }
	else if (all == 2) {
		if ((uint8_t)(x - ax + 1) > 2 || (uint8_t)(y - ay + 1) > 2) fog_all = 1;
	}
}
