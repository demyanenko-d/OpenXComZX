// Экран наземного боя: первая реализация отрисовки (16_battlescape_plan.md §8, этап Б1).
// Карта и её тайлсет — готовые ресурсы BATMAP0..2 / BATTILE0..2 из BATTLE.PAK (генератора
// карт ещё нет, поле замостил конвертер: OxzConv/Core/Battle.cs). Рисуется всё подряд, без
// отсева по перекрытию: прототип показал, что отсев не окупается (§8.3a).
//
// Геометрия (Camera::convertMapToScreen): X = (x - y) * 16, Y = (x + y) * 8 - z * 24; спрайт
// клетки 32x40, вертикальное смещение части — MCD.P_Level. Порядок художника: уровни снизу
// вверх, внутри уровня — ряды Y, внутри ряда X (перекрывающие клетки идут позже), внутри
// клетки — пол, западная стена, северная стена, объект.
//
// Управление: стрелки — камера, Q/A — этаж, M — следующая карта, ESC — выход.
#include <stdint.h>
#include <string.h>
#include "tsconf.h"
#include "memmap.h"
#include "res.h"
#include "res_ids.h"
#include "ui.h"
#include "gfx.h"
#include "input.h"
#include "far.h"
#include "pages.h"
#include "scrdef.h"

#define VIEW_H     144               // окно карты; ниже — панель ICONS (56 строк)
#define TILE_W     32
#define TILE_H     40
#define MAP_MAXX   64                // предел ширины карты (буфер ряда)
#define NMAPS      3

static uint8_t cur_map;              // какая карта показывается (по очереди при входе)
static uint8_t m_sx, m_sy, m_sz, m_nt;
static uint16_t tiles_res;           // SPRSET тайлов этой карты
static uint8_t level;                // этаж камеры
static int16_t cam_x, cam_y;         // начало координат карты на экране
static int16_t base_x, base_y;       // для какого положения камеры построен буфер
static int16_t vx_lo, vx_hi;         // мировой диапазон X, который сейчас нарисован в буфере
static uint8_t __at(0xBC00) tile_y[256];   // вертикальное смещение тайла (MCD.P_Level) — память банка
static far_t cells;                  // начало клеток карты
static far_t map_phys;               // где лежит ресурс карты (слот SD мог смениться)
static uint8_t loaded;
static uint8_t __at(0xBD00) row[MAP_MAXX * 4];   // ряд клеток карты (Win1 занято, стек в Win0 мал)
// Кадры тайлсета в готовом для блита виде (17_battle_render.md §2): при загрузке карты записи
// SPRSET (x, y, w, h, смещение/2) разворачиваются так, чтобы в кадре не осталось ни 32-битной
// арифметики far_t, ни делений — только сложения.
//   [0] страница источника, [1..2] смещение в ней, [3] DMALEN (w/2-1), [4] высота,
//   [5] dx, [6] dy (угол кадра в ячейке), [7] ширина
#define TE_SIZE 8
static uint8_t __at(0xB400) tile_tab[256 * TE_SIZE];
static far_t tiles_phys;             // где сейчас лежит тайлсет (слот SD мог смениться)
static uint16_t tile_frames;

// Дисплей-лист (17_battle_render.md §2): вид раскладывается в список готовых команд DMA —
// по 8 байт, ровно то, что выгружается в регистры. Строится при смене камеры, этажа или карты,
// а перерисовка становится циклом «прочитал 8 байт — записал 8 портов».
#define DL_MAX  2048
#define DL_SIZE 8
static uint8_t *const dl = (uint8_t *)0xC000;   // страница dl_page, подключается в Win3
static uint8_t dl_page = PG_NONE;
static uint16_t dl_n;                // записей в списке
static uint8_t dl_ok;                // список отвечает текущему виду

// Сплит-экран (17_battle_render.md §3): карта живёт в строках холста BUF_Y.. и показывается
// смещением GYOffs, а панель ICONS остаётся на своём месте — на строке VIEW_H-1 строчное
// прерывание (crt0.s) переключает смещения на панельные. Так камеру можно двигать регистрами,
// не перерисовывая ни карту, ни панель.
// GYOffs — не постоянное смещение, а значение, которым перезагружается счётчик строки
// видеопамяти; дальше он растёт сам. Поэтому на разрезе ставится номер строки панели, а
// смещения карты возвращаются за SPLIT_LEAD строк до картинки (запас на длинный обработчик
// кадра со счётчиком и вводом) — с поправкой на эти строки.
#define SPLIT_TOP  73                // строк верхнего бордюра: VSInt считает их с картинкой
#define SPLIT_LEAD 0                 // за сколько строк до картинки вернуть смещения карты
#define BUF_Y 200                    // первая строка холста под карту (видимое — 0..199)
// Карта строится с запасом по краям: буфер 512x256 (строка холста — 512 байт), окно 320x144
// показывается внутри него. Пока камера не вышла за запас, её движение — это только запись
// GXOffs/GYOffs, без единого блита.
#define BUF_W   512
#define BUF_H   256
#define PAD_X   80                   // запас по X: 320 + 2*80 = 480 < 512, кольцо не перекрывается
#define PAD_Y   ((BUF_H - VIEW_H) / 2)       // 56
#define BIAS    1024                 // мировая X лежит в ±640; сдвиг делает её положительной
extern uint8_t split_gx, split_gy, split_on, split_phase;
extern uint16_t map_gx, map_gy, split_line, vblank_line;

// Адрес начала строки карты в холсте: смещение в странице и сама страница. Считать это в сборке
// списка (сдвиги 16-битных величин) дороже, чем прочитать из таблицы.
static uint16_t __at(0xB000) row_ofs[BUF_H];
static uint8_t __at(0xB200) row_page[BUF_H];

static void rows_init(void)
{
	for (uint16_t y = 0; y < BUF_H; y++) {
		uint16_t b = y + BUF_Y;
		row_ofs[y] = (uint16_t)(b & 31) << 9;
		row_page[y] = SCREEN_PAGE + (uint8_t)(b >> 5);
	}
}

// Включить сплит: карта видна со строки BUF_Y холста, панель — со своей строки 144
static void split_start(void)
{
	map_gx = PAD_X;
	map_gy = BUF_Y + PAD_Y - SPLIT_LEAD;   // счётчик дорастёт до нужной строки к картинке
	split_gx = 0;
	split_gy = VIEW_H;                   // после разреза показываются строки панели (144..199)
	split_line = SPLIT_TOP + VIEW_H - 1;
	vblank_line = SPLIT_TOP - 1 - SPLIT_LEAD;
	split_phase = 0;
	split_on = 1;
	TS_VSINTL = (uint8_t)split_line;
	TS_VSINTH = (uint8_t)(split_line >> 8);
}

static void split_stop(void)
{
	split_on = 0;
	TS_VSINTL = 0;                       // кадровое прерывание — снова в начале кадра
	TS_VSINTH = 0;
	TS_GXOFFSL = 0; TS_GXOFFSH = 0;
	TS_GYOFFSL = 0; TS_GYOFFSH = 0;
}

static void dma_wait(void)
{
	while (TS_DMASTATUS & DMASTATUS_ACT)
		;
}

// Очистить буфер карты (512x256 = 128 КБ) одним DMA FILL: он читает слово и повторяет его,
// поэтому стоит втрое дешевле переноса. gfx_fill тут не годится — он режет по видимому экрану.
static void fill_buf(void)
{
	far_t base = FAR(SCREEN_PAGE + (BUF_Y >> 5), (uint16_t)(BUF_Y & 31) << 9);
	uint16_t zero = 0;
	far_write(base, &zero, 2);
	uint16_t so = FAR_OFFS(base) + 2;
	uint8_t sp = FAR_PAGE(base);
	dma_wait();
	TS_DMASAL = (uint8_t)FAR_OFFS(base); TS_DMASAH = (uint8_t)(FAR_OFFS(base) >> 8);
	TS_DMASAX = FAR_PAGE(base);
	TS_DMADAL = (uint8_t)so; TS_DMADAH = (uint8_t)(so >> 8); TS_DMADAX = sp;
	TS_DMALEN = 255;                     // 256 слов = 512 байт в пачке
	TS_DMANUM = 255;                     // 256 пачек = 256 строк буфера
	TS_DMACTRL = DMA_FILL;
	dma_wait();
}

// Прогон списка: на запись — ожидание DMA и восемь выгрузок регистров
static void dl_run(void)
{
	uint8_t old = pg_map3(dl_page);
	const uint8_t *p = dl;
	for (uint16_t i = dl_n; i; i--, p += DL_SIZE) {
		dma_wait();
		TS_DMASAL = p[0]; TS_DMASAH = p[1]; TS_DMASAX = p[2];
		TS_DMADAL = p[3]; TS_DMADAH = p[4]; TS_DMADAX = p[5];
		TS_DMALEN = p[6];
		TS_DMANUM = p[7];
		TS_DMACTRL = DMA_BLT1 | DMA_D_ALGN | DMA_ASZ;
	}
	pg_map3(old);
}

// Вызывается, когда в Win3 уже подключена страница списка (build_list переключает по рядам)
static uint8_t *dl_put(void)
{
	if (dl_n >= DL_MAX) return 0;
	return dl + dl_n++ * DL_SIZE;
}

// Кадр тайлсета -> команды дисплей-листа: источник лежит подряд (строка = w байт), приёмник
// шагает на 512 (BLT1 | D_ALGN | ASZ, 02 §6). Кадр — всегда одна команда; по краям окна он не
// обрезается (см. ниже), сверху и снизу обрезается строками.
static void dl_tile(uint8_t t, int16_t x, int16_t y)
{
	const uint8_t *e = tile_tab + (uint16_t)t * TE_SIZE;
	int16_t h = e[4];
	if (!e[7]) return;
	int16_t bx = (int16_t)((uint16_t)(x + e[5] + BIAS) & (BUF_W - 1)), by = y + e[6];
	uint8_t sp = e[0];
	uint16_t so = (uint16_t)e[1] | ((uint16_t)e[2] << 8);
	uint8_t len = e[3];
	int16_t w = e[7];
	if (bx & 1) return;                  // DMA адресует словами; cam_x и dx кадра всегда чётные
	if (by <= -TILE_H || by >= BUF_H) return;
	// Быстрый путь: кадр целиком внутри буфера — обрезать нечего, одна команда на кадр
	if (by >= 0 && by + h <= BUF_H && bx >= 0 && bx + w <= BUF_W) {
		uint8_t *q = dl_put();
		if (!q) return;
		uint16_t o = row_ofs[by] + (uint16_t)bx;
		q[0] = (uint8_t)so; q[1] = (uint8_t)(so >> 8); q[2] = sp;
		q[3] = (uint8_t)o; q[4] = (uint8_t)(o >> 8); q[5] = row_page[by];
		q[6] = len;
		q[7] = (uint8_t)(h - 1);
		return;
	}
	if (by < 0) {                        // верх кадра выше буфера — пропустить строки
		so += (uint16_t)(-by) * (uint16_t)w;
		h += by;
		by = 0;
	}
	if (by + h > BUF_H) h = BUF_H - by;
	if (h <= 0) return;
	// Кадр лёг на стык кольца: левая часть идёт в конец строки буфера, правая — в начало.
	// Строки при этом короче шага источника, поэтому команда на строку (таких кадров мало —
	// только те, что попали на стык).
	uint16_t step = e[7];
	uint16_t w1 = (uint16_t)(BUF_W - bx) & ~1;       // сколько влезает до конца строки
	uint16_t w2 = ((uint16_t)w - w1) & ~1;           // остаток — в начало строки
	while (so >= 0x4000) { so -= 0x4000; sp++; }
	while (h > 0) {
		uint8_t *d;
		if (w1) {
			d = dl_put();
			if (!d) return;
			uint16_t offs = row_ofs[by] + (uint16_t)bx;
			d[0] = (uint8_t)so; d[1] = (uint8_t)(so >> 8); d[2] = sp;
			d[3] = (uint8_t)offs; d[4] = (uint8_t)(offs >> 8); d[5] = row_page[by];
			d[6] = (uint8_t)(w1 / 2 - 1);
			d[7] = 0;
		}
		if (w2) {
			uint16_t so2 = so + w1;
			uint8_t sp2 = sp;
			if (so2 >= 0x4000) { so2 -= 0x4000; sp2++; }
			d = dl_put();
			if (!d) return;
			uint16_t offs = row_ofs[by];
			d[0] = (uint8_t)so2; d[1] = (uint8_t)(so2 >> 8); d[2] = sp2;
			d[3] = (uint8_t)offs; d[4] = (uint8_t)(offs >> 8); d[5] = row_page[by];
			d[6] = (uint8_t)(w2 / 2 - 1);
			d[7] = 0;
		}
		so += step;
		if (so >= 0x4000) { so -= 0x4000; sp++; }
		by++;
		h--;
	}
}

static const wdef_t w_battle[] = {
	CUS(0, 0, 320, 200, DYN(0), A_NONE, 0),        // всё рисует экран (EVT_DRAW)
	HOT(0, 144, 320, 56, A_POP, 0, ESC),           // панель: пока только выход
};

static const scr_t tab[] = {
	SCR(SCR_BATTLE, UI_SCR_SAVEMENUS, RES_PAL_BATTLESCAPE, 0, SF_RAWPAL, w_battle),
};

// Камера не должна уводить карту из окна (иначе вид пустой, а перерисовка всё равно идёт):
// крайние клетки дают X от -(sy-1)*16 до (sx-1)*16 и Y от -level*24 до (sx+sy-2)*8.
static void clamp_cam(void)
{
	int16_t lo = TILE_W - (int16_t)(m_sx - 1) * 16, hi = SCREEN_W - TILE_W + (int16_t)(m_sy - 1) * 16;
	if (cam_x < lo) cam_x = lo;
	if (cam_x > hi) cam_x = hi;
	lo = TILE_H - (int16_t)(m_sx + m_sy - 2) * 8;
	hi = VIEW_H - TILE_H + (int16_t)level * 24;
	if (cam_y < lo) cam_y = lo;
	if (cam_y > hi) cam_y = hi;
	cam_x &= ~1;                         // блит DMA адресует словами
	// по горизонтали буфер кольцевой (дорисовывается полосами), пересборка нужна только
	// при уходе по вертикали за запас
	if (cam_y - base_y > PAD_Y || base_y - cam_y > PAD_Y) dl_ok = 0;
}

// Камера в центр карты: середина поля попадает в середину окна
static void center(void)
{
	int16_t cx = m_sx / 2, cy = m_sy / 2;
	cam_x = 160 - (cx - cy) * 16;
	cam_y = VIEW_H / 2 - ((cx + cy) * 8 - (int16_t)level * 24);
	dl_ok = 0;
	clamp_cam();
}

static uint8_t load_map(void)
{
	res_t r;
	uint8_t h[8];
	loaded = 0;
	if (!res_find((uint16_t)(RES_BATMAP0 + cur_map), &r)) return 0;
	far_read(r.phys, h, 8);
	m_sx = h[0]; m_sy = h[1]; m_sz = h[2]; m_nt = h[3];
	tiles_res = (uint16_t)h[4] | ((uint16_t)h[5] << 8);
	if (!m_sx || m_sx > MAP_MAXX || !m_sy || !m_sz) return 0;
	for (uint16_t i = 0; i < m_nt; i++) {
		uint8_t t[4];
		far_read(r.phys + 8 + (uint32_t)i * 4, t, 4);
		tile_y[i] = t[0];
	}
	rows_init();
	map_phys = r.phys;
	cells = r.phys + 8 + (uint32_t)m_nt * 4;
	level = m_sz > 1 ? 1 : 0;
	center();
	loaded = 1;
	return 1;
}

// Тайлсет карты: записи SPRSET разворачиваются в готовые для блита (см. tile_tab). Делается
// один раз при загрузке карты и после переезда ресурса в другой слот SD.
static uint8_t load_tiles(void)
{
	res_t rs;
	if (!res_find(tiles_res, &rs)) return 0;
	if (rs.phys == tiles_phys) return 1;
	dl_ok = 0;
	uint8_t hh[4];
	far_read(rs.phys, hh, 4);
	tile_frames = (uint16_t)hh[0] | ((uint16_t)hh[1] << 8);
	uint16_t n = tile_frames > 256 ? 256 : tile_frames;
	far_t data = rs.phys + 4 + (uint32_t)tile_frames * 6;   // данные кадров за таблицей
	for (uint16_t i = 0; i < n; i++) {
		uint8_t e[6];
		far_read(rs.phys + 4 + (uint32_t)i * 6, e, 6);
		far_t src = data + ((uint32_t)((uint16_t)e[4] | ((uint16_t)e[5] << 8)) << 1);
		uint8_t *p = tile_tab + i * TE_SIZE;
		uint16_t so = FAR_OFFS(src);
		p[0] = FAR_PAGE(src);
		p[1] = (uint8_t)so; p[2] = (uint8_t)(so >> 8);
		p[3] = e[2] ? (uint8_t)(e[2] / 2 - 1) : 0;
		p[4] = e[3];
		p[5] = e[0]; p[6] = e[1];
		p[7] = e[2];
	}
	tiles_phys = rs.phys;
	return 1;
}

// Разложить вид в дисплей-лист: уровни снизу вверх, ряды Y, внутри ряда X, внутри клетки —
// пол, западная стена, северная стена, объект (порядок художника, Map.cpp:626-966)
// Разложить в список клетки, чья мировая экранная координата X попадает в [lo, hi]. Мировая X
// клетки — (x - y) * 16, от камеры не зависит; в буфере ей отвечает колонка (X + BIAS) & 511,
// то есть буфер по горизонтали кольцевой и при движении камеры ничего не переезжает — надо лишь
// дорисовать въехавшую полосу. Полоса берётся с запасом в ширину спрайта: клетка рисуется
// целиком, поэтому в неё должны попасть и соседи, чьи спрайты в неё заезжают.
static void build_range(int16_t lo, int16_t hi)
{
	dl_n = 0;
	for (uint8_t z = 0; z <= level; z++)
		for (uint8_t y = 0; y < m_sy; y++) {
			int16_t ry = (int16_t)y * 8 - (int16_t)z * 24 + base_y + PAD_Y;
			int16_t rx = -(int16_t)y * 16;   // мировая X клетки x этого ряда: rx + x*16
			// Отрезок ряда: из lo-32 < wx <= hi и -40 < py < BUF_H получаем границы x
			int16_t x0 = (lo - TILE_W + 1 - rx + 15) >> 4, x1 = (hi - rx) >> 4;
			int16_t t0 = (-TILE_H + 1 - ry + 7) >> 3, t1 = (BUF_H - 1 - ry) >> 3;
			if (t0 > x0) x0 = t0;
			if (t1 < x1) x1 = t1;
			if (x0 < 0) x0 = 0;
			if (x1 >= m_sx) x1 = m_sx - 1;
			if (x0 > x1) continue;
			far_read(cells + (((uint32_t)z * m_sy + y) * m_sx + x0) * 4, row, (uint16_t)(x1 - x0 + 1) * 4);
			uint8_t old = pg_map3(dl_page);   // дальше пишем команды в страницу списка
			const uint8_t *p = row;
			int16_t px = rx + x0 * 16, py = ry + x0 * 8;
			for (int16_t x = x0; x <= x1; x++, p += 4, px += 16, py += 8)
				for (uint8_t k = 0; k < 4; k++) {
					uint8_t t = p[k];
					if (!t) continue;
					dl_tile((uint8_t)(t - 1), px, py - (int16_t)tile_y[t - 1]);   // px — мировая X
				}
			pg_map3(old);
		}
}

static void build_list(void)
{
	base_x = cam_x;                      // вид построен для этого положения камеры
	base_y = cam_y;
	build_range(-cam_x - PAD_X, -cam_x + SCREEN_W - 1 + PAD_X);
	dl_ok = 1;
}

// Сначала собирается список (старая картинка ещё на экране), и только потом экран гасится и
// рисуется заново — иначе чёрный экран висит всё время сборки, а она втрое дольше отрисовки.
// Камера внутри буфера: показать нужное место одной парой регистров
static void show_cam(void)
{
	map_gx = (uint16_t)(BIAS - cam_x) & (BUF_W - 1);
	map_gy = (uint16_t)(BUF_Y + PAD_Y - (cam_y - base_y) - SPLIT_LEAD) & 511;
}

// Очистить колонки кольца, отвечающие мировому диапазону [lo, hi] (полоса на всю высоту буфера).
// На завороте кольца получается два куска.
static void fill_cols(int16_t lo, int16_t hi)
{
	uint16_t c0 = (uint16_t)(lo + BIAS) & (BUF_W - 1);
	uint16_t n = (uint16_t)(hi - lo + 1);
	if (n > BUF_W) n = BUF_W;
	while (n) {
		uint16_t part = BUF_W - c0;      // до конца строки буфера
		if (part > n) part = n;
		uint16_t w = (part + 1) & ~1;
		far_t base = FAR(row_page[0], row_ofs[0] + c0);
		uint16_t zero = 0;
		far_write(base, &zero, 2);
		dma_wait();
		TS_DMASAL = (uint8_t)FAR_OFFS(base); TS_DMASAH = (uint8_t)(FAR_OFFS(base) >> 8);
		TS_DMASAX = FAR_PAGE(base);
		TS_DMADAL = (uint8_t)FAR_OFFS(base); TS_DMADAH = (uint8_t)(FAR_OFFS(base) >> 8);
		TS_DMADAX = FAR_PAGE(base);
		TS_DMALEN = (uint8_t)(w / 2 - 1);
		TS_DMANUM = BUF_H - 1;
		TS_DMACTRL = DMA_FILL | DMA_D_ALGN | DMA_ASZ;
		dma_wait();
		n -= part;
		c0 = 0;
	}
}

// Дорисовать полосу мирового диапазона [lo, hi]: очистить её и перерисовать все клетки, чьи
// спрайты её задевают (на TILE_W шире с каждой стороны — соседи заезжают в полосу; поверх уже
// нарисованного они лягут теми же пикселями, поэтому шов не портится).
static void draw_strip(int16_t lo, int16_t hi)
{
	fill_cols(lo, hi);
	build_range(lo - TILE_W, hi + TILE_W);
	dl_run();
	dma_wait();
}

static void draw_map(void)
{
	res_t r;
	if (!loaded || dl_page == PG_NONE) return;
	if (!res_find((uint16_t)(RES_BATMAP0 + cur_map), &r)) return;   // слот мог смениться
	if (!load_tiles()) return;
	if (r.phys != map_phys) { map_phys = r.phys; cells = r.phys + 8 + (uint32_t)m_nt * 4; dl_ok = 0; }
	if (dl_ok) {
		// Вид уже в буфере: если камера ушла по горизонтали, дорисовать въехавшую полосу —
		// буфер кольцевой, поэтому переезжать ничему не нужно.
		int16_t lo = -cam_x - TILE_W, hi = -cam_x + SCREEN_W - 1;
		if (lo < vx_lo) { draw_strip(lo, vx_lo - 1); vx_lo = lo; }
		if (hi > vx_hi) { draw_strip(vx_hi + 1, hi); vx_hi = hi; }
		if (vx_hi - vx_lo > BUF_W - TILE_W) {   // в кольцо больше не влезает — забыть дальний край
			if (hi > vx_hi - 8) vx_lo = vx_hi - (BUF_W - TILE_W);
			else vx_hi = vx_lo + (BUF_W - TILE_W);
		}
		show_cam();
		return;
	}
	build_list();
	fill_buf();
	dl_run();
	dma_wait();
	vx_lo = -cam_x - PAD_X;              // буфер покрывает окно плюс запас с обеих сторон
	vx_hi = -cam_x + SCREEN_W - 1 + PAD_X;
	show_cam();
}

// Микробенчмарк DMA (клавиша B, тест bat_bench.oxs): сколько стоит запуск BLT1 и какова
// настоящая пропускная способность. Модель прототипов считает 300 T на запуск и 74 КБ за кадр
// (02 §6) — на этих числах стоят все оценки в §8.3. Пишем в невидимые строки экрана (y >= 256).
#define BENCH_N 512
static void bench_setup(uint16_t so, uint8_t sp, uint8_t len, uint8_t num)
{
	dma_wait();
	TS_DMASAL = (uint8_t)so; TS_DMASAH = (uint8_t)(so >> 8); TS_DMASAX = sp;
	TS_DMADAL = 0; TS_DMADAH = 0; TS_DMADAX = SCREEN_PAGE + 8;   // y = 256, вне видимой области
	TS_DMALEN = len;
	TS_DMANUM = num;
	TS_DMACTRL = DMA_BLT1 | DMA_D_ALGN | DMA_ASZ;
}

// 512 запусков по одной строке 32 байта: почти чистые накладные расходы
static void bench_small(void)
{
	uint8_t sp = tile_tab[0];
	uint16_t so = (uint16_t)tile_tab[1] | ((uint16_t)tile_tab[2] << 8);
	for (uint16_t i = 0; i < BENCH_N; i++) bench_setup(so, sp, 15, 0);
	dma_wait();
}

// 512 запусков по 32 строки (1024 байта каждый): 512 КБ переноса
static void bench_big(void)
{
	uint8_t sp = tile_tab[0];
	uint16_t so = (uint16_t)tile_tab[1] | ((uint16_t)tile_tab[2] << 8);
	for (uint16_t i = 0; i < BENCH_N; i++) bench_setup(so, sp, 15, 31);
	dma_wait();
}

// 512 запусков по 8 строк (256 байт) — размер, близкий к настоящей части клетки
static void bench_tile(void)
{
	uint8_t sp = tile_tab[0];
	uint16_t so = (uint16_t)tile_tab[1] | ((uint16_t)tile_tab[2] << 8);
	for (uint16_t i = 0; i < BENCH_N; i++) bench_setup(so, sp, 15, 7);
	dma_wait();
}

static void draw_all(void)
{
	draw_map();                          // гасит экран сам — прямо перед отрисовкой
	gfx_blit(RES_ICONS_PCK, 0, VIEW_H, 0, VIEW_H, 320, 200 - VIEW_H);
}

uint8_t bat_get(uint8_t id, sdef_t *s, wdef_t *w) __banked
{
	return scr_find(tab, sizeof tab / sizeof tab[0], id, s, w);
}

void bat_text(uint8_t id, uint8_t slot, uint8_t line, char *buf) __banked
{
	(void)id; (void)slot; (void)line;
	buf[0] = 0;
}

uint8_t bat_rows(uint8_t id, uint8_t slot) __banked
{
	(void)id; (void)slot;
	return 0;
}

uint8_t bat_event(uint8_t id, uint8_t ev, uint8_t arg) __banked
{
	(void)id;
	switch (ev) {
	case EVT_OPEN:
		if (dl_page == PG_NONE) dl_page = pg_alloc(1, 1);
		if (!load_map() && cur_map) { cur_map = 0; load_map(); }
		split_start();
		break;
	case EVT_DRAW:
		draw_all();
		break;
	case EVT_CLOSE:                      // следующий бой — на следующей карте (генератора ещё нет)
		split_stop();
		if (dl_page != PG_NONE) { pg_free(dl_page, 1); dl_page = PG_NONE; dl_ok = 0; }
		cur_map = cur_map + 1 < NMAPS ? cur_map + 1 : 0;
		break;
	case EVT_KEY: {
		// Автоповтор за время долгой перерисовки копится (in_reps) — сдвигать сразу на все
		// шаги, иначе после отпускания клавиши вид ещё несколько раз перерисовывается.
		int16_t n = in_reps ? in_reps : 1;
		int16_t ox = cam_x, oy = cam_y;
		switch (arg) {
		case KEY_LEFT:  cam_x += 16 * n; clamp_cam(); break;
		case KEY_RIGHT: cam_x -= 16 * n; clamp_cam(); break;
		case KEY_UP:    cam_y += 8 * n; clamp_cam(); break;
		case KEY_DOWN:  cam_y -= 8 * n; clamp_cam(); break;
		case 'q': case 'Q': if (level + 1 < m_sz) { level++; center(); } break;
		case 'a': case 'A': if (level) { level--; center(); } break;
		case 'b': case 'B': bench_small(); return 0;   // микробенчмарк DMA: 512 запусков по 32 байта
		case 'n': case 'N': bench_tile(); return 0;    // ... по 256 байт (размер части клетки)
		case 'v': case 'V': bench_big(); return 0;     // ... по 1024 байта
		case 'm': case 'M':
			cur_map = cur_map + 1 < NMAPS ? cur_map + 1 : 0;
			if (!load_map()) { cur_map = 0; load_map(); }
			break;
		default: return 0;
		}
		if (cam_x == ox && cam_y == oy && (arg == KEY_LEFT || arg == KEY_RIGHT || arg == KEY_UP || arg == KEY_DOWN))
			break;                       // камера упёрлась в край карты — перерисовывать нечего
		ui_dirty(0);
		break;
	}
	}
	return 0;
}
