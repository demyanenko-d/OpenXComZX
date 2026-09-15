// Банк 25: тень глобуса (день и ночь) — OpenXcom Globe::drawShadow (Globe.cpp:176–245,
// 986–1008) без сезонов: уровень пары k = ⌊тень/3⌋ (0..10), project_docs/globe.md §6.4,
// модель tools/globe_shadow.js (spans).
//
// Вывод. Строка заднего буфера с тенью заранее заполняется: x 0..255 — добавка суши
// L = ⌊(v_k − шум)/3⌋, x 256..511 — цвет океана O = OCEAN + (v_k − шум) (v_k = 3k + 1, 0 и 31 у
// крайних уровней). Проход строк (globe_s.s) кладёт сушу BLT2 по полубайтам с насыщением
// (узор + L = ровно getLandShadow), океан — копией из x + 256. Интервал уровня строки — одно
// 2D DMA из строк-образцов уровня (L и O через 256, шум — сдвиг строки образца).
//
// Уровни по строке. Эллипс уровня k на экране — сдвиг R·D_k·(sx, sy) и масштаб a_k ≈ 1
// эллипса E0 (проекция большого круга ⟂ s); таблица E0 на кадр — X левой и правой точки по
// смещению v (пиксели) от центра. Строка y, уровень k: v = (y + 0.5 − 100 − cy_k) / a_k,
// x = cx_k + a_k·T[v]; видимость — диапазоны v по сторонам (дуга между точками касания края).
// Порядок пересечений — вложенность эллипсов (sz ≥ 0: ночь по краям строки). Уровни крайних
// пар строки точны (t = TX + TY + TZ); на зумах 0–1 — и экстремума строки (e·s вдоль строки
// вогнута или выпукла): у края диска проекция малого круга касается края, дуги у точек касания
// выпадают при округлении видимости — нехватку смен добирают крайние пересечения.
//
// Здесь — подготовка кадра (солнце, таблицы t, уровни, видимость); таблица E0 и проход строк —
// globe_sh_s.s.
#include <stdint.h>
#include <string.h>
#include "tsconf.h"
#include "memmap.h"
#include "pages.h"
#include "far.h"
#include "state.h"
#include "globe.h"
#include "globe_sh_tab.h"

int32_t __mulsint2slong(int16_t a, int16_t b);
#define MUL(a, b) __mulsint2slong((int16_t)(a), (int16_t)(b))
#define HI16(v)   ((int16_t)((uint32_t)(v) >> 16))
#define SHR14(v)  HI16((int32_t)(v) << 2)
#define sin16(a)  globe_sin((uint16_t)(a))
#define cos16(a)  globe_sin((uint16_t)(a) + 0x4000)

// Страница тени (Win3 на время расчёта; globe_sh_s.s — те же смещения)
#define SH_PAT    0x0000                // 17 узоров по 512 байт: строка L, строка O (через 256)
#define SH_TLL    0x2200                // E0 [512]: X левой точки (пары 8.8 от центра) мл., ст. (+#200)
#define SH_TX     0x2A00                // t16 от x пары [128] мл., ст. (+128)
#define SH_TZ     0x2B00                // t16 от z [256] мл., ст. (+256)
#define SH_TA     0x2D00                // t16 экстремума строки по ρ [256] мл., ст. (+256)
#define LV        0x3200                // уровни (по 16): A0, A1, A2, SF, CXL, CXH, SC, L (5), R (5)
#define LV_ORDL   0x3310                // порядок уровней стороны L, R
#define LV_ORDR   0x3320
#define NPAT      17                    // узоры: 0..10 — уровни, 11..16 — полосы по два уровня

static const int16_t zoom_r[GLOBE_ZOOMS] = { 90, 120, 180, 280, 450, 720 };
// D_k = −TB_k / 250 и a_k = √(1 − D_k²), Q14 (TB — пороги shade_gradient, globe_sh_tab.h)
static const int16_t dk14[10] = { 2228, 786, 524, 328, 131, -131, -328, -524, -918, -2556 };
static const int16_t ak14[10] = { 16231, 16365, 16376, 16381, 16384, 16384, 16381, 16376, 16358, 16183 };
static const int16_t ia14[10] = { 16538, 16403, 16392, 16387, 16384, 16384, 16387, 16392, 16410, 16587 };   // 1/a_k, Q14
// Набор границ по зумам: полный (10 уровней) или по два (границы 1, 3, 5, 7, 9 — узоры 11..16)
static const uint8_t lev_full[10] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9 };
static const uint8_t lev_pair[5] = { 0, 2, 4, 6, 8 };
static const uint8_t zoom_pair[GLOBE_ZOOMS] = { 0, 0, 0, 1, 1, 1 };

static uint8_t shp = PG_NONE, pat_ocean = 0xFF;

// globe_sh_s.s: таблица E0 и проход строк (параметры — там же)
void sh_e0(void);
void sh_rows(void);
extern uint16_t sh_j0, sh_jn, sh_w4;
extern int32_t sh_qw, sh_dq, sh_ddq, sh_sw, sh_xc, sh_dxc;
extern uint16_t sh_zi;
extern uint8_t sh_nlev, sh_cross, sh_top, sh_base, sh_limb, sh_page;
extern int8_t sh_dl;
extern int16_t sh_txh;
extern int32_t sh_ty, sh_dty;
extern const uint8_t *sh_lut;

static uint16_t isqrt32(uint32_t v)
{
	uint32_t r = 0, b = 1ul << 30;
	while (b > v) b >>= 2;
	while (b) {
		if (v >= r + b) { v -= r + b; r = (r >> 1) + b; }
		else r >>= 1;
		b >>= 2;
	}
	return (uint16_t)r;
}

// ---------------------------------------------------------------- время

uint16_t globe_sunlon(void) __banked
{
	uint8_t h = ST->hour + 18;
	if (h >= 24) h -= 24;
	uint32_t sec = (uint32_t)h * 3600u + (uint16_t)ST->minute * 60u + ST->second;   // 0..86399
	uint16_t day = (uint16_t)((sec * 3107u) >> 12);                               // * 65536 / 86400
	return (uint16_t)(0x4000 - day);
}

// ---------------------------------------------------------------- узоры

// Шум: 256 значений 0..3 (как rand() % 4 OpenXcom) и бит смеси двух уровней полосы
static uint8_t noise_at(uint16_t i, uint8_t salt)
{
	uint16_t x = i * 0x9E37u + salt * 0x79B9u;
	x ^= x >> 7; x *= 0x2F1Du; x ^= x >> 9;
	return (uint8_t)x;
}

static uint8_t level_v(uint8_t k)
{
	return k == 0 ? 0 : k >= 10 ? 31 : (uint8_t)(3 * k + 1);
}

static void patterns(uint8_t ocean)
{
	uint8_t *p = (uint8_t *)(0xC000 + SH_PAT);
	for (uint8_t i = 0; i < NPAT; i++, p += 512)
		for (uint16_t x = 0; x < 256; x++) {
			uint8_t k = i, n = noise_at(x, 1) & 3;
			if (i > 10) { uint8_t b = i - 11; k = b ? (uint8_t)(2 * b - (noise_at(x, 2) & 1)) : 0; }
			uint8_t v = level_v(k), s = v > n ? v - n : 0;
			p[x] = s / 3;
			p[x + 256] = ocean + s;
		}
	pat_ocean = ocean;
}

// ---------------------------------------------------------------- кадр

static int16_t sx, sy, sz, a2, r, ra2;                // ra2 — √A2, Q14

// Лестница (globe_sh_s.s sh_ramp): n значений старшего слова v, v += dv (16.16) — младшие байты
// с adr, старшие с adr + hs
extern uint16_t sh_radr, sh_rhs, sh_rn;
extern int32_t sh_rv, sh_rdv;
void sh_ramp(void);
static void ramp(uint16_t off, uint16_t n, uint16_t hs, int32_t v, int32_t dv)
{
	sh_radr = 0xC000 + off; sh_rhs = hs; sh_rn = n; sh_rv = v; sh_rdv = dv;
	sh_ramp();
}

// z точки уровня (со знаком) на E0 при смещении eta (Q14 долей R) и стороне (0 — L): знак
// D·sz·A2 + a·(−sy·η·sz + σ·sx·sgn(sz)·W), W = √(A2 − η²)
// Без корня: z·A2 = P + Q·W (Q14), W ≥ 0 — знаки P и Q, иначе P² против Q²·W² = Q²·(A2 − η²).
static uint8_t visible(int16_t d, int16_t a, int16_t eta, uint8_t side)
{
	int16_t p = SHR14(MUL(SHR14(MUL(d, sz)), a2)) - SHR14(MUL(a, SHR14(MUL(SHR14(MUL(sy, eta)), sz))));
	int16_t q = SHR14(MUL(a, sx));
	if (side) q = -q;
	if (sz < 0) q = -q;
	int32_t w2 = ((int32_t)a2 << 14) - MUL(eta, eta);                  // W², Q28
	if (w2 <= 0 || !q) return p >= 0;
	if ((p >= 0) == (q > 0)) return p >= 0;                             // одного знака
	int32_t pp = MUL(p, p), qq = MUL(SHR14(MUL(q, q)), (int16_t)(w2 >> 14));   // Q28
	return pp > qq ? p >= 0 : q > 0;
}

// Видимые j (строки E0 + 256) стороны. Точки касания края (η в Q14, обе, без разбора по
// сторонам: при терминаторе почти ребром стороны у них не различить) делят сторону на ≤ 3
// участка; видимость участка — по его середине. Узор участков -> [lo, hi] или вне (inv).
static void side_range(uint8_t i, uint8_t side, int16_t d, int16_t a, const int16_t *te, uint8_t nt)
{
	int16_t h = ra2;                                    // √A2 (Q14) — кончики эллипса
	int16_t e[2]; uint8_t n = 0;
	for (uint8_t q = 0; q < nt; q++) if (te[q] > -h && te[q] < h) e[n++] = te[q];
	if (n == 2 && e[0] > e[1]) { int16_t t = e[0]; e[0] = e[1]; e[1] = t; }
	#define J(eta) ((int16_t)(SHR14(MUL(eta, r)) + 256))
	int16_t jl = 0, jh = 511;
	uint8_t inv = 0;
	if (n == 0) {
		if (!visible(d, a, 0, side)) { jl = 1; jh = 0; }
	} else if (n == 1) {
		uint8_t va = visible(d, a, (int16_t)((e[0] - h) >> 1), side), vb = visible(d, a, (int16_t)((e[0] + h) >> 1), side);
		if (va && vb) ;
		else if (!va && !vb) { jl = 1; jh = 0; }
		else if (va) jh = J(e[0]);
		else jl = J(e[0]);
	} else {
		uint8_t va = visible(d, a, (int16_t)((e[0] - h) >> 1), side);
		uint8_t vb = visible(d, a, (int16_t)((e[0] + e[1]) >> 1), side);
		uint8_t vc = visible(d, a, (int16_t)((e[1] + h) >> 1), side);
		switch ((uint8_t)(va << 2 | vb << 1 | vc)) {
		case 7: break;                                      // всё
		case 0: jl = 1; jh = 0; break;                      // ничего
		case 4: jh = J(e[0]); break;                        // до первой
		case 6: jh = J(e[1]); break;                        // до второй
		case 1: jl = J(e[1]); break;                        // после второй
		case 3: jl = J(e[0]); break;                        // после первой
		case 2: jl = J(e[0]); jh = J(e[1]); break;          // между
		default: jl = J(e[0]); jh = J(e[1]); inv = 1; break; // 5: кроме между
		}
	}
	if (jl < 0) jl = 0;
	if (jh > 511) jh = 511;
	if (jl > 511 || jh < 0) { jl = 1; jh = 0; }
	uint8_t *v = (uint8_t *)(0xC000 + LV + 0x70 + (side ? 0x50 : 0)) + i;   // LV_L / LV_R
	v[0] = (uint8_t)jl; v[16] = (uint8_t)(jl >> 8); v[32] = (uint8_t)jh; v[48] = (uint8_t)(jh >> 8); v[64] = inv;
}

// Уровни кадра: накопители индексов E0 (строка 0), шаги, сдвиги x, масштаб, видимость, порядок.
// Возвращает наименьший и наибольший j (строки 0 и 199).
static void levels(uint8_t pair, int16_t *jmin, int16_t *jmax)
{
	const uint8_t *ks = pair ? lev_pair : lev_full;
	uint8_t n = pair ? 5 : 10;
	uint8_t *lv = (uint8_t *)(0xC000 + LV);
	int16_t b2 = SHR14(MUL(sx, sx) + MUL(sy, sy));
	int32_t lo = 0x7FFFFFFF, hi = -0x7FFFFFFF;
	for (uint8_t i = 0; i < n; i++) {
		uint8_t k = ks[i];
		int16_t d = dk14[k], a = ak14[k];
		int16_t inva = ia14[k];                                           // 1/a, Q14
		int16_t step = inva >> 6;                                       // 8.8: 256..259
		// cx: R·D·sx пикселей = R·D·sx/2 пар; 8.8 -> R·D14·sx14 / 2^21; + 64 пары + (−0.25 + 255/256)
		int16_t cx = (int16_t)(SHR14(MUL((int16_t)(MUL(r, d) >> 7), sx)) + 16384 + 191);
		// cy (пиксели) = R·D·sy; acc(y = 0) = ((−99.5 − cy) / a + 256) · 256 + 0.5 (ближайшая строка)
		int32_t cy8 = SHR14(MUL((int16_t)(MUL(r, d) >> 6), sy));          // cy · 256
		int32_t acc = ((((int32_t)-99 * 256 - 128 - cy8) >> 2) * inva >> 12) + 65536 + 128;
		int32_t a1 = acc + (int32_t)step * 199;
		if (acc < lo) lo = acc;
		if (a1 > hi) hi = a1;
		lv[i] = (uint8_t)acc; lv[i + 0x10] = (uint8_t)(acc >> 8); lv[i + 0x20] = (uint8_t)(acc >> 16);
		lv[i + 0x30] = (uint8_t)(step - 256);
		lv[i + 0x40] = (uint8_t)cx; lv[i + 0x50] = (uint8_t)((uint16_t)cx >> 8);
		lv[i + 0x60] = k == 0 ? 7 : k == 9 ? 6 : 0;
		// точки касания края: P = D·(sx, sy)/B2 ± √(B2 − D²)/B2·(−sy, sx); η = (P.y − D·sy) / a
		int16_t te[2]; uint8_t nt = 0;
		int32_t dd = (int32_t)SHR14(MUL(d, d));
		if (b2 > 64 && b2 > dd) {
			int16_t by = (int16_t)((int32_t)d * sy / b2);
			int16_t hh = (int16_t)(((int32_t)isqrt32((uint32_t)(b2 - dd) * 16384) << 14) / b2);
			for (int8_t sg = -1; sg <= 1; sg += 2) {
				int16_t py = by + sg * SHR14(MUL(hh, sx));
				te[nt++] = SHR14(MUL(py - SHR14(MUL(d, sy)), inva));
			}
		}
		side_range(i, 0, d, a, te, nt);
		side_range(i, 1, d, a, te, nt);
	}
	// порядок по вложенности: sz ≥ 0 (ночь по краям строки) — L по убыванию k, R по возрастанию
	uint8_t *ol = (uint8_t *)(0xC000 + LV_ORDL), *orr = (uint8_t *)(0xC000 + LV_ORDR);
	for (uint8_t q = 0; q < n; q++) {
		uint8_t dn = n - 1 - q;
		ol[q] = sz >= 0 ? dn : q;
		orr[q] = sz >= 0 ? q : dn;
	}
	sh_nlev = n;
	*jmin = (int16_t)(lo >> 8);
	*jmax = (int16_t)(hi >> 8) + 1;
}

// Параметры таблицы E0 для j ∈ [j0, j1] (globe_sh_s.s sh_e0): W4 = 2κ·√(H² − v²) (четверти
// пары), QW = W4²·512 = K·(H² − v²), K = 2048κ², κ = |sz|/A2; xc = 128·xs·v (пары 8.8) · 256,
// xs = −sx·sy/A2
static void e0_setup(int16_t j0, int16_t j1)
{
	int16_t v0 = j0 - 256;
	int32_t a2r2 = (MUL(a2, r) >> 4) * r >> 10;                       // A2·R², пикс²
	int32_t kap = ((int32_t)(sz < 0 ? -sz : sz) << 14) / a2;           // κ, Q14 (≤ 4 при A2 ≥ 1/16)
	int32_t kh = kap >> 1;
	int32_t k = (kh * kh) >> 15;                                        // 2048κ²
	sh_qw = k * (a2r2 - (int32_t)v0 * v0);
	sh_dq = -k * (2 * (int32_t)v0 + 1);
	sh_ddq = -2 * k;
	uint16_t w4 = sh_qw > 0 ? isqrt32((uint32_t)sh_qw >> 9) : 0;
	sh_w4 = w4;
	sh_sw = (int32_t)w4 * w4 * 512;
	int32_t xs14 = -MUL(sx, sy) / a2;                                  // Q14
	sh_dxc = xs14 * 2;                                                  // 128·xs·256 = 2·xs14
	sh_xc = sh_dxc * v0;
	sh_j0 = (uint16_t)j0;
	sh_jn = (uint16_t)(j1 - j0 + 1);
}

// Отсев на кадр: сторона уровня, пересечение которой в 9 строках-пробах (через 25) видно,
// есть в E0 и всюду левее или всюду правее окна, — «нет» (inv = 2: проход строк её пропускает)
static void cull(void)
{
	const uint8_t *lv = (const uint8_t *)(0xC000 + LV);
	for (uint8_t i = 0; i < sh_nlev; i++)
		for (uint8_t side = 0; side < 2; side++) {
			uint8_t *v = (uint8_t *)(0xC000 + LV + 0x70 + (side ? 0x50 : 0)) + i;
			uint16_t lo = v[0] | (uint16_t)v[16] << 8, hi = v[32] | (uint16_t)v[48] << 8;
			uint8_t inv = v[64], sc = lv[i + 0x60];
			if (lo > hi && !inv) { v[64] = 2; continue; }          // не видна совсем
			int16_t cx = (int16_t)(lv[i + 0x40] | (uint16_t)lv[i + 0x50] << 8);
			uint32_t acc = lv[i] | (uint32_t)lv[i + 0x10] << 8 | (uint32_t)lv[i + 0x20] << 16;
			uint16_t step = 256 + lv[i + 0x30];
			uint8_t l = 0, rr = 0, n = 0;
			for (uint8_t y = 0; y < 200; y += 25, n++) {
				uint16_t j = (uint16_t)((acc + (uint32_t)step * (y ? y : 0)) >> 8);
				if (j > 511) break;
				uint8_t in = j >= lo && j <= hi;
				if (in == inv) break;                              // проба не видна — не отсекать
				const uint8_t *tb = (const uint8_t *)(0xC000 + SH_TLL + (side ? 0x400 : 0)) + j;
				int16_t t = (int16_t)(tb[0] | (uint16_t)tb[0x200] << 8);
				if (t == (int16_t)0x8000) break;
				if (sc) t -= t >> sc;
				int32_t x = (int32_t)cx + t;
				if (x < 0) l++;
				else if (x >= 32768L) rr++;
				else break;
			}
			if (n == 8 && (l == 8 || rr == 8)) v[64] = 2;
		}
}

static void tables(void)
{
	// t16 = −4000·(sx·X + sy·Y + sz·Z) / (R·16384); X пары p = 2p + 0.5 − 128
	int32_t a = (2040000L / r) * sx, da = -(32000L * sx) / r;
	ramp(SH_TX, 128, 128, a, da);
	sh_txh = HI16(da >> 1);
	int32_t z = 0, dz = -(16000L * sz) / 255;
	ramp(SH_TZ, 256, 256, z, dz);
	// экстремум вдоль строки: sx·X + sz·√(ρ² − X²) = ±A·ρ (sz ≥ 0 — наибольшее, иначе наименьшее)
	int32_t e = 0, de = -(16000L * (sz >= 0 ? ra2 : -ra2)) / 255;
	ramp(SH_TA, 256, 256, e, de);
	sh_ty = (1592000L / r) * sy;                                       // t16 строки 0 · 65536
	sh_dty = -(16000L * sy) / r;
}

uint8_t globe_shadow(uint16_t lon, int16_t lat, uint8_t zoom, uint16_t sunlon, uint8_t ocean) __banked
{
	if (shp == PG_NONE) shp = pg_alloc(1, 1);
	if (shp == PG_NONE) return PG_NONE;
	uint8_t old = pg_map3(shp), oc = pat_ocean;
	if (oc != ocean) patterns(ocean);
	r = zoom_r[zoom];
	// солнце в осях вида: s = (sin d, −sinC·cos d, cosC·cos d), d = λs − λ0
	uint16_t dl = sunlon - lon;
	int16_t sd = sin16(dl), cd = cos16(dl), sc = sin16((uint16_t)lat), cc = cos16((uint16_t)lat);
	sx = sd; sy = -SHR14(MUL(sc, cd)); sz = SHR14(MUL(cc, cd));
	a2 = SHR14(MUL(sx, sx) + MUL(sz, sz));
	ra2 = (int16_t)isqrt32((uint32_t)a2 * 16384);
	uint8_t pair = zoom_pair[zoom];
	tables();
	sh_lut = pair ? sh_lut_pair : sh_lut_full;
	sh_cross = a2 >= 1024;                                             // иначе эллипсы — почти прямые строки
	if (sh_cross) {
		int16_t j0, j1;
		levels(pair, &j0, &j1);
		if (j0 < 0) j0 = 0;
		if (j1 > 511) j1 = 511;
		e0_setup(j0, j1);
		sh_e0();
		cull();
	}
	sh_zi = (uint16_t)zoom * (GLOBE_H * 5);
	sh_dl = sz >= 0 ? -1 : 1;
	sh_top = pair ? 5 : 10;
	sh_base = pair ? 11 : 0;
	sh_limb = zoom < 2;
	sh_page = shp;
	{ int16_t *dg = (int16_t *)(0xC000 + 0x3FF0); dg[0] = sx; dg[1] = sy; dg[2] = sz; }   // отладка: солнце кадра
	sh_rows();
	pg_map3(old);
	return shp;
}
