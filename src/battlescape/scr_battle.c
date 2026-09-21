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
// Управление: левая кнопка — идти в клетку (по своему бойцу — выбрать его), правая —
// развернуться туда и открыть дверь (как поворот в оригинале: UnitTurnBState открывает
// дверь, к которой повернулся боец); N — следующий боец, Shift+N — предыдущий (в оригинале
// TAB и SHIFT); стрелки — камера, Q/A — этаж, G — пересобрать карту, ESC — выход.
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
#include "dbg.h"
#include "mapgen.h"
#include "pathfind.h"
#include "globe.h"
#include "music.h"
#include "text.h"
#include "units.h"
#include "doors.h"

#define VIEW_H     144               // окно карты; ниже — панель ICONS (56 строк)
#define TILE_W     32
#define TILE_H     40
#define MAP_MAXX   64                // предел ширины карты (буфер ряда)

static uint16_t gen_terrain;             // какой террейн пробует генератор (клавиша G)
// Параметры миссии, которые приносит геоскейп (bat_mission): развёртывание, карта НЛО,
// карта корабля отряда (всё — номера записей террейнов) и сам корабль в пуле кампании —
// от него берётся экипаж. #FFFF — «не задано»: бой отладочный, всё по умолчанию.
static uint16_t mis_deploy = 0xFFFF, mis_ufo = 0xFFFF, mis_craft = 0xFFFF, mis_crew = 0xFFFF;
static uint8_t m_sx, m_sy, m_sz, m_nt;
static uint8_t level;                // этаж камеры
// Показывать все этажи снизу до текущего или только текущий: кнопка «1/2» на панели,
// в оригинале Camera::toggleShowAllLayers. С одним этажом крыша корабля не мешает смотреть
// внутрь, но верхние этажи не видны — как в оригинале.
static uint8_t all_levels = 1;
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

// Юниты на карте (отладка генератора, тумана войны пока нет). Спрайт собирается как в
// UnitSprite::drawRoutine0 — ноги, торс и руки по направлению; лист UNIT_* даёт 32 кадра
// в порядке: 0-7 левая рука, 8-15 правая, 16-23 ноги, 24-31 торс.
#define MAX_UNITS 12
#define UNIT_PARTS 4
typedef struct { uint8_t x, y, z, dir, alive; uint8_t tu, tu_max, hp, hp_max, en, mor; } unit_t;
static unit_t __at(0xBE00) units[MAX_UNITS];
static uint8_t nunits;
static uint8_t sel;                      // выбранный боец (панель показывает его состояние)
static uint8_t unit_page = PG_NONE, unit_np;
// Память банка (08 §4): units до #BE84, поэтому лист кадров начинается с #BF00 и занимает
// страницу до конца (#BFFF). Раньше он стоял с #BE40 и при семи и больше бойцах затирался
// ими — кадры блитились из случайной памяти, и отряд выглядел цветным мусором.
static uint8_t __at(0xBF00) unit_tab[32 * TE_SIZE];   // кадры листа в виде для блита

// Курсор клетки — как в оригинале (Map.cpp:679 и :968): рамка клетки в CURSOR.PCK разрезана
// на две части. Задняя (кадр 0) рисуется до содержимого клетки, передняя (кадр 3) — после
// бойца, поэтому рамка охватывает клетку объёмно: боец стоит внутри неё, а не поверх.
// Аппаратным спрайтом такого не сделать — спрайты TSU всегда поверх графики.
// Кадры: 0 — задняя половина красной рамки, 1 — жёлтой (на клетке боец), 3 и 4 — передние
#define CUR_FRAMES 4
static uint8_t __at(0xBE90) cur_tab[CUR_FRAMES * TE_SIZE];
static uint8_t cur_page = PG_NONE, cur_np;
static uint8_t cur_on, cur_cx, cur_cy;   // курсор на клетке (cur_cx, cur_cy) текущего этажа

// Дисплей-лист (17_battle_render.md §2): вид раскладывается в список готовых команд DMA —
// по 8 байт, ровно то, что выгружается в регистры. Строится при смене камеры, этажа или карты,
// а перерисовка становится циклом «прочитал 8 байт — записал 8 портов».
// Список живёт в двух страницах: карта миссии бывает 60x60 клеток в четыре этажа, и в одну
// страницу (2048 записей) вид перестал влезать — часть клеток просто не рисовалась.
#define DL_PAGE_N 2048                   // записей в странице (16 КБ / 8)
#define DL_MAX  (DL_PAGE_N * 2)
#define DL_SIZE 8
static uint8_t *const dl = (uint8_t *)0xC000;   // подключённая страница списка
static uint8_t dl_page = PG_NONE;        // первая из двух подряд
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

static void center_on_unit(void);       // определена ниже, вместе с юнитами
static void draw_strip(int16_t lo, int16_t hi);   // перерисовка полосы вида (ниже)
static void cell_repaint(uint8_t x, uint8_t y);   // перерисовка одной клетки (ниже)

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
	for (uint16_t i = dl_n, k = 0; i; i--, k++, p += DL_SIZE) {
		if (k == DL_PAGE_N) { pg_map3(dl_page + 1); p = dl; }
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
static uint16_t dl_over;                 // сколько кадров не влезло в список (диагностика)

static uint8_t *dl_put(void)
{
	if (dl_n >= DL_MAX) { dl_over++; return 0; }
	if (dl_n == DL_PAGE_N) pg_map3(dl_page + 1);   // перевалили во вторую страницу
	return dl + (dl_n++ & (DL_PAGE_N - 1)) * DL_SIZE;
}

// Кадр (запись вида tile_tab/unit_tab) -> команды дисплей-листа: источник лежит подряд
// (строка = w байт), приёмник
// шагает на 512 (BLT1 | D_ALGN | ASZ, 02 §6). Кадр — всегда одна команда; по краям окна он не
// обрезается (см. ниже), сверху и снизу обрезается строками.
static void dl_blit(const uint8_t *e, int16_t x, int16_t y)
{
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

// Кадр карты по номеру части: вертикальное смещение части (MCD.P_Level) учтено вызывающим
static void dl_tile(uint8_t t, int16_t x, int16_t y)
{
	dl_blit(tile_tab + (uint16_t)t * TE_SIZE, x, y);
}

static const wdef_t w_battle[] = {
	// Виджет карты: рисует экран (EVT_DRAW) и отдаёт клики экрану (EVT_BUTTON, arg 0) —
	// по ним боец идёт в клетку. Панель ниже перекрыта своими кнопками.
	CUSR(0, 0, 320, VIEW_H, DYN(0), A_CUSTOM, 0),
	// Панель ICONS: кнопки на тех же местах, что в оригинале (BattlescapeState.cpp:100-115).
	HOT(48, 144, 32, 16, A_NONE, 1, 0),            // боец выше по списку
	HOT(48, 160, 32, 16, A_NONE, 2, 0),            // боец ниже
	HOT(80, 144, 32, 16, A_NONE, 3, 0),            // этаж вверх
	HOT(80, 160, 32, 16, A_NONE, 4, 0),            // этаж вниз
	HOT(144, 144, 32, 16, A_NONE, 5, 'i'),         // инвентарь
	HOT(144, 160, 32, 16, A_NONE, 6, 'c'),         // центрировать на бойце
	HOT(176, 144, 32, 16, A_NONE, 7, 0),           // следующий боец
	HOT(208, 144, 32, 16, A_NONE, 9, 0),           // показывать все этажи или только текущий
	HOT(240, 144, 32, 16, A_NONE, 8, 0),           // конец хода
	HOT(240, 160, 32, 16, A_POP, 0, ESC),          // выйти из боя
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

// Разложить вид в дисплей-лист: уровни снизу вверх, ряды Y, внутри ряда X, внутри клетки —
// пол, западная стена, северная стена, объект (порядок художника, Map.cpp:626-966)
// Разложить в список клетки, чья мировая экранная координата X попадает в [lo, hi]. Мировая X
// клетки — (x - y) * 16, от камеры не зависит; в буфере ей отвечает колонка (X + BIAS) & 511,
// то есть буфер по горизонтали кольцевой и при движении камеры ничего не переезжает — надо лишь
// дорисовать въехавшую полосу. Полоса берётся с запасом в ширину спрайта: клетка рисуется
// целиком, поэтому в неё должны попасть и соседи, чьи спрайты в неё заезжают.
// Кадр юнита в дисплей-лист (как dl_tile, но кадры берутся из своего листа)
static void dl_unit_frame(uint8_t fr, int16_t x, int16_t y)
{
	const uint8_t *e = unit_tab + (uint16_t)fr * TE_SIZE;
	if (!e[7]) return;
	dl_blit(e, x, y);
}

// Стоит ли в клетке живой боец (цвет рамки курсора)
// Номер бойца в клетке + 1 (0 — никого): по нему и рисуют, и выбирают щелчком
static uint8_t unit_no(uint8_t z, uint8_t y, uint8_t x)
{
	for (uint8_t i = 0; i < nunits; i++) {
		const unit_t *u = &units[i];
		uint8_t uz = u->z, uy = u->y, ux = u->x;
		if (u->alive && uz == z && uy == y && ux == x) return (uint8_t)(i + 1);
	}
	return 0;
}

static uint8_t unit_at(uint8_t z, uint8_t y, uint8_t x)
{
	return unit_no(z, y, x) != 0;
}

// Боец в этой клетке (если он тут есть): ноги, торс, руки по направлению
static void draw_unit_at(uint8_t z, uint8_t y, uint8_t x, int16_t px, int16_t py)
{
	if (!nunits || unit_page == PG_NONE) return;
	for (uint8_t i = 0; i < nunits; i++) {
		const unit_t *u = &units[i];
		if (!u->alive || u->z != z || u->y != y || u->x != x) continue;
		uint8_t d = u->dir & 7;
		dl_unit_frame(16 + d, px, py);       // ноги
		dl_unit_frame(24 + d, px, py);       // торс
		dl_unit_frame(0 + d, px, py);        // левая рука
		dl_unit_frame(8 + d, px, py);        // правая рука
		return;
	}
}

// Окно буфера по Y для сборки списка: перерисовка клетки (курсор, шаг бойца) трогает
// полосу не на всю высоту, а только вокруг себя — иначе на каждое движение мыши
// пересобирался бы весь столбец карты.
static int16_t clip_y0, clip_y1 = BUF_H - 1;

static void build_range(int16_t lo, int16_t hi)
{
	dl_n = 0;
	int16_t dlo = (lo - TILE_W + 1 + 15) >> 4, dhi = hi >> 4;   // x - y для краёв полосы
	for (uint8_t z = all_levels ? 0 : level; z <= level; z++) {
		// Какие ряды вообще могут попасть в буфер: по X полоса задаёт x - y = d из [dlo, dhi],
		// значит by = (2y + d) * 8 - z * 24 + B, и из -40 < by < BUF_H выводятся границы y.
		int16_t b = base_y + PAD_Y - (int16_t)z * 24;
		int16_t ylo = (clip_y0 - TILE_H + 1 - 8 * dhi - b + 15) >> 4;
		int16_t yhi = (clip_y1 - 8 * dlo - b) >> 4;
		if (ylo < 0) ylo = 0;
		if (yhi >= m_sy) yhi = m_sy - 1;
		// Адрес ряда ведётся сложением: 32-битное умножение на каждый ряд стоило дороже,
		// чем всё остальное в этом цикле вместе взятое.
		uint16_t row_bytes = (uint16_t)m_sx * 4;
		far_t rbase = cells + (uint32_t)(((uint32_t)z * m_sy + ylo) * m_sx) * 4;
		for (int16_t y = ylo; y <= yhi; y++, rbase += row_bytes) {
			int16_t ry = (int16_t)y * 8 - (int16_t)z * 24 + base_y + PAD_Y;
			int16_t rx = -(int16_t)y * 16;   // мировая X клетки x этого ряда: rx + x*16
			// Отрезок ряда: из lo-32 < wx <= hi и -40 < py < BUF_H получаем границы x
			int16_t x0 = (lo - TILE_W + 1 - rx + 15) >> 4, x1 = (hi - rx) >> 4;
			int16_t t0 = (clip_y0 - TILE_H + 1 - ry + 7) >> 3, t1 = (clip_y1 - ry) >> 3;
			if (t0 > x0) x0 = t0;
			if (t1 < x1) x1 = t1;
			if (x0 < 0) x0 = 0;
			if (x1 >= m_sx) x1 = m_sx - 1;
			if (x0 > x1) continue;
			far_read(rbase + (uint16_t)x0 * 4, row, (uint16_t)(x1 - x0 + 1) * 4);
			// дальше пишем команды в страницу списка — ту, в которой сейчас его конец
			uint8_t old = pg_map3(dl_n >= DL_PAGE_N ? dl_page + 1 : dl_page);
			const uint8_t *p = row;
			int16_t px = rx + x0 * 16, py = ry + x0 * 8;
			for (int16_t x = x0; x <= x1; x++, p += 4, px += 16, py += 8) {
				uint8_t cx = cur_cx, cy = cur_cy;   // сравнения байтов — с копиями (SDCC 4.5)
				uint8_t here = cur_on && z == level && (uint8_t)x == cx && (uint8_t)y == cy;
				// на клетке боец — рамка жёлтая, пустая клетка — красная (как в оригинале)
				uint8_t cf = here && unit_at(z, (uint8_t)y, (uint8_t)x) ? 1 : 0;
				if (here) dl_blit(cur_tab + cf * TE_SIZE, px, py);          // задняя половина
				for (uint8_t k = 0; k < 4; k++) {
					uint8_t t = p[k];
					if (!t) continue;
					dl_tile((uint8_t)(t - 1), px, py - (int16_t)tile_y[t - 1]);   // px — мировая X
				}
				// Боец идёт сразу за своей клеткой: клетки правее и ниже рисуются позже и
				// закрывают его — иначе он виден сквозь стены и закрытые двери.
				draw_unit_at(z, (uint8_t)y, (uint8_t)x, px, py);
				if (here) dl_blit(cur_tab + (2 + cf) * TE_SIZE, px, py);    // передняя половина
			}
			pg_map3(old);
		}
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

// Очистить в кольце прямоугольник: мировой диапазон [lo, hi] по X, строки буфера
// [y0, y1] по Y. На завороте кольца получается два куска.
static void fill_cols(int16_t lo, int16_t hi, int16_t y0, int16_t y1)
{
	uint16_t c0 = (uint16_t)(lo + BIAS) & (BUF_W - 1);
	uint16_t n = (uint16_t)(hi - lo + 1);
	if (n > BUF_W) n = BUF_W;
	while (n) {
		uint16_t part = BUF_W - c0;      // до конца строки буфера
		if (part > n) part = n;
		uint16_t w = (part + 1) & ~1;
		far_t base = FAR(row_page[y0], row_ofs[y0] + c0);
		uint16_t zero = 0;
		far_write(base, &zero, 2);
		dma_wait();
		TS_DMASAL = (uint8_t)FAR_OFFS(base); TS_DMASAH = (uint8_t)(FAR_OFFS(base) >> 8);
		TS_DMASAX = FAR_PAGE(base);
		TS_DMADAL = (uint8_t)FAR_OFFS(base); TS_DMADAH = (uint8_t)(FAR_OFFS(base) >> 8);
		TS_DMADAX = FAR_PAGE(base);
		TS_DMALEN = (uint8_t)(w / 2 - 1);
		TS_DMANUM = (uint8_t)(y1 - y0);
		TS_DMACTRL = DMA_FILL | DMA_D_ALGN | DMA_ASZ;
		dma_wait();
		n -= part;
		c0 = 0;
	}
}

// Дорисовать полосу мирового диапазона [lo, hi]: очистить её и перерисовать все клетки, чьи
// спрайты её задевают (на TILE_W шире с каждой стороны — соседи заезжают в полосу; поверх уже
// нарисованного они лягут теми же пикселями, поэтому шов не портится).


// Лист юнита в память и в готовые для блита записи (как тайлсет карты)
// Записать кадр тайлсета в строку таблицы частей — в готовом для блита виде (17 §2)
static void tile_entry(uint8_t *p, far_t tphys, far_t fdata, uint16_t fr)
{
	uint8_t e[6];
	far_read(tphys + 4 + (uint32_t)fr * 6, e, 6);
	far_t src = fdata + ((uint32_t)((uint16_t)e[4] | ((uint16_t)e[5] << 8)) << 1);
	uint16_t so = FAR_OFFS(src);
	p[0] = FAR_PAGE(src);
	p[1] = (uint8_t)so; p[2] = (uint8_t)(so >> 8);
	p[3] = e[2] ? (uint8_t)(e[2] / 2 - 1) : 0;
	p[4] = e[3];
	p[5] = e[0]; p[6] = e[1];
	p[7] = e[2];
}

static uint8_t load_unit_sheet(uint16_t id)
{
	res_t rt;
	if (unit_page != PG_NONE) { pg_free(unit_page, unit_np); unit_page = PG_NONE; }
	uint32_t sz = sdres_size(id);
	if (!sz) return 0;
	unit_np = (uint8_t)((sz + 16383) >> 14);
	unit_page = pg_alloc(unit_np, 1);
	if (unit_page == PG_NONE) return 0;
	if (!sdres_load(id, unit_page, &rt)) { pg_free(unit_page, unit_np); unit_page = PG_NONE; return 0; }
	uint16_t nfr = rt.a;
	far_t fdata = rt.phys + 4 + (uint32_t)nfr * 6;
	for (uint16_t i = 0; i < 32; i++) {
		uint8_t *p = unit_tab + i * TE_SIZE;
		if (i >= nfr) { p[7] = 0; continue; }
		tile_entry(p, rt.phys, fdata, i);
	}
	return 1;
}

// Лист курсора клетки: из CURSOR.PCK берутся два кадра — задняя и передняя половины рамки
static uint8_t load_cursor(void)
{
	res_t rt;
	if (cur_page == PG_NONE) {
		uint32_t sz = sdres_size(RES_CURSOR_PCK);
		if (!sz) return 0;
		cur_np = (uint8_t)((sz + 16383) >> 14);
		cur_page = pg_alloc(cur_np, 1);
		if (cur_page == PG_NONE) return 0;
	}
	if (!sdres_load(RES_CURSOR_PCK, cur_page, &rt)) return 0;
	uint16_t nfr = rt.a;
	far_t fdata = rt.phys + 4 + (uint32_t)nfr * 6;
	static const uint8_t want[CUR_FRAMES] = { 0, 1, 3, 4 };
	for (uint8_t i = 0; i < CUR_FRAMES; i++) {
		uint8_t *p = cur_tab + i * TE_SIZE;
		if (want[i] >= nfr) { p[7] = 0; continue; }
		tile_entry(p, rt.phys, fdata, want[i]);
	}
	return 1;
}

// Навести камеру на выбранного бойца (кнопка «центрировать», как в оригинале)
static void center_on_unit(void)
{
	if (!nunits) return;
	const unit_t *u = &units[sel < nunits ? sel : 0];
	cam_x = 160 - ((int16_t)u->x - (int16_t)u->y) * 16;
	cam_y = VIEW_H / 2 - (((int16_t)u->x + (int16_t)u->y) * 8 - (int16_t)level * 24);
	dl_ok = 0;
	clamp_cam();
}

// Высадка отряда: экипаж корабля миссии и его места на карте считает units.c (банк 30) —
// в банке боя для этого уже нет места. Здесь только перенос в таблицу бойцов вида.
static void place_squad(uint8_t debug_n)
{
	crew_req_t q;
	cunit_t cu[MAX_UNITS];
	nunits = 0;
	mapgen_craft(&q.cx, &q.cy, &q.cw, &q.cl, &q.part_min);
	q.cells = cells; q.sx = m_sx; q.sy = m_sy;
	q.craft = mis_crew; q.debug_n = debug_n;
	uint8_t n = crew_deploy(&q, cu, MAX_UNITS);
	for (uint8_t i = 0; i < n; i++) {
		units[i].x = cu[i].x; units[i].y = cu[i].y; units[i].z = cu[i].z;
		units[i].dir = 0; units[i].alive = 1;
		units[i].tu = units[i].tu_max = cu[i].tu;
		units[i].hp = units[i].hp_max = cu[i].hp;
		units[i].en = cu[i].en; units[i].mor = cu[i].mor;
	}
	nunits = n;
}


// ---------------------------------------------------------------- ход бойца
// Поиск пути — волна по клеткам текущего этажа (Pathfinding оригинала считает ещё и
// подъёмы, стоимость частей и приседание; здесь пока ровная цена шага). Рабочие массивы
// лежат в своей странице пула и видны через Win3: стоимость и откуда пришли, по байту
// на клетку карты (до 64x64).
#define STEP_TU        4                 // единиц времени за шаг (в оригинале зависит от пола)
#define PATH_MAX       24

static uint8_t pf_page = PG_NONE;                // страница под рабочие массивы поиска пути
static uint8_t path[PATH_MAX], path_n, path_i;   // направления шагов
static uint8_t move_wait;                        // кадров до следующего шага
static uint8_t turn_dir = 0xFF;                  // куда разворачивается боец (правая кнопка)
static uint8_t ndoors;                           // дверей среди частей карты (отладка)

#define MC_SIZE 12                       // байт на часть в таблице набора (MCDSET, 16 §2.3)

// Свойства частей лежат в странице поиска пути (pathfind.h): её читает и поиск, и бой
static void pf_put(uint16_t off, uint8_t v)
{
	if (pf_page != PG_NONE) far_write(FAR(pf_page, off), &v, 1);
}

static uint8_t pf_get(uint16_t off)
{
	return pf_page == PG_NONE ? 0 : far_byte(FAR(pf_page, off));
}

// Смещения восьми направлений (0 — север, дальше по часовой, как в оригинале)
// Своя копия: таблицы из банка поиска пути видны только при подключённом том банке
static const int8_t dir_dx[8] = { 0, 1, 1, 1, 0, -1, -1, -1 };
static const int8_t dir_dy[8] = { -1, -1, 0, 1, 1, 1, 0, -1 };

// Экранная точка -> клетка карты (Camera::convertScreenToMap оригинала: к точке добавляется
// -32/2 и 24 на каждый этаж, дальше косой пересчёт). Клетке принадлежат нижние 16 строк её
// тайла 32x40 — там же рисуется ромб курсора: клетка ровно та же, что в оригинале.
static uint8_t screen_to_cell(int16_t mx, int16_t my, uint8_t *ox, uint8_t *oy)
{
	int16_t a = mx - cam_x;                              // (x - y) * 16
	int16_t b = my - cam_y + (int16_t)level * 24 - TILE_W / 2;   // (x + y) * 8
	int16_t x = (a + 2 * b - 32) / 32, y = (2 * b - a) / 32;
	if (x < 0 || y < 0 || x >= m_sx || y >= m_sy) return 0;
	*ox = (uint8_t)x; *oy = (uint8_t)y;
	return 1;
}

// Следующий и предыдущий боец отряда: выбор, камера на него и отмена начатого пути
// (BattlescapeState::btnNextSoldierClick — в оригинале камера тоже переезжает)
static void sel_next(void)
{
	if (!nunits) return;
	sel = (uint8_t)((sel + 1) % nunits);
	path_n = 0;
	center_on_unit();
	ui_dirty(0);
}

static void sel_prev(void)
{
	if (!nunits) return;
	sel = (uint8_t)((sel + nunits - 1) % nunits);
	path_n = 0;
	center_on_unit();
	ui_dirty(0);
}

// Карта для банка клеток (doors.c): двери и цена прохода считаются там
static dmap_t bmap;

// Открыть дверь в направлении dir и перерисовать изменившиеся клетки.
// 0 — открыли, 1 — не хватило времени, 2 — двери нет.
static uint8_t door_try(unit_t *u, uint8_t dir, uint8_t rclick)
{
	uint8_t list[8], spent = 0;
	memset(list, 0xFF, sizeof list);     // #FF — конец списка: координаты меньше 64
	uint8_t r = door_open(&bmap, u->x, u->y, u->z, dir, rclick, u->tu, &spent, list, 4);
	if (r) return r;
	u->tu -= spent;
	for (uint8_t i = 0; i < 4 && list[i * 2] != 0xFF; i++)
		cell_repaint(list[i * 2], list[i * 2 + 1]);
	return 0;
}

// Повернуться на одну восьмую в сторону d (в оригинале поворот при ходьбе бесплатен и
// идёт по одному шагу за такт — отсюда и разворот на месте перед шагом)
static void turn_to(unit_t *u, uint8_t d)
{
	uint8_t diff = (uint8_t)((d - u->dir) & 7);
	u->dir = (uint8_t)((u->dir + (diff && diff <= 4 ? 1 : 7)) & 7);
	cell_repaint(u->x, u->y);
}

// Один шаг по пути: поворот, открывание двери, перенос в клетку и расход времени
static void step_unit(void)
{
	if (!nunits || path_i >= path_n) { path_n = 0; return; }
	unit_t *u = &units[sel < nunits ? sel : 0];
	uint8_t d = path[path_i];
	if (u->dir != d) { turn_to(u, d); return; }      // сперва развернуться лицом к шагу
	if (door_try(u, d, 0) != 2) return;              // дверь открывается вместо шага
	int16_t nx = (int16_t)u->x + dir_dx[d], ny = (int16_t)u->y + dir_dy[d];
	if (nx < 0 || ny < 0 || nx >= m_sx || ny >= m_sy) { path_n = 0; return; }
	uint8_t cost = cell_cost(&bmap, (uint8_t)nx, (uint8_t)ny, u->z, d, STEP_TU);
	if (u->tu < cost) { path_n = 0; return; }
	uint8_t ox = u->x, oy = u->y;        // откуда ушёл — эту клетку тоже перерисовать
	u->x = (uint8_t)nx; u->y = (uint8_t)ny;
	u->tu -= cost;
	if (u->en > 1) u->en--;              // энергия тратится медленнее времени
	path_i++;
	if (path_i >= path_n) path_n = 0;
	// Перерисовываем не весь вид и даже не полосу, а две клетки — ту, откуда боец ушёл, и
	// ту, где оказался (17 §2). Гасится и собирается заново только их прямоугольник.
	uint8_t nxx = u->x, nyy = u->y;
	cell_repaint(ox, oy);
	cell_repaint(nxx, nyy);
}

// ---------------------------------------------------------------- сгенерированная карта
// Генератор (mapgen.c, банк 29) собирает поле из блоков террейна, а здесь собирается всё,
// что нужно для показа: таблица частей (tile_tab) и тайлсеты наборов в своих страницах.
// Номер части в клетке — как в MAP оригинала: 0 пусто, иначе номер части миссии; рендер
// берёт tile_tab[t - 1], поэтому часть t кладётся в строку t - 1.
#define GEN_SETS 16              // столько же, сколько у генератора (MAX_SETS)
static uint8_t gen_mode;                 // 1 — показывается сгенерированная карта
static uint8_t gen_page[GEN_SETS], gen_np[GEN_SETS], gen_ns;

static void gen_free(void)
{
	for (uint8_t i = 0; i < gen_ns; i++)
		if (gen_page[i] != PG_NONE) pg_free(gen_page[i], gen_np[i]);
	gen_ns = 0;
}

static uint8_t load_gen(uint16_t terrain)
{
	uint16_t set[GEN_SETS], tset[GEN_SETS];
	uint8_t ns = 0;
	gen_free();
	loaded = 0;
	// Параметры миссии: размер поля, этажи, скрипт и террейн — из развёртывания, если оно
	// задано геоскейпом; корабль отряда и НЛО — от цели, иначе первые попавшиеся (отладка).
	uint8_t mods = 4, levels = 4;
	if (mis_deploy != 0xFFFF) {
		uint16_t t = mapgen_deploy(mis_deploy, &mods, &levels);
		if (t != 0xFFFF) terrain = t;
	}
	mapgen_set_extra(mis_craft != 0xFFFF ? mis_craft : mapgen_find_kind(1),
			 mis_ufo != 0xFFFF ? mis_ufo : mapgen_find_kind(2));
	if (!mapgen_run(terrain, mods, levels)) return 0;
	mapgen_sets(set, tset, &ns);
	if (ns > GEN_SETS) ns = GEN_SETS;
	memset(tile_tab, 0, sizeof tile_tab);   // не оставлять кадры прошлой карты: если набор
	memset(tile_y, 0, sizeof tile_y);       //   не загрузится, его части просто не рисуются
	sdres_flush();                       // кэш ресурсов отдаёт страницы: наборы тайлов важнее
	// Тайлсет грузим только у наборов, чьи части реально попали на карту: пустые наборы
	// (те же BLANKS) иначе съедают страницы, которых потом не хватает кораблю и НЛО.
	uint8_t used[32];
	mapgen_used(used);
	// Свойства частей (цена прохода и двери) живут в странице поиска пути: там их читает
	// и сам поиск, и банку боя не приходится отдавать под них память (pathfind.h).
	far_fill(FAR(pf_page, PF_TU), 0, 768);
	uint16_t total = 0;                  // всего частей во всех наборах миссии
	for (uint8_t s = 0; s < ns; s++) {
		res_t rm;
		if (res_find(set[s], &rm)) total += rm.a;
	}
	ndoors = 0;
	uint16_t vnext = 255;                // номера для частей «дверь НЛО открыта» — сверху вниз
	uint16_t part = 0;                   // сквозной номер части миссии
	for (uint8_t s = 0; s < ns && part < 256; s++) {
		res_t rm, rt;
		if (!res_find(set[s], &rm)) continue;
		uint8_t need = 0;                // используется ли хоть одна часть набора
		for (uint16_t i = 0; i < rm.a && part + i < 256; i++) {
			uint16_t g = part + i;
			if (used[g >> 3] & (1 << (g & 7))) { need = 1; break; }
		}
		if (!need) { part += rm.a; continue; }
		uint32_t tsz = sdres_size(tset[s]);
		uint8_t np = (uint8_t)((tsz + 16383) >> 14);
		uint8_t pg = np ? pg_alloc(np, 1) : PG_NONE;
		if (pg == PG_NONE) break;
		if (!sdres_load(tset[s], pg, &rt)) { pg_free(pg, np); break; }
		gen_page[gen_ns] = pg; gen_np[gen_ns] = np; gen_ns++;
		uint16_t nfr = rt.a;
		far_t fdata = rt.phys + 4 + (uint32_t)nfr * 6;
		uint16_t base = part;                // номер первой части набора (в нём же номера alt)
		for (uint16_t i = 0; i < rm.a && part < 256; i++, part++) {
			uint8_t mc[MC_SIZE];
			far_read(rm.phys + (uint32_t)i * MC_SIZE, mc, MC_SIZE);
			uint16_t fr = (uint16_t)mc[0] | ((uint16_t)mc[1] << 8);
			if (!part || fr >= nfr) continue;        // часть 0 в клетках не встречается
			tile_entry(tile_tab + (part - 1) * TE_SIZE, rt.phys, fdata, fr);
			tile_y[part - 1] = mc[2];
			// Цена прохода (255 — стена) и двери: распашная меняется на часть Alt_MCD, дверь
			// НЛО сдвигается в сторону — ей заводится своя часть с открытым кадром (Frame[7]).
			pf_put(PF_TU + part, mc[7]);
			if ((mc[3] & 8) && mc[8] && base + mc[8] < 256) {
				pf_put(PF_ALT + part, (uint8_t)(base + mc[8]));
				ndoors++;
				pf_put(PF_ALTSLOT + part, far_byte(rm.phys + (uint32_t)mc[8] * MC_SIZE + 5) & 3);
			} else if ((mc[3] & 4) && vnext > total) {
				uint16_t f7 = (uint16_t)mc[9] | ((uint16_t)mc[10] << 8);
				if (f7 < nfr) {
					uint16_t vp = vnext--;
					tile_entry(tile_tab + (vp - 1) * TE_SIZE, rt.phys, fdata, f7);
					tile_y[vp - 1] = mc[2];
					pf_put(PF_TU + vp, 0);           // сквозь открытую дверь ходят свободно
					pf_put(PF_ALT + part, (uint8_t)vp);
					ndoors++;
					pf_put(PF_ALTSLOT + part, (uint8_t)((mc[5] & 3) | 0x80));
				}
			}
		}
	}
	if (!gen_ns) return 0;
	m_sx = (uint8_t)mapgen_sx(); m_sy = (uint8_t)mapgen_sy(); m_sz = mapgen_sz();
	m_nt = part;
	cells = mapgen_cells();
	map_phys = cells;
	bmap.cells = cells; bmap.sx = m_sx; bmap.sy = m_sy; bmap.page = pf_page;
	rows_init();
	level = 0;                           // камера начинается на земле, как в оригинале
	gen_mode = 1;
	dl_ok = 0;
	// Отряд: экипаж корабля миссии со своими показателями (units.c), а когда бой отладочный
	// (корабля нет) — шесть фигур. Лист брони X-COM: TDXCOM_0 у TFTD, XCOM_0 у UFO.
	if (load_unit_sheet(res_game() == 2 ? RES_UNIT_TDXCOM_0 : RES_UNIT_XCOM_0)) place_squad(6);
	else nunits = 0;
	sel = 0;
	center();
	if (nunits) center_on_unit();        // камера на первого бойца отряда
	loaded = 1;
	return 1;
}

// Перерисовать кусок вида: гасим прямоугольник кольца и собираем в него заново все клетки,
// которые его задевают. Гасить приходится потому, что тайлы кладутся прозрачным блитом
// (BLT1 пропускает цвет 0) — без гашения под новой картинкой остаётся старая.
// Полоса на всю высоту (скролл камеры) — это тот же прямоугольник с y0 = 0.
static void draw_rect(int16_t lo, int16_t hi, int16_t y0, int16_t y1)
{
	if (y0 < 0) y0 = 0;
	if (y1 > BUF_H - 1) y1 = BUF_H - 1;
	if (y0 > y1) return;
	fill_cols(lo, hi, y0, y1);
	// Сборка берёт клетки чуть шире прямоугольника: соседи заезжают в него своими спрайтами,
	// а части с P_Level подняты вверх (до 24 точек), поэтому клетки ниже прямоугольника тоже
	// могут в него нарисовать — их и добираем снизу.
	clip_y0 = y0;
	clip_y1 = y1 + 24;
	build_range(lo - TILE_W, hi + TILE_W);
	clip_y0 = 0;
	clip_y1 = BUF_H - 1;
	dl_run();
	dma_wait();
}

static void draw_strip(int16_t lo, int16_t hi)
{
	draw_rect(lo, hi, 0, BUF_H - 1);
}

// Перерисовать клетку (её спрайт 32x40) — столько и трогаем при переезде курсора и шаге бойца
static void cell_repaint(uint8_t x, uint8_t y)
{
	if (!dl_ok || dl_page == PG_NONE) { ui_dirty(0); return; }
	int16_t wx = ((int16_t)x - (int16_t)y) * 16;
	int16_t by = ((int16_t)x + (int16_t)y) * 8 - (int16_t)level * 24 + base_y + PAD_Y;
	draw_rect(wx, wx + TILE_W - 1, by, by + TILE_H - 1);
}

static void draw_map(void)
{
	res_t r;
	if (!loaded || dl_page == PG_NONE) return;
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

// Замеры DMA (клавиши B/N/V) убраны: их числа записаны в 16 §8.3, а место в банке нужно
// под сам бой.

// Панель бойца рисует банк 30 (units.c): в банке боя место кончилось
static void draw_panel(void)
{
	bpanel_t p;
	const unit_t *u = &units[sel < nunits ? sel : 0];
	p.n = nunits; p.level = level;
	p.tu = u->tu; p.tu_max = u->tu_max; p.en = u->en;
	p.hp = u->hp; p.hp_max = u->hp_max; p.mor = u->mor;
	bat_panel(&p);
}

static void draw_all(void)
{
	draw_map();                          // гасит экран сам — прямо перед отрисовкой
	draw_panel();
}

// Геоскейп сообщает, какая миссия начинается: развёртывание (state.h site_t.deployment),
// террейн НЛО и террейн корабля отряда; #FFFF — не задано.
void bat_mission(uint16_t deploy, uint16_t ufo, uint16_t craft_map, uint16_t crew) __banked
{
	mis_deploy = deploy;
	mis_ufo = ufo;
	mis_craft = craft_map;
	mis_crew = crew;
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
	case EVT_MUSIC: {
		// Тема боя: роль MK_TACTIC из таблицы MUSGRP (22 §1.3), как GMTACTIC в оригинале
		uint16_t id = mus_kind(MK_TACTIC);
		if (id) mus_play(id);
		break;
	}
	case EVT_OPEN:
		if (dl_page == PG_NONE) dl_page = pg_alloc(2, 1);   // список вида: две страницы подряд
		if (pf_page == PG_NONE) pf_page = pg_alloc(1, 1);   // рабочая память поиска пути
		// Карта миссии собирается генератором (16 §3). Пока миссии нет, террейн берётся
		// по очереди — клавиша G дальше пересобирает карту следующего террейна.
		load_gen(gen_terrain);               // карта миссии: генератор (16 §3)
		split_start();
		// У боя своя палитра: цвета #F0..#FF в ней тёмные, и курсор в них не виден.
		// В оригинале у боя свой цвет курсора (Mod::BATTLESCAPE_CURSOR = 144).
		cursor_color(CURSOR_BATTLESCAPE);
		load_cursor();                   // рамка клетки из CURSOR.PCK (рисуется вместе с картой)
		cur_on = 0;
		break;
	case EVT_TICK:
		// Курсор клетки: рамка вокруг клетки под мышью. Она часть картинки карты (задняя
		// половина под содержимым клетки, передняя поверх), поэтому при переезде на другую
		// клетку перерисовываются только две клетки — прежняя и новая.
		if (gen_mode && loaded) {
			uint8_t cx = 0, cy = 0, on = 0;
			if (cursor_y < VIEW_H && screen_to_cell(cursor_x, cursor_y, &cx, &cy)) on = 1;
			uint8_t ox = cur_cx, oy = cur_cy, oon = cur_on;
			if (on != oon || (on && (cx != ox || cy != oy))) {
				cur_cx = cx; cur_cy = cy; cur_on = on;
				if (oon) cell_repaint(ox, oy);
				if (on) cell_repaint(cx, cy);
			}
		}
		if (turn_dir != 0xFF && nunits) {    // правая кнопка: разворот по одной восьмой,
			unit_t *u = &units[sel < nunits ? sel : 0];   // в конце — попытка открыть дверь
			if (move_wait) move_wait--;
			else if (u->dir != turn_dir) { turn_to(u, turn_dir); move_wait = 2; }
			else { door_try(u, turn_dir, 1); turn_dir = 0xFF; ui_dirty(0); }
		} else if (path_n) {                 // идём по пути: шаг раз в несколько кадров
			if (move_wait) move_wait--;
			else { step_unit(); move_wait = 3; if (!path_n) ui_dirty(0); }   // в конце — обновить панель
		}
		break;
	case EVT_DRAW:
		draw_all();
		break;
	case EVT_CLOSE:
		// Бой отдаёт всю свою память: карту, наборы тайлов, лист юнитов, список вида и
		// рабочую память поиска пути. Иначе геоскейпу не из чего заводить слоты кэша, и он
		// перечитывает каждый фон с карты — экран залипает (14 §todo).
		split_stop();
		cur_on = 0;
		if (cur_page != PG_NONE) { pg_free(cur_page, cur_np); cur_page = PG_NONE; }
		cursor_color(CURSOR_GEOSCAPE);   // вернуть цвет курсора геоскейпа
		gen_free();
		if (unit_page != PG_NONE) { pg_free(unit_page, unit_np); unit_page = PG_NONE; }
		mapgen_free();
		gen_mode = 0;
		loaded = 0;
		nunits = 0;
		if (dl_page != PG_NONE) { pg_free(dl_page, 2); dl_page = PG_NONE; dl_ok = 0; }
		if (pf_page != PG_NONE) { pg_free(pf_page, 1); pf_page = PG_NONE; }
		path_n = 0;
		turn_dir = 0xFF;
		// Карта боя рисуется в строки холста 200.., а там же задний буфер глобуса (globe.s,
		// BACK_Y 280): после боя геоскейп обязан нарисовать планету заново, иначе на экране
		// остаются тайлы поля боя.
		globe_invalidate();
		gview_reset();
		globe_prepare();                 // собрать планету в задний буфер целиком, поверх следов боя
		break;
	case EVT_BUTTON:
		switch (arg) {
		case 0: {                            // клик по карте
			if (!gen_mode || !nunits || cursor_y >= VIEW_H) break;
			uint8_t tx, ty;
			if (!screen_to_cell(cursor_x, cursor_y, &tx, &ty)) break;
			unit_t *u = &units[sel < nunits ? sel : 0];
			if (ui_arrow_max) {              // правая кнопка: развернуться туда и открыть дверь
				path_n = 0;
				turn_dir = dir_to((int16_t)tx - (int16_t)u->x, (int16_t)ty - (int16_t)u->y);
				move_wait = 0;
				break;
			}
			uint8_t no = unit_no(u->z, ty, tx);
			if (no && no - 1 != sel) {       // свой боец — выбрать его (primaryAction оригинала)
				sel = (uint8_t)(no - 1);
				path_n = 0;
				ui_dirty(0);
				break;
			}
			pf_req_t q = { cells, m_sx, m_sy, u->z, u->x, u->y, tx, ty, u->tu, STEP_TU, pf_page };
			path_n = pf_find(&q, path, PATH_MAX);
			path_i = 0;
			move_wait = 0;
			break;
		}
		case 1: sel_prev(); break;           // боец выше по списку (и камера на него)
		case 2: sel_next(); break;
		case 3: if (level + 1 < m_sz) { level++; center(); ui_dirty(0); } break;
		case 4: if (level) { level--; center(); ui_dirty(0); } break;
		case 5: UI_GO(A_PUSH, SCR_INVENTORY); break;
		case 6: center_on_unit(); ui_dirty(0); break;
		case 7: sel_next(); break;           // следующий боец
		case 9:                              // «1/2»: все этажи или только текущий
			all_levels = !all_levels;
			dl_ok = 0;
			ui_dirty(0);
			break;
		case 8:                              // конец хода: время и энергия бойцов восстанавливаются
			for (uint8_t i = 0; i < nunits; i++) { units[i].tu = units[i].tu_max; units[i].en = units[i].tu_max; }
			ui_dirty(0);
			break;
		}
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
		case 'd': case 'D': {               // отладка: поставить бойца к первой двери
			uint8_t dx, dy, ds;
			if (!gen_mode || !nunits || !door_find(&bmap, level, &dx, &dy, &ds)) break;
			unit_t *u = &units[sel < nunits ? sel : 0];
			uint8_t ox = u->x, oy = u->y;
			u->x = dx; u->y = dy; u->z = level;
			u->dir = ds == 1 ? 6 : 0;         // западная стена — лицом на запад, северная — на север
			path_n = 0;
			cell_repaint(ox, oy);
			dbg_puts("door: at "); dbg_dec(dx); dbg_puts(","); dbg_dec(dy);
			dbg_puts(" slot "); dbg_dec(ds); dbg_puts("
");
			center_on_unit();
			break;
		}
		case 'n': sel_next(); break;         // следующий боец (в оригинале TAB)
		case 'N': sel_prev(); break;         // предыдущий (в оригинале SHIFT)
		case 'a': case 'A': if (level) { level--; center(); } break;
		case 'g': case 'G': {
			// Собрать карту очередного террейна генератором и показать её (16 §3)
			uint8_t ok = load_gen(gen_terrain);
			dbg_puts("mapgen: terrain ");
			dbg_dec(gen_terrain);
			dbg_puts(ok ? " ok " : " FAIL ");
			dbg_puts(" dl "); dbg_dec(dl_n); dbg_puts(" units "); dbg_dec(nunits); dbg_puts(" doors "); dbg_dec(ndoors);
			dbg_puts("\n");
			gen_terrain++;
			ui_dirty(0);
			break;
		}
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
