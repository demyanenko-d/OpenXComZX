// Банк 24: глобус — этап 1 плана project_docs/globe.md (М4в, без тени): плоская карта
// WORLD.DAT (ресурс GLOBE v5, конвертер Core/Globe.cs: границы текстур с текстурами сторон,
// перекрытия и T-стыки разрешены заранее) в ортографической проекции OpenXcom
// (Globe::polarToCart), вывод по строкам отрезками развёрнутых строк узора через DMA.
//
// Узор суши в OpenXcom привязан к экрану (texturedPolygon(…, 0, 0)): пиксель (x, y)
// берёт TEXTURE[y & 31][x & 31]. Поэтому строка r узора t развёрнута в блок 256 байт
// (повтор 8 раз), и отрезок [xs, xe) строки y — это копия байтов блока с смещения xs:
// младший байт адреса источника DMA = x. Блоки — в 8 страницах пула: страница r / 4,
// блок (r & 3) * 14 + t, t = 13 — океан (globe.md §6.5).
//
// Кадр: таблицы произведений на 8 констант вида -> центры ячеек 30° (передняя сторона и
// окно) -> по видимой ячейке: вершины (DMA в рабочую страницу, линейная проекция таблицами,
// globe_s.s gl_project) и рёбра (gl_edges: горизонт, окно, шаг по строкам -> страница
// рёбер) -> строки (gl_rows: список рёбер по x -> отрезки DMA в задний буфер, строки
// экрана 280..479) -> копия на экран.
//
// Проекция (Globe::polarToCart в векторах): X = cosφ cosλ, Y = cosφ sinλ, Z = sinφ (Q14),
// W = X cosλ0 + Y sinλ0: x = 128 + R(Y cosλ0 − X sinλ0), y = 100 + R(cosC·Z − sinC·W),
// z = cosC·W + sinC·Z; x, y — в четвертях пикселя (Q2). Умножение значения v (Q14) на
// константу K — таблицы T_hi[v >> 7] + T_lo[v & 127] (globe_s.s gl_ktab), z — так же, в Q12
// (для отсечения по горизонту; ячейки целиком спереди — без z).
//
// Рабочая страница пула (Win3 на время проекции и рёбер):
//   #0000    таблицы: 5 констант x/y и 3 константы z по 4 страницы (T_hi и T_lo: младшие,
//            старшие байты)
//   #2000    проекции вершин ячейки: x, y (Q2), z (Q12) — 6 байт
//   #2390    блок ячейки из ресурса (одно DMA): векторы вершин, затем рёбра
//   #2AA0    записи рёбер ячейки (globe_s.s) — затем DMA в страницу рёбер
//   #3080    корзины строк (адреса записей в странице рёбер) — в конце DMA туда же
// Страница рёбер — globe_s.s (корзины, записи строк, события края, список, рёбра).
#include <stdint.h>
#include <string.h>
#include "tsconf.h"
#include "memmap.h"
#include "pages.h"
#include "far.h"
#include "res.h"
#include "res_ids.h"
#include "dmabuf.h"
#include "dbg.h"
#include "gfx.h"
#include "state.h"
#include "game.h"
#include "globe.h"
#include "globe_tab.h"

#define RES_OFF   0x2000                // рабочая страница: проекции вершин ячейки (x, y, z — 6 байт)
#define VREC      6
#define VSRC      0x2390                //   блок ячейки (DMA из ресурса): вершины, рёбра (по 6 байт)
#define STG       0x2AA0                //   записи рёбер ячейки (globe_s.s), затем DMA в страницу рёбер
#define WB_BUCKET 0x3080                //   корзины строк (адреса записей в странице рёбер)
#define WB_COV    0x3300                //   покрытие строк рёбрами: разности (e_store)
#define EP_BT     0x0000                // страница рёбер: текстура полосы строк без рёбер (#FE — нет)
#define MAXCV     150                   // вершин / рёбер в ячейке (конвертер: до 144)
#define EP_BUCKET 0x1500                // страница рёбер: корзины (копия из рабочей),
#define EP_SHF    0x1700                //   флаги строк с тенью (globe_shadow),
#define EP_POOL   0x1800                //   записи рёбер
#define EP_ROW    0x0200                // страница рёбер (globe_s.s): записи строк (6 байт),
#define EP_EVB    0x0700                //   события края: текстура ниже, выше, ключи
#define EP_EVKB   0x0900
#define EP_EVKA   0x0A00
#define NCELL     72
#define BACK_Y    280                   // задний буфер: строки экрана 280..479, x 0..255
#define NTEX      13
#define NBLK      14                    // блоков узора на строку: 13 текстур + океан (13)

// 16x16 -> 32 (src/kernel/mul32.s; выражение (int32_t)a * b SDCC часто считает 32x32)
int32_t __mulsint2slong(int16_t a, int16_t b);
#define MUL(a, b) __mulsint2slong((int16_t)(a), (int16_t)(b))
// Сдвиги int32 через старшее слово (сдвиг на 10–18 SDCC делает циклом по битам)
#define HI16(v)   ((int16_t)((uint32_t)(v) >> 16))
#define SHR14(v)  HI16((int32_t)(v) << 2)
#define SHR18(v)  (HI16(v) >> 2)
#define SHR10(v)  HI16((int32_t)(v) << 6)

static const int16_t zoom_r[GLOBE_ZOOMS] = { 90, 120, 180, 280, 450, 720 };   // Globe::setupRadii

static uint8_t work = PG_NONE, epage = PG_NONE, strip_set = 0xFF;
static uint8_t valid, v_zoom = 0xFF, bg_zoom = 0xFF, row_zoom = 0xFF, ocean, v_sun = 0xFF;
static uint8_t geom;                       // страница рёбер держит геометрию вида v_lon/v_lat/v_zoom
static int32_t kc[8];                      // K построенных таблиц произведений (kc_ok — годны)
static uint8_t kc_ok;
static uint16_t v_lon;
static int16_t v_lat, R;
static uint8_t strip = PG_NONE;
static uint8_t row_meta;                   // постоянные поля записей строк заполнены (от зума не зависят)

// Проходы М4в (globe_s.s). Рёбра: gl_ec записей по 6 байт с gl_eb -> записи в странице
// рёбер gl_epg с gl_eptr (корзины строк); проекции вершин ячейки — с gl_res (Win3 = рабочая
// страница gl_wpg); gl_limb — зумы 0–1 (левый край строки — край диска). Строки: gl_gtex —
// текстура, если в окне нет ни одного ребра (сетка 5° ресурса в центре вида).
const uint8_t *gl_eb;
uint8_t gl_ec, gl_wpg, gl_epg, gl_limb, gl_gtex;
uint16_t gl_res, gl_eptr, gl_sptr, gl_nedge;
void gl_edges(void);
void gl_rows(void);
uint8_t gl_nb, gl_bl[32];               // полосы строк без рёбер (gl_bands): число, (y0, y1)
void gl_bands(void);
// Проекция (globe_s.s): gl_pn векторов (по 2 байта на X, Y, Z: v & 127, v >> 7) с gl_pv
// -> {x, y (Q2), z (Q12)} с gl_pd (6 байт)
const uint8_t *gl_pv;
uint8_t *gl_pd;
uint8_t gl_pn, gl_noz;                  // gl_noz = 1 — z не считать (ячейка вся спереди)
void gl_project(void);
// Таблицы произведений (globe_s.s): gl_ktab — на K (int32 = R·c, c в Q14) в 4 страницы
// с gl_kpg (адрес страницы Win3)
int32_t gl_k;
uint8_t gl_kpg;
void gl_ktab(void);

// Синус 16-битного угла (Q14) — src/ui/globe_ui.c, банк 2
#define sin16(a) globe_sin((uint16_t)(a))
#define cos16(a) globe_sin((uint16_t)(a) + 0x4000)

static void dma_wait(void)
{
	while (TS_DMASTATUS & DMASTATUS_ACT)
		;
}

// ---------------------------------------------------------------- подготовка

// DMA напрямую регистрами (после конца прошлого): источник sp:so, приёмник dp:do_ (страница,
// смещение в ней), len — слов в пачке - 1, num — пачек - 1
static void dma_go(uint8_t sp, uint16_t so, uint8_t dp, uint16_t do_, uint8_t len, uint8_t num, uint8_t ctrl)
{
	dma_wait();
	TS_DMASAL = (uint8_t)so; TS_DMASAH = (uint8_t)(so >> 8); TS_DMASAX = sp;
	TS_DMADAL = (uint8_t)do_; TS_DMADAH = (uint8_t)(do_ >> 8); TS_DMADAX = dp;
	TS_DMALEN = len;
	TS_DMANUM = num;
	TS_DMACTRL = ctrl;
}

// Запуск DMA без DMALEN/DMANUM: их пишут один раз на серию одинаковых пачек (сборка узоров)
static void dma_blk(uint8_t sp, uint16_t so, uint8_t dp, uint16_t do_, uint8_t ctrl)
{
	dma_wait();
	TS_DMASAL = (uint8_t)so; TS_DMASAH = (uint8_t)(so >> 8); TS_DMASAX = sp;
	TS_DMADAL = (uint8_t)do_; TS_DMADAH = (uint8_t)(do_ >> 8); TS_DMADAX = dp;
	TS_DMACTRL = ctrl;
}

// Копия bytes (чётно) со страницы sp, смещение so (может уходить за 16 КБ — в следующие
// страницы), в страницу dp со смещением doff, пачками по 512 байт. Лёгкая замена far_copy:
// тот считает 32-битные адреса и ждёт дважды (~2 750 тактов на вызов), здесь — байты и слова
static void far_dma(uint8_t sp, uint16_t so, uint8_t dp, uint16_t doff, uint16_t bytes)
{
	sp += (uint8_t)((uint8_t)(so >> 8) >> 6);
	so &= 0x3FFF;
	while (bytes) {
		uint16_t b = bytes > 512 ? 512 : bytes;
		dma_go(sp, so, dp, doff, (uint8_t)((b >> 1) - 1), 0, DMA_RAM_RAM);
		so += b; doff += b; bytes -= b;
		if (so >= 0x4000) { so -= 0x4000; sp++; }
	}
}

// Узоры набора set (0 — зумы 4–5, 1 — 2–3, 2 — 0–1): в каждом блоке — 32 байта строки узора
// (океан — заливка словом), затем три удвоения сразу по всем 56 блокам страницы (2D DMA,
// пачка на блок, шаг 256): [0, 32) -> [32, 64), [0, 64) -> [64, 128), [0, 128) -> [128, 256).
static void build_strips(far_t tex, uint8_t set)
{
	uint8_t tp = FAR_PAGE(tex);
	uint16_t to = FAR_OFFS(tex) + (uint16_t)set * NTEX * 1024u;   // < 64 КБ: смещение + 39 КБ
	uint16_t fw = (uint16_t)&dma_fill_word;
	dma_wait();
	dma_fill_word = ocean | ((uint16_t)ocean << 8);
	for (uint8_t p = 0; p < 8; p++) {
		uint8_t sp = strip + p;
		for (uint8_t rr = 0; rr < 4; rr++) {
			uint16_t o = to + (uint16_t)((p << 2) | rr) * 32u;
			dma_wait();                       // длину и число пачек писать только после конца передачи
			TS_DMALEN = 15;                   // дальше DMA их не меняет — на серию одинаковых пачек
			TS_DMANUM = 0;
			for (uint8_t t = 0; t < NTEX; t++, o += 1024u)
				dma_blk(tp + (uint8_t)(o >> 14), o & 0x3FFF, sp, (uint16_t)(rr * NBLK + t) << 8, DMA_RAM_RAM);
			dma_blk(DATA_PAGE, fw & 0x3FFF, sp, (uint16_t)(rr * NBLK + NTEX) << 8, DMA_FILL);
		}
		dma_go(sp, 0x0000, sp, 0x0020, 15, 4 * NBLK - 1, DMA_RAM_RAM | DMA_S_ALGN | DMA_D_ALGN);
		dma_go(sp, 0x0000, sp, 0x0040, 31, 4 * NBLK - 1, DMA_RAM_RAM | DMA_S_ALGN | DMA_D_ALGN);
		dma_go(sp, 0x0000, sp, 0x0080, 63, 4 * NBLK - 1, DMA_RAM_RAM | DMA_S_ALGN | DMA_D_ALGN);
	}
	strip_set = set;                          // конца ждать не надо: следующий пользователь DMA ждёт сам
}

// Записи строк страницы рёбер (globe_s.s): диск в парах пикселей (Globe: circle_norm с
// центрами пикселей i + .5, j + .5: пиксель внутри, если (2i + 1 - 256)^2 +
// (2j + 1 - 200)^2 < 4R^2; пара — если внутри хоть один её пиксель), адрес строки
// заднего буфера, блок и страница узоров. Win3 = страница рёбер.
// Постоянные поля записи строки (адрес строки заднего буфера, блок и страница узоров) от
// зума не зависят — заполняются один раз; от зума зависят только пары диска pl, pr.
static void rows_meta(void)
{
	uint8_t *rw = (uint8_t *)(0xC000 + EP_ROW);
	uint8_t dah = (BACK_Y & 31) << 1, dax = SCREEN_PAGE + (BACK_Y >> 5);
	for (uint8_t y = 0; y < GLOBE_H; y++, rw += 6) {
		rw[2] = dah;
		rw[3] = dax;
		rw[4] = (uint8_t)((y & 3) * NBLK);
		rw[5] = strip + (uint8_t)((y & 31) >> 2);
		dah += 2;
		if (dah == 64) { dah = 0; dax++; }    // (row & 31) << 1: 32 строки на страницу
	}
	row_meta = 1;
}

// Фон окна вне диска (зумы 0–1): левые 256 столбцов GEOBORD в задний буфер. Адрес источника —
// 16-битное смещение с переносом в номер страницы (32-битная арифметика far_t в SDCC стоила
// ~1 500 тактов на строку), адрес приёмника — приращением. При увеличении зума не зовётся:
// новый диск накрывает старый, фон вне него уже лежит.
static void bg_restore(void)
{
	res_t r;
	if (!res_find(RES_GEOBORD_SCR, &r)) return;
	uint8_t sp = FAR_PAGE(r.phys);
	uint16_t so = FAR_OFFS(r.phys);
	uint8_t dah = (BACK_Y & 31) << 1, dax = SCREEN_PAGE + (BACK_Y >> 5);
	for (uint8_t y = 0; y < GLOBE_H; y++) {
		dma_go(sp, so, dax, (uint16_t)dah << 8, GLOBE_W / 2 - 1, 0, DMA_RAM_RAM);
		so += r.a;
		if (so >= 0x4000) { so -= 0x4000; sp++; }
		dah += 2;
		if (dah == 64) { dah = 0; dax++; }
	}
	dma_wait();
}

// Таблицы произведений на 8 констант вида (λ0, наклон C, радиус R). Win3 = work.
// x/y: K = R·c (c — Q14), страницы 4k..4k+3: kxX, kxY, kyX, kyY, kyZ; z (Q12): K = k·1024
// (k — Q14), страницы 20..31: kzX, kzY, kzZ.
static void tables(uint16_t lon, int16_t lat)
{
	int16_t cl = cos16(lon), sl = sin16(lon), cc = cos16((uint16_t)lat), sc = sin16((uint16_t)lat);
	int32_t k[8];
	k[0] = MUL(R, -sl);                               // x: -R sinλ0 · X
	k[1] = MUL(R, cl);                                //     R cosλ0 · Y
	k[2] = MUL(R, -SHR14(MUL(sc, cl)));               // y: -R sinC cosλ0 · X
	k[3] = MUL(R, -SHR14(MUL(sc, sl)));               //    -R sinC sinλ0 · Y
	k[4] = MUL(R, cc);                                //     R cosC · Z
	k[5] = (int32_t)SHR14(MUL(cc, cl)) << 10;         // z: cosC cosλ0 · X
	k[6] = (int32_t)SHR14(MUL(cc, sl)) << 10;         //    cosC sinλ0 · Y
	k[7] = (int32_t)sc << 10;                         //    sinC · Z
	// Строятся только таблицы, у которых K изменилось: при повороте по долготе те же K4 и
	// sinC·Z, при повороте по широте — K0, K1 (по 0.13 кадра на таблицу)
	uint8_t ok = kc_ok;
	for (uint8_t i = 0; i < 8; i++) {
		if (ok && kc[i] == k[i]) continue;
		gl_k = k[i];
		gl_kpg = (uint8_t)(0xC0 + i * 4);
		gl_ktab();
		kc[i] = k[i];
	}
	kc_ok = 1;
}

// Полосы строк без рёбер: строка без рёбер — одна область, её текстура — в любой точке строки.
// Точка посередине полосы -> долгота/широта -> сетка 5° конвертера; однородная клетка — текстура
// полосы (надёжнее событий левого края: у края диска вершины почти на горизонте дают точки
// края в пределах единиц Q2 с несогласованным порядком). До 3 точек; иначе #FE (по событиям).
// Win3 = страница рёбер; в EP_BT — разности покрытия (e_store).
static int16_t ev[9];                   // оси экрана в мировых координатах (Q14): e_x, e_y, e_z
static uint16_t kr;                     // 2^21 / R: пиксель -> доля радиуса Q14 (>> 8)

// Пиксель окна -> номер клетки сетки 5° (gy * 72 + gx; #FFFF — вне диска): z — таблицей
// sqrt(1 - r^2), мировой вектор — умножениями на оси вида, широта и долгота — двоичным поиском
// по границам клеток (sin 5k°, tan 5k° перекрёстным умножением)
static uint16_t cell_of(uint8_t x, uint8_t y)
{
	int16_t px = (int16_t)(MUL(2 * x + 1 - GLOBE_W, kr) >> 8), py = (int16_t)(MUL(2 * y + 1 - GLOBE_H, kr) >> 8);
	uint32_t rr = (uint32_t)MUL(px, px) + (uint32_t)MUL(py, py);
	if (rr >= 0x10000000ul) return 0xFFFF;
	uint8_t i = (uint8_t)(rr >> 20), f = (uint8_t)(rr >> 12);
	int16_t pz = sqz_q14[i] - (int16_t)((MUL(sqz_q14[i] - sqz_q14[i + 1], f)) >> 8);
	int16_t q0 = SHR14(MUL(px, ev[0]) + MUL(py, ev[3]) + MUL(pz, ev[6]));
	int16_t q1 = SHR14(MUL(px, ev[1]) + MUL(py, ev[4]) + MUL(pz, ev[7]));
	int16_t q2 = SHR14(MUL(py, ev[5]) + MUL(pz, ev[8]));
	uint8_t lo = 0, hi = 36, m;                // широта: число границ sin(5k - 90°) <= q2
	while (hi - lo > 1) { m = (lo + hi) >> 1; if (sin5_q14[m] <= q2) lo = m; else hi = m; }
	uint16_t g = (uint16_t)lo * 72;
	int16_t ax = q0 < 0 ? -q0 : q0, ay = q1 < 0 ? -q1 : q1;
	lo = 0; hi = 18;                           // угол в четверти: число k с 5k° <= atan(ay / ax)
	while (hi - lo > 1) { m = (lo + hi) >> 1; if (MUL(ay, cos5k_q14[m]) >= MUL(ax, sin5k_q14[m])) lo = m; else hi = m; }
	if (q0 < 0) lo = q1 >= 0 ? 35 - lo : 36 + lo;
	else if (q1 < 0) lo = 71 - lo;
	return g + lo;
}

// Полосы строк без рёбер (_gl_bands: #FD в EP_BT, список _gl_bl) -> текстура точки посередине
// (до 3 точек, пока клетка не однородна) или #FE. Win3 = страница рёбер.
static void bands(far_t gt, uint16_t lon, int16_t lat)
{
	gl_bands();
	if (!gl_nb) return;
	int16_t cl = cos16(lon), sl = sin16(lon), cc = cos16((uint16_t)lat), sc = sin16((uint16_t)lat);
	ev[0] = -sl; ev[1] = cl; ev[2] = 0;
	ev[3] = -SHR14(MUL(sc, cl)); ev[4] = -SHR14(MUL(sc, sl)); ev[5] = cc;
	ev[6] = SHR14(MUL(cc, cl)); ev[7] = SHR14(MUL(cc, sl)); ev[8] = sc;
	kr = (uint16_t)(0x200000ul / R);
	for (uint8_t n = 0; n < gl_nb; n++) {
		uint8_t y0 = gl_bl[n * 2], y1 = gl_bl[n * 2 + 1], ym = (uint8_t)((y0 + y1) >> 1);
		const uint8_t *q = (const uint8_t *)(0xC000 + EP_ROW) + (uint16_t)ym * 6;
		uint8_t a = q[0], b = q[1], t = 0xFE, p[3];
		p[0] = (uint8_t)((a + b) >> 1); p[1] = a + ((b - a) >> 2); p[2] = b - ((b - a) >> 2);
		for (uint8_t k = 0; k < 3 && t == 0xFE; k++) {
			uint16_t c = cell_of(p[k] << 1, ym);
			if (c != 0xFFFF) t = far_byte(gt + c);
		}
		memset((void *)(0xC000 + EP_BT + y0), t, y1 - y0 + 1);
	}
}

// ---------------------------------------------------------------- кадр

extern volatile uint16_t frames;

static void render(uint16_t lon, int16_t lat, uint8_t z, uint16_t sun)
{
	res_t rg, rt;
	uint16_t h[3];
	if (!res_find(RES_GLOBE, &rg) || !res_find(RES_TEXTURE_DAT, &rt)) return;
	far_read(rg.phys, h, 6);                   // nCell, nVert, nEdge
	far_t ct = rg.phys + 6, bt = ct + h[0] * 16u, gt = bt + (h[1] + h[2]) * 6u;
	if (h[0] > NCELL) { dbg_puts("globe: bad GLOBE\n"); return; }
	uint16_t t0 = frames;
	uint8_t set = 2 - (z >> 1), ss = strip_set, bz = bg_zoom, rz = row_zoom;   // копии: SDCC и сравнение с глобальной
	R = zoom_r[z];
	ocean = res_game() == 2 ? 16 : 192;       // globe.rul oceanPalette: TFTD 1, UFO 12
	if (set != ss) build_strips(rt.phys, set);
	// Вид тот же (перерисовка по эпохе солнца): геометрия в странице рёбер годна — проход строк
	// её только читает (записи, корзины, события края, полосы, строки), а слоты и порядок
	// заводит сам. Считаем заново лишь тень (globe_sh.c) и проход строк.
	uint8_t vz = v_zoom;                      // копии: SDCC портит сравнение байта с глобальной
	uint16_t vlon = v_lon;
	int16_t vlat = v_lat;
	if (geom && z == vz && lon == vlon && lat == vlat) {
		pg_map3(epage);
		uint8_t sp2 = globe_shadow(lon, lat, z, sun, ocean);
		if (sp2 != PG_NONE) far_copy(FAR(epage, EP_SHF), FAR(sp2, GLOBE_SH_FLG), GLOBE_H);
		else far_fill(FAR(epage, EP_SHF), 0, GLOBE_H);
		gl_rows();
		pg_map3(work);
		dma_wait();
		dbg_puts("globe: sun only, zoom "); dbg_dec(z);
		dbg_puts(", frames "); dbg_dec((uint16_t)(frames - t0));
		dbg_puts(", sun "); dbg_dec(sun); dbg_puts("\n");
		return;
	}
	pg_map3(epage);                            // страница рёбер: строки (при смене зума), события
	if (!row_meta) rows_meta();
	if (z != rz) { globe_rows_pl(z, (uint8_t *)(0xC000 + EP_ROW)); row_zoom = z; }   // пары диска — таблицей
	memset((void *)(0xC000 + EP_EVB), 0xFE, 0x200);
	memset((void *)(0xC000 + EP_EVKB), 0, 0x100);
	memset((void *)(0xC000 + EP_EVKA), 0xFF, 0x100);
	gl_eptr = 0xC000 + EP_POOL;
	gl_nedge = 0;
	pg_map3(work);
	memset((void *)(0xC000 + WB_BUCKET), 0, GLOBE_H * 2);
	memset((void *)(0xC000 + WB_COV), 0, 256);
	if (z >= 2) bg_zoom = 0xFF;                // диск закрывает окно — фон затёрт
	else if (bz > z) { bg_restore(); bg_zoom = z; }   // диск вырос — фон вне него и так на месте
	else bg_zoom = z;
	v_lon = lon; v_lat = lat; v_zoom = z;
	tables(lon, lat);
	// центры ячеек — той же проекцией (x, y в Q2, z в Q6)
	uint16_t cr[NCELL][8];                    // блок, vn, en, 0, центр (6 байт), sinRho
	uint8_t cv[NCELL][6];
	uint8_t cp[NCELL][VREC];                  // x, y (Q2), z (Q12)
	far_read(ct, cr, h[0] * 16u);
	for (uint8_t c = 0; c < h[0]; c++) memcpy(cv[c], &cr[c][4], 6);
	gl_pv = &cv[0][0]; gl_pd = &cp[0][0]; gl_pn = (uint8_t)h[0]; gl_noz = 0;
	gl_project();
	gl_limb = z < 2;
	gl_wpg = work; gl_epg = epage;
	gl_res = 0xC000 + RES_OFF;
	uint8_t ncells = 0;
	uint8_t bt_pg = FAR_PAGE(bt);                   // страница и смещение блоков ячеек — один раз
	uint16_t bt_of = FAR_OFFS(bt);
	for (uint8_t c = 0; c < h[0]; c++) {
		uint16_t vn = cr[c][1], en = cr[c][2];
		if (!en) continue;
		int16_t sr = (int16_t)cr[c][7];
		int16_t zc = (int16_t)(cp[c][4] | (cp[c][5] << 8)), sr4 = (int16_t)cr[c][7] >> 2;
		if (sr < 16384) {
			const uint8_t *q = cp[c];
			int16_t t = zc + sr4;                  // вся ячейка на задней стороне: z центра + sinρ < -1/64 (Q12)
			if (t < -64) continue;
			// окно: проекция не длиннее хорды (2R·sin(ρ/2) <= 1.1·R·sinρ при ρ < 49°)
			int16_t m = SHR14(MUL(R, sr));
			int16_t xc = (int16_t)(q[0] | (q[1] << 8)) >> 2, yc = (int16_t)(q[2] | (q[3] << 8)) >> 2;
			m += (m >> 3) + 2;
			if (xc + m < 0 || xc - m >= GLOBE_W || yc + m < 0 || yc - m >= GLOBE_H) continue;
		}
		if (vn > MAXCV || en > MAXCV) { dbg_puts("globe: cell too big\n"); continue; }
		if (gl_eptr > 0xC000 + 0x4000 - 10 * en) { dbg_puts("globe: edge pool full\n"); break; }
		ncells++;
		// вся ячейка на передней стороне — z вершин не нужен
		gl_noz = sr < 16384 && (int16_t)(zc - sr4) > 64;
		far_dma(bt_pg, bt_of + cr[c][0], work, VSRC, (vn + en) * 6u);   // блок ячейки: вершины, рёбра
		dma_wait();
		gl_pv = (const uint8_t *)(0xC000 + VSRC); gl_pd = (uint8_t *)(0xC000 + RES_OFF); gl_pn = (uint8_t)vn;
		gl_project();
		gl_eb = (const uint8_t *)(0xC000 + VSRC) + vn * 6u; gl_ec = (uint8_t)en;
		uint16_t e0 = gl_eptr;
		gl_sptr = 0xC000 + STG;
		gl_edges();
		if (gl_eptr != e0)                              // записи ячейки -> страница рёбер
			far_dma(work, STG, epage, e0 - 0xC000, gl_eptr - e0);
	}
	// сетка 5° (72 x 36 от λ = 0, φ = −90°) в центре вида — если в окне нет рёбер
	uint8_t gx = (uint8_t)(((uint32_t)lon * 72) >> 16), gy = (uint8_t)(((uint32_t)(lat + 16384) * 36) >> 15);
	if (gy > 35) gy = 35;
	uint8_t g = far_byte(gt + (uint16_t)gy * 72 + gx);
	gl_gtex = g == 0xFE ? NTEX : g;
	// корзины и рёбра -> страница рёбер (те же смещения); строки: активные рёбра -> отрезки
	// DMA в задний буфер
	far_copy(FAR(epage, EP_BUCKET), FAR(work, WB_BUCKET), GLOBE_H * 2);
	far_copy(FAR(epage, EP_BT), FAR(work, WB_COV), 256);
	pg_map3(epage);
	bands(gt, lon, lat);
	// тень (банк 25): подкладка суши и цвет океана строк заднего буфера, флаги строк
	uint8_t sp = globe_shadow(lon, lat, z, sun, ocean);
	if (sp != PG_NONE) far_copy(FAR(epage, EP_SHF), FAR(sp, GLOBE_SH_FLG), GLOBE_H);
	else far_fill(FAR(epage, EP_SHF), 0, GLOBE_H);
	gl_rows();
	pg_map3(work);
	dma_wait();
	valid = 1;
	geom = 1;
	dbg_puts("globe: zoom "); dbg_dec(z);
	dbg_puts(", cells "); dbg_dec(ncells);
	dbg_puts(", edges "); dbg_dec(gl_nedge);
	dbg_puts(", frames "); dbg_dec((uint16_t)(frames - t0));
	dbg_puts(", view "); dbg_dec(lon); dbg_puts(" "); dbg_dec((uint16_t)lat);
	dbg_puts(", sun "); dbg_dec(sun); dbg_puts(", shp "); dbg_dec(sp);
	dbg_puts("\n");
}

// Задний буфер -> окно глобуса на экране (2D DMA, строки по 512). Старт — сразу после
// кадрового прерывания (VSINT = 0, луч в начале картинки): копия идёт ~1.35 строки на строку
// луча и остаётся впереди него, иначе виден разрыв поперёк глобуса.
static void blit(void)
{
	dma_wait();
	uint16_t f0 = frames;
	while (frames == f0)
		;
	TS_DMASAL = 0; TS_DMASAH = (BACK_Y & 31) << 1; TS_DMASAX = SCREEN_PAGE + (BACK_Y >> 5);
	TS_DMADAL = 0; TS_DMADAH = 0; TS_DMADAX = SCREEN_PAGE;
	TS_DMALEN = 127;
	TS_DMANUM = (uint8_t)(GLOBE_H - 1);
	TS_DMACTRL = DMA_RAM_RAM | DMA_S_ALGN | DMA_D_ALGN | DMA_ASZ;
	dma_wait();
}

void globe_invalidate(void) __banked
{
	valid = 0;
	geom = 0;
	kc_ok = 0;
	row_meta = 0;
	bg_zoom = 0xFF;
	v_zoom = 0xFF;
	row_zoom = 0xFF;
	v_sun = 0xFF;
}

void globe_draw(void) __banked
{
	uint8_t z = ST->zoom;
	uint16_t lon = ctx.globe_lon;
	int16_t lat = ctx.globe_lat;
	if (z >= GLOBE_ZOOMS) z = GLOBE_ZOOMS - 1;
	if (work == PG_NONE) work = pg_alloc(1, 1);
	if (strip == PG_NONE) strip = pg_alloc(8, 1);
	if (epage == PG_NONE) epage = pg_alloc(1, 1);
	if (work == PG_NONE || strip == PG_NONE || epage == PG_NONE) { dbg_puts("globe: no pages\n"); return; }
	uint16_t sun = globe_sunlon();                // ST ещё в Win3
	uint8_t old = pg_win3();
	uint8_t vz = v_zoom, se = (uint8_t)(sun >> 7), vs = v_sun;   // эпоха солнца: 0.7°
	if (!valid || z != vz || lon != v_lon || lat != v_lat || se != vs) { render(lon, lat, z, sun); v_sun = se; }
	blit();
	pg_map3(old);
}
