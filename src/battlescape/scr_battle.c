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
#include "loader.h"

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
static int16_t vx_lo, vx_hi;         // мировой диапазон X, который сейчас нарисован (Y — vy_lo, vy_hi)
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
static uint8_t __at(0xB400) tile_tab[256 * TE_SIZE];

// Юниты на карте (отладка генератора, тумана войны пока нет). Спрайт собирается как в
// UnitSprite::drawRoutine0 — ноги, торс и руки по направлению; лист UNIT_* даёт 32 кадра
// в порядке: 0-7 левая рука, 8-15 правая, 16-23 ноги, 24-31 торс.
#define MAX_UNITS 12
#define UNIT_PARTS 4
typedef struct { uint8_t x, y, z, dir, alive, kneel; uint8_t tu, tu_max, hp, hp_max, en, mor; } unit_t;
static unit_t __at(0xBE00) units[MAX_UNITS];
static uint8_t nunits;
static uint8_t sel;                      // выбранный боец (панель показывает его состояние)
static uint8_t unit_page = PG_NONE, unit_np;
// Память банка (08 §4): units до #BE84, поэтому лист кадров начинается с #BF00 и занимает
// страницу до конца (#BFFF). Раньше он стоял с #BE40 и при семи и больше бойцах затирался
// ими — кадры блитились из случайной памяти, и отряд выглядел цветным мусором.
static uint8_t __at(0xBF00) unit_tab[32 * TE_SIZE];   // кадры листа в виде для блита
// Ноги на колене — восемь кадров листа (32..39), они идут после позы стоя
#define KNEEL_TU_DOWN  4                 // цена приседания и вставания (BattlescapeGame::kneel)
#define KNEEL_TU_UP    8
#define KNEEL_DY       4                 // всё, кроме ног, опускается на 4 точки (offYKneel)
static uint8_t __at(0xBEB0) kneel_tab[8 * TE_SIZE];
// Бойцы TFTD рисуются правилом 13 (armors.rul STR_NONE_UC drawingRoutine): на колене всё, кроме
// ног, ниже на offYKneel + 1, при ходьбе торс качается на YoffWalk + 1 (UnitSprite.cpp:341, 493)
static uint8_t u_tftd;

// Ходьба (BattleUnit::startWalking / keepWalking): идёт один боец за раз. Фаза растёт раз в
// 30 мс (Options::battleXcomSpeed); на середине пути позиция бойца уже клетка назначения —
// «рисовальный» переход оригинала, к концу он стоит. По диагонали фаз вдвое больше.
static uint8_t wk_u = 0xFF;              // кто идёт (#FF — никто)
static uint8_t wk_ph, wk_mid, wk_end;    // фаза, середина и конец
static uint8_t wk_lx, wk_ly, wk_dx, wk_dy, wk_z;   // откуда и куда (этаж тот же)
static uint8_t wk_acc;                   // мс к следующей фазе
static uint16_t wk_fr;                   // кадр прошлого такта
static int16_t wk_ox, wk_oy;             // смещение спрайта от клетки, где боец числится (Map::calculateWalkingOffset)
extern volatile uint16_t frames;         // crt0.s
// Рабочая страница боя: массивы поиска пути, свойства частей и таблицы графики миссии
static uint8_t pf_page = PG_NONE;                // страница под рабочие массивы поиска пути

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
uint8_t dl_page = PG_NONE;               // первая из двух подряд (и scr_battle_s.s)
uint16_t dl_n;                       // записей в списке
static uint8_t dl_ok;                // список отвечает текущему виду

// Сплит-экран (17_battle_render.md §3): карта живёт в своём холсте и показывается смещениями
// GXOffs/GYOffs, а панель ICONS остаётся в основном холсте — на строке VIEW_H-1 строчное
// прерывание (crt0.s) переключает холст и смещения на панельные. Так камеру можно двигать
// регистрами, не перерисовывая ни карту, ни панель.
// GYOffs — не постоянное смещение, а значение, которым перезагружается счётчик строки
// видеопамяти; дальше он растёт сам. Поэтому на разрезе ставится номер строки панели, а
// смещения карты возвращаются за SPLIT_LEAD строк до картинки (запас на длинный обработчик
// кадра со счётчиком и вводом) — с поправкой на эти строки.
// Строка кадра, с которой идёт картинка 320x200 (50 Гц, кадр 320 строк: 32 гашения + 44 бордюра):
// REF/fpga/video/video_mode.v:176 (PENT_312 в tune.v выключен), в эмуляторе — raster R_320_200
// (unreal/.../video/draw.cpp). VSInt = v приходит на строке v, новое GYOffs — со следующей. Было 73:
// разрез на панель попадал на строку картинки 141, и под панелью три строки показывали буфер карты.
#define SPLIT_TOP  76
#define SPLIT_LEAD 0                 // за сколько строк до картинки вернуть смещения карты
// Карта — кольцо 512x512 на весь свой холст (раскладка Б, 17 §3): мировая точка (x, y) лежит в
// столбце (x + BIAS) & 511 и строке (y + BIAS) & 511, камера — только GXOffs/GYOffs. Нарисован
// прямоугольник мира [vx_lo, vx_hi] x [vy_lo, vy_hi] (не больше кольца): пока окно внутри него,
// движение камеры — запись смещений без единого блита, вышло — дорисовываются въехавшие полосы.
// Полная сборка — окно с запасом PAD (с вылетом спрайтов за край — меньше кольца).
#define RING    512
#define PAD_X   80                   // 320 + 2*80 + 32 вылета = 512
#define PAD_Y   56                   // 144 + 2*56 = 256
#define STRIP_X 48                   // полоса дорисовывается с запасом вперёд — реже и дешевле
#define STRIP_Y 32
#define BIAS    1024                 // мировые X и Y — в ±1024; сдвиг делает их положительными
extern uint8_t split_gx, split_gy, split_on, split_phase, split_mark, map_canvas;
extern uint16_t map_gx, map_gy, split_line, vblank_line;
extern uint8_t in_prev_keys;             // input.s: клавиша, нажатая сейчас (0 — нет)

// Два холста карты (16 §8.3h п. 2, map.h CANVAS2_PAGE и CANVAS3_PAGE): видимый и скрытый.
// Полная перерисовка собирается в скрытом и показывается сменой холста в прерывании (crt0.s
// _map_canvas вместе со смещениями) — экран не гаснет и не собирается на глазах. Частичная тоже
// собирается в скрытом: прямоугольник гасится, все задевающие его клетки рисуются целиком, а в
// видимый уходит только прямоугольник одним DMA — спрайты соседей за его краем не ложатся поверх
// уже нарисованного в обход порядка художника. Вне прямоугольника скрытый холст — черновик.
#define CANVAS_X  (CANVAS2_PAGE ^ CANVAS3_PAGE)   // видимый ^ CANVAS_X — скрытый
uint8_t vis_pg;                          // первая страница видимого холста (0 — ещё не выбран)
uint8_t tgt_d;                           // и того, куда рисуют ring_fill и dl_run
static uint8_t away;                     // поверх боя открывали экран (инвентарь): сплит снят
static int16_t vy_lo, vy_hi;             // нарисованный мировой диапазон Y (X — vx_lo, vx_hi)

static void center_on_unit(void);       // определена ниже, вместе с юнитами
static void cell_repaint(uint8_t x, uint8_t y);   // перерисовка одной клетки (ниже)
static void walk_paint(void);                     // перерисовка шага идущего бойца (ниже)
static void repaint(int16_t lo, int16_t hi, int16_t top, int16_t bot);   // кусок вида (ниже)

// Включить сплит: карта — из её холста, панель — из основного, со своей строки 144
static void split_start(void)
{
	split_on = 0;                        // на геоскейпе сплит включён (метка луча): пока меняем — выкл.
	map_gx = 0;                          // холст пока чёрный целиком — смещения любые
	map_gy = 0;
	split_gx = 0;
	split_gy = VIEW_H;                   // после разреза показываются строки панели (144..199)
	split_line = SPLIT_TOP + VIEW_H - 1;
	vblank_line = SPLIT_TOP - 1 - SPLIT_LEAD;
	split_phase = 0;
	split_mark = 0;                      // глобус включал метку луча без смещений (globe.s blit)
	map_canvas = vis_pg - SCREEN_PAGE;
	split_on = 1;
	TS_VSINTL = (uint8_t)split_line;
	TS_VSINTH = (uint8_t)(split_line >> 8);
}

static void split_stop(void)
{
	split_on = 0;
	TS_VSINTL = 0;                       // кадровое прерывание — снова в начале кадра
	TS_VSINTH = 0;
	map_canvas = 0;                      // глобус (метка луча) берёт холст отсюда же
	TS_VPAGE = SCREEN_PAGE;
	TS_GXOFFSL = 0; TS_GXOFFSH = 0;
	TS_GYOFFSL = 0; TS_GYOFFSH = 0;
}

static void dma_wait(void)
{
	while (TS_DMASTATUS & DMASTATUS_ACT)
		;
}

// Прямоугольник кольца [lo, hi] x [top, bot] (мировые) — scr_battle_s.s: ring_fill гасит его в
// холсте tgt_d (DMA FILL), ring_copy переносит из скрытого холста в видимый (2D DMA)
void ring_fill(int16_t lo, int16_t hi, int16_t top, int16_t bot);
void ring_copy(int16_t lo, int16_t hi, int16_t top, int16_t bot);

// Погасить весь холст tgt_d (512x512 = 256 КБ, gfx_fill режет по видимому экрану)
#define fill_buf() ring_fill(-BIAS, RING - 1 - BIAS, -BIAS, RING - 1 - BIAS)

// Кадр (запись tile_tab/unit_tab) -> команды списка: scr_battle_s.s. Вызывать, когда в Win3
// подключена страница списка (build_range переключает по рядам). dl_blit — кадр целиком (x —
// мировая, y — мировая), dl_blit_clip — в прямоугольнике [cx0, cx1) x [cy0, cy1)
void dl_blit(const uint8_t *e, int16_t x, int16_t y);
void dl_blit_clip(const uint8_t *e, int16_t x, int16_t y, int16_t cx0, int16_t cx1, int16_t cy0, int16_t cy1);
uint16_t dl_over;                        // сколько команд не влезло в список (диагностика)
void dl_run(void);                       // прогон списка в холст tgt_d (scr_battle_s.s)

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
	HOT(48, 144, 32, 16, A_CUSTOM, 1, 0),            // боец выше по списку
	HOT(48, 160, 32, 16, A_CUSTOM, 2, 0),            // боец ниже
	HOT(80, 144, 32, 16, A_CUSTOM, 3, 'q'),          // этаж вверх (и Q)
	HOT(80, 160, 32, 16, A_CUSTOM, 4, 'a'),          // этаж вниз (и A)
	HOT(112, 160, 32, 16, A_CUSTOM, 10, 'k'),        // присесть и встать
	HOT(144, 144, 32, 16, A_CUSTOM, 5, 'i'),         // инвентарь
	HOT(144, 160, 32, 16, A_CUSTOM, 6, 'c'),         // центрировать на бойце
	HOT(176, 144, 32, 16, A_CUSTOM, 7, 0),           // следующий боец
	HOT(208, 144, 32, 16, A_CUSTOM, 9, 0),           // показывать все этажи или только текущий
	HOT(240, 144, 32, 16, A_CUSTOM, 8, 0),           // конец хода
	HOT(240, 160, 32, 16, A_POP, 0, ESC),          // выйти из боя
};

static const scr_t tab[] = {
	SCR(SCR_BATTLE, UI_SCR_SAVEMENUS, RES_PAL_BATTLESCAPE, 0, SF_RAWPAL, w_battle),
};

// Точка экрана -> клетка (Camera::convertScreenToMap оригинала: к точке добавляется -32/2 и 24 на
// каждый этаж, дальше косой пересчёт); 1 — клетка на поле. Клетке принадлежат нижние 16 строк её
// тайла 32x40 — там же рисуется ромб курсора: клетка ровно та же, что в оригинале.
static uint8_t screen_cell(int16_t mx, int16_t my, int16_t *ox, int16_t *oy)
{
	int16_t a = mx - cam_x;                              // (x - y) * 16
	int16_t b = my - cam_y + (int16_t)level * 24 - TILE_W / 2;   // (x + y) * 8
	int16_t x = (a + 2 * b - 32) / 32, y = (2 * b - a) / 32;
	*ox = x; *oy = y;
	return x >= 0 && y >= 0 && x < m_sx && y < m_sy;
}

static void clamp_cam(void)
{
	// Camera::scrollXY: клетка под серединой окна должна быть на поле — иначе камера шаг за шагом
	// возвращается вдоль осей карты (по экрану ось — 2 точки по X на 1 по Y; X — чётный, DMA)
	cam_x &= ~1;
	for (uint8_t i = 0; i < 255; i++) {
		int16_t x, y;
		if (screen_cell(SCREEN_W / 2, VIEW_H / 2, &x, &y)) break;
		if (x < 0) { cam_x -= 2; cam_y--; }
		else if (x >= m_sx) { cam_x += 2; cam_y++; }
		else if (y < 0) { cam_x += 2; cam_y--; }
		else if (y >= m_sy) { cam_x -= 2; cam_y++; }
		else break;                      // (не бывает: на поле — выход выше)
	}
}

// Камера в центр карты: середина поля попадает в середину окна
static void center_at(int16_t cx, int16_t cy)
{
	cam_x = 160 - (cx - cy) * 16;
	cam_y = VIEW_H / 2 - ((cx + cy) * 8 - (int16_t)level * 24);
	dl_ok = 0;
	clamp_cam();
}

static void center(void)
{
	center_at(m_sx / 2, m_sy / 2);
}

// Вид — в дисплей-лист (build_range ниже): уровни снизу вверх, ряды Y, внутри ряда X, внутри
// клетки — пол, западная стена, северная стена, объект (порядок художника, Map.cpp:626-966).
// Мировой точке отвечает точка кольца ((X + BIAS) & 511, (Y + BIAS) & 511): при движении камеры
// ничего не переезжает — дорисовываются только въехавшие полосы.

// Номер бойца в клетке + 1 (0 — никого): по нему выбирают щелчком и красят рамку курсора
static uint8_t unit_no(uint8_t z, uint8_t y, uint8_t x)
{
	for (uint8_t i = 0; i < nunits; i++) {
		const unit_t *u = &units[i];
		uint8_t uz = u->z, uy = u->y, ux = u->x;
		if (u->alive && uz == z && uy == y && ux == x) return (uint8_t)(i + 1);
	}
	return 0;
}

// Части бойца в порядке наложения по направлению (UnitSprite::drawRoutine0, без предметов в
// руках): 0 — левая рука, 1 — правая, 2 — ноги, 3 — торс
static const uint8_t part_order[8][4] = {
	{ 0, 2, 3, 1 }, { 0, 2, 3, 1 }, { 0, 2, 3, 1 }, { 2, 3, 0, 1 },
	{ 2, 1, 3, 0 }, { 1, 2, 3, 0 }, { 1, 2, 3, 0 }, { 0, 1, 2, 3 },
};
static const int8_t walk_bob[8] = { 1, 0, -1, 0, 1, 0, -1, 0 };   // YoffWalk: торс при ходьбе

// Боец (кадр в точке px, py): стоя — руки, ноги и торс листа; на колене — свои ноги, остальное
// ниже; идущий — кадры фазы ходьбы (40 + 24·d: руки и ноги по восемь фаз, читаются из рабочей
// страницы), торс качается, всё обрезано маской клетки [cx0, cx1) x [cy0, cy0 + 72)
static void draw_unit(const unit_t *u, uint8_t walking, int16_t px, int16_t py, int16_t cx0, int16_t cx1, int16_t cy0)
{
	uint8_t d = u->dir & 7, ph = wk_ph & 7, e[TE_SIZE];
	int16_t dy = walking ? walk_bob[ph] + u_tftd : u->kneel ? KNEEL_DY + u_tftd : 0;
	for (uint8_t k = 0; k < 4; k++) {
		uint8_t p = part_order[d][k];
		const uint8_t *f;
		int16_t y = py;
		if (p == 3) { f = unit_tab + (uint16_t)(24 + d) * TE_SIZE; y += dy; }
		else if (walking) {
			far_read(FAR(pf_page + 1, TL_SHEET) + (uint16_t)(40 + p * 8 + 24 * d + ph) * TE_SIZE, e, TE_SIZE);
			f = e;
		} else if (p == 2) f = u->kneel ? kneel_tab + (uint16_t)d * TE_SIZE : unit_tab + (uint16_t)(16 + d) * TE_SIZE;
		else { f = unit_tab + (uint16_t)(p * 8 + d) * TE_SIZE; y += dy; }
		if (walking) dl_blit_clip(f, px, y, cx0, cx1, cy0, cy0 + 72);
		else dl_blit(f, px, y);
	}
}

// Маска идущего бойца в клетке (x, y) — Map::drawUnit: 1 — вся колонка клетки, 2 — левая
// половина, 3 — правая, 0 — эта клетка его не рисует. В клетку назначения он входит, из прежней
// выходит; соседние по диагонали (направления 1 и 5) рисуют его целиком.
static const uint8_t wm_dest[8] = { 3, 3, 1, 1, 1, 2, 2, 0 };
static const uint8_t wm_last[8] = { 1, 2, 2, 0, 3, 3, 1, 1 };

static uint8_t walk_mask(uint8_t x, uint8_t y, uint8_t d)
{
	uint8_t lx = wk_lx, ly = wk_ly, dx = wk_dx, dy = wk_dy;   // сравнения — с копиями (SDCC 4.5)
	if (x == dx && y == dy) return wm_dest[d];
	if (x == lx && y == ly) return wm_last[d];
	if (d == 1 && ((dx == x && dy + 1 == y) || (lx + 1 == x && ly == y))) return 1;
	if (d == 5 && ((dx + 1 == x && dy == y) || (lx == x && ly + 1 == y))) return 1;
	return 0;
}

// Бойцы, которых рисует клетка (Map::drawTerrain). После её содержимого (back = 0) — стоящий в
// ней и идущий, если он числится в ней или в клетке «перед» ней (frontPos: (-1,+1), (0,+1),
// (+1,+1), (+1,0), (+1,-1)); сразу после пола (back = 1) — идущий из клеток «за» ней (backPos:
// (0,-1), (-1,-1), (-1,0)). Так идущего между клетками закрывает то, что ближе к зрителю.
// Наборы — биты по (ay + 1) * 3 + ax + 1.
static uint8_t __at(0xB340) rl[MAX_UNITS];   // бойцы текущего ряда сборки (build_range) — память банка
static uint8_t __at(0xB34C) rn;

static void draw_units_cell(uint8_t y, uint8_t x, int16_t px, int16_t py, uint8_t back)
{
	uint8_t wu = wk_u;
	uint16_t set = back ? 0x00B : 0x1F4;
	for (uint8_t k = 0; k < rn; k++) {
		uint8_t i = rl[k];
		const unit_t *u = &units[i];
		int8_t ax = (int8_t)(u->x - x), ay = (int8_t)(u->y - y);
		if (i != wu) {
			if (!back && !ax && !ay) draw_unit(u, 0, px, py, 0, 0, 0);
			continue;
		}
		if (ax < -1 || ax > 1 || ay < -1 || ay > 1 || !((set >> ((ay + 1) * 3 + ax + 1)) & 1)) continue;
		uint8_t m = walk_mask(x, y, u->dir & 7);
		if (!m) continue;
		draw_unit(u, 1, px + (ax - ay) * 16 + wk_ox, py + (ax + ay) * 8 + wk_oy,
			  m == 3 ? px + 16 : px, m == 2 ? px + 16 : px + TILE_W, py - 32);
	}
}

// Разложить в список клетки, чей спрайт (32x40 от угла клетки) задевает мировой прямоугольник
// [lo, hi] x [top, bot]; части с P_Level подняты до 24 точек — берутся и клетки до 24 строк ниже.
// Мировые X и Y клетки — (x - y) * 16 и (x + y) * 8 - z * 24, от камеры не зависят.
static void build_range(int16_t lo, int16_t hi, int16_t top, int16_t bot)
{
	uint8_t wu = wk_u;
	dl_n = 0;
	bot += 24;
	int16_t dlo = (lo - TILE_W + 1 + 15) >> 4, dhi = hi >> 4;   // x - y для краёв полосы
	for (uint8_t z = all_levels ? 0 : level; z <= level; z++) {
		// Какие ряды могут задеть прямоугольник: по X полоса задаёт x - y = d из [dlo, dhi],
		// значит Y = (2y + d) * 8 - z * 24, и из top - 40 < Y <= bot выводятся границы y.
		int16_t b = -(int16_t)z * 24;
		int16_t ylo = (top - TILE_H + 1 - 8 * dhi - b + 15) >> 4;
		int16_t yhi = (bot - 8 * dlo - b) >> 4;
		if (ylo < 0) ylo = 0;
		if (yhi >= m_sy) yhi = m_sy - 1;
		// Адрес ряда ведётся сложением: 32-битное умножение на каждый ряд стоило дороже,
		// чем всё остальное в этом цикле вместе взятое.
		uint16_t row_bytes = (uint16_t)m_sx * 4;
		far_t rbase = cells + (uint32_t)(((uint32_t)z * m_sy + ylo) * m_sx) * 4;
		for (int16_t y = ylo; y <= yhi; y++, rbase += row_bytes) {
			int16_t ry = (int16_t)y * 8 - (int16_t)z * 24;   // мировая Y клетки x этого ряда: ry + x*8
			int16_t rx = -(int16_t)y * 16;   // мировая X клетки x этого ряда: rx + x*16
			// Отрезок ряда: из lo-32 < wx <= hi и top-40 < wy <= bot получаем границы x
			int16_t x0 = (lo - TILE_W + 1 - rx + 15) >> 4, x1 = (hi - rx) >> 4;
			int16_t t0 = (top - TILE_H + 1 - ry + 7) >> 3, t1 = (bot - ry) >> 3;
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
			// Кого рисовать в этом ряду: бойцы ряда и идущий из соседнего — клетка перебирает
			// только их (раньше — всех бойцов на каждую клетку, треть времени шага)
			uint8_t ru = 0;
			for (uint8_t i = 0; i < nunits && unit_page != PG_NONE; i++) {
				const unit_t *u = &units[i];
				uint8_t uy = u->y;
				if (u->alive && u->z == z && (uy == y || (i == wu && (uy + 1 == y || uy == y + 1)))) rl[ru++] = i;
			}
			rn = ru;
			for (int16_t x = x0; x <= x1; x++, p += 4, px += 16, py += 8) {
				uint8_t cx = cur_cx, cy = cur_cy;   // сравнения байтов — с копиями (SDCC 4.5)
				uint8_t here = cur_on && z == level && (uint8_t)x == cx && (uint8_t)y == cy;
				// на клетке боец — рамка жёлтая, пустая клетка — красная (как в оригинале)
				uint8_t cf = here && unit_no(z, (uint8_t)y, (uint8_t)x) ? 1 : 0;
				for (uint8_t k = 0; k < 4; k++) {
					uint8_t t = p[k];
					if (t) dl_tile((uint8_t)(t - 1), px, py - (int16_t)tile_y[t - 1]);   // px — мировая X
					if (k) continue;
					// после пола (Map.cpp:668-721): задняя половина рамки курсора и идущий боец
					// из клеток «за» этой — до её стен, чтобы стены его закрывали
					if (here) dl_blit(cur_tab + cf * TE_SIZE, px, py);
					if (ru && wu != 0xFF) draw_units_cell((uint8_t)y, (uint8_t)x, px, py, 1);
				}
				// Боец идёт сразу за своей клеткой: клетки правее и ниже рисуются позже и
				// закрывают его — иначе он виден сквозь стены и закрытые двери.
				if (ru) draw_units_cell((uint8_t)y, (uint8_t)x, px, py, 0);
				if (here) dl_blit(cur_tab + (2 + cf) * TE_SIZE, px, py);    // передняя половина
			}
			pg_map3(old);
		}
	}
}

// Полная сборка: окно камеры с запасом PAD по краям
static void build_list(void)
{
	vx_lo = -cam_x - PAD_X;
	vx_hi = -cam_x + SCREEN_W - 1 + PAD_X;
	vy_lo = -cam_y - PAD_Y;
	vy_hi = -cam_y + VIEW_H - 1 + PAD_Y;
	build_range(vx_lo, vx_hi, vy_lo, vy_hi);
	dl_ok = 1;
}

// Камера в кольце: показать нужное место парой регистров (и холст — разом с ними)
static void show_cam(void)
{
	uint16_t gx = (uint16_t)(BIAS - cam_x) & (RING - 1);
	uint16_t gy = (uint16_t)(BIAS - cam_y - SPLIT_LEAD) & (RING - 1);
	__asm__("di");                       // прерывание берёт холст и смещения одним кадром
	map_gx = gx;
	map_gy = gy;
	map_canvas = vis_pg - SCREEN_PAGE;
	__asm__("ei");
}


// Лист юнита в память и в готовые для блита записи (как тайлсет карты)
// Лист спрайтов грузит банк 30 (loader.c): страницу берёт он, таблицу кадров кладёт
// в дальнюю память, а сюда, в память банка, она переносится одним чтением.
// n — сколько кадров разобрать, ncopy — сколько записей перенести в tab. Разделены не зря:
// у листа брони кадров 40, а в unit_tab влезает 32 (хвост #BF00..#BFFF), и копия «на всё»
// уезжала за #C000 — прямо в окно Win3, где в этот миг лежат таблицы частей миссии.
static uint8_t load_sheet(uint16_t res, uint8_t *pg, uint8_t *np, uint8_t n,
			  const uint8_t *want, uint8_t *tab, uint8_t ncopy)
{
	if (pf_page == PG_NONE) return 0;
	// Переменные банка (кроме __at-массивов) лежат в Win1, а он подключён всегда — значит
	// указатели на них можно отдавать и в другой банк.
	if (!sheet_load(res, pg, np, n, want, FAR(pf_page + 1, TL_SHEET))) return 0;
	far_read(FAR(pf_page + 1, TL_SHEET), tab, (uint16_t)ncopy * TE_SIZE);
	return 1;
}

static uint8_t load_unit_sheet(uint16_t id)
{
	if (unit_page != PG_NONE) { pg_free(unit_page, unit_np); unit_page = PG_NONE; }
	u_tftd = res_game() == 2;
	// 232 кадра: стойка и колено (0..39) — в память банка, ходьба (40..231) остаётся в рабочей
	// странице (TL_SHEET) и читается при сборке вида
	if (!load_sheet(id, &unit_page, &unit_np, 232, 0, unit_tab, 32)) return 0;
	far_read(FAR(pf_page + 1, TL_SHEET) + 32 * TE_SIZE, kneel_tab, sizeof kneel_tab);
	return 1;
}

// Лист курсора клетки: из CURSOR.PCK берутся четыре кадра — задние и передние половины рамки
static uint8_t load_cursor(void)
{
	uint8_t want[CUR_FRAMES] = { 0, 1, 3, 4 };   // в стеке: банк вызывающего при вызове отключён
	return load_sheet(RES_CURSOR_PCK, &cur_page, &cur_np, CUR_FRAMES, want, cur_tab, CUR_FRAMES);
}

// Навести камеру на выбранного бойца (кнопка «центрировать», как в оригинале)
static void center_on_unit(void)
{
	if (!nunits) return;
	const unit_t *u = &units[sel < nunits ? sel : 0];
	center_at(u->x, u->y);
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
		unit_t *u = &units[i];
		const cunit_t *c = &cu[i];
		u->x = c->x; u->y = c->y; u->z = c->z;
		u->dir = 0; u->alive = 1; u->kneel = 0;
		u->tu = u->tu_max = c->tu;
		u->hp = u->hp_max = c->hp;
		u->en = c->en; u->mor = c->mor;
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

// Клетка под точкой экрана (screen_cell) — номерами, если она на поле
static uint8_t screen_to_cell(int16_t mx, int16_t my, uint8_t *ox, uint8_t *oy)
{
	int16_t x, y;
	if (!screen_cell(mx, my, &x, &y)) return 0;
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

// Присесть или встать (BattlescapeGame::kneel): 4 единицы времени вниз, 8 вверх.
// Сидящего бойца хуже видно и точнее стреляет — это появится вместе с видимостью и стрельбой.
static void do_kneel(void)
{
	if (!nunits || wk_u != 0xFF) return;
	unit_t *u = &units[sel < nunits ? sel : 0];
	uint8_t cost = u->kneel ? KNEEL_TU_UP : KNEEL_TU_DOWN;
	if (u->tu < cost) return;
	u->tu -= cost;
	u->kneel = !u->kneel;
	path_n = 0;
	cell_repaint(u->x, u->y);
	ui_dirty(0);
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
	if (u->kneel) {                                  // идти на коленях нельзя — сперва встать
		if (u->tu < KNEEL_TU_UP) { path_n = 0; return; }
		u->tu -= KNEEL_TU_UP;
		u->kneel = 0;
		cell_repaint(u->x, u->y);
		return;
	}
	if (u->dir != d) { turn_to(u, d); return; }      // сперва развернуться лицом к шагу
	if (door_try(u, d, 0) != 2) return;              // дверь открывается вместо шага
	int16_t nx = (int16_t)u->x + dir_dx[d], ny = (int16_t)u->y + dir_dy[d];
	if (nx < 0 || ny < 0 || nx >= m_sx || ny >= m_sy) { path_n = 0; return; }
	uint8_t cost = cell_cost(&bmap, (uint8_t)nx, (uint8_t)ny, u->z, d, STEP_TU);
	if (u->tu < cost) { path_n = 0; return; }
	uint8_t ox = u->x, oy = u->y, oz = u->z;
	uint8_t nz = cell_step_z(&bmap, ox, oy, oz, (uint8_t)nx, (uint8_t)ny);   // лестница вверх, падение вниз
	u->tu -= cost;
	if (u->en > 1) u->en--;              // энергия тратится медленнее времени
	path_i++;
	if (path_i >= path_n) path_n = 0;
	if (nz != oz) {                      // сменился этаж — пока скачком, камера едет за бойцом
		u->x = (uint8_t)nx; u->y = (uint8_t)ny; u->z = nz;   // (Camera::setViewLevel)
		level = nz;
		dl_ok = 0;
		ui_dirty(0);
		return;
	}
	// Шаг — фазами ходьбы (walk_phase): боец числится в прежней клетке до середины пути
	wk_u = sel < nunits ? sel : 0;
	wk_lx = ox; wk_ly = oy; wk_z = oz;
	wk_dx = (uint8_t)nx; wk_dy = (uint8_t)ny;
	wk_mid = 4 + 4 * (d & 1);
	wk_end = 8 + 8 * (d & 1);
	wk_ph = 0;
	wk_acc = 0;
	wk_fr = frames;
	move_wait = 0;                       // между клетками пути паузы нет
	walk_paint();
}

// Смещение спрайта на фазе (Map::calculateWalkingOffset, бойцы 1x1, без смены высоты пола):
// до середины — от прежней клетки к новой, после — от новой, где он уже числится
static const int8_t wo_x[8] = { 1, 1, 1, 0, -1, -1, -1, 0 };
static const int8_t wo_y[8] = { 1, 0, -1, -1, -1, 0, 1, 1 };

// Перерисовать прямоугольник обеих клеток шага (и строку-другую на качание торса)
static void walk_paint(void)
{
	if (wk_u != 0xFF) {
		uint8_t d = units[wk_u].dir & 7;
		int8_t k = (int8_t)(wk_ph < wk_mid ? wk_ph : wk_ph - wk_end);
		wk_ox = k * 2 * wo_x[d];
		wk_oy = -k * wo_y[d];
	}
	int16_t lx = ((int16_t)wk_lx - wk_ly) * 16, dx = ((int16_t)wk_dx - wk_dy) * 16;
	int16_t ly = ((int16_t)wk_lx + wk_ly) * 8, dy = ((int16_t)wk_dx + wk_dy) * 8;
	int16_t b = -(int16_t)wk_z * 24;
	repaint(lx < dx ? lx : dx, (lx > dx ? lx : dx) + TILE_W - 1,
		(ly < dy ? ly : dy) + b - 2, (ly > dy ? ly : dy) + b + TILE_H + 1);
}

// Следующая фаза: на середине боец переходит в новую клетку, на последней — уже стоит в ней
static void walk_phase(void)
{
	unit_t *u = &units[wk_u];
	wk_ph++;
	if (wk_ph == wk_mid) { u->x = wk_dx; u->y = wk_dy; }
	if (wk_ph >= wk_end) {
		wk_u = 0xFF;
		if (!path_n) ui_dirty(0);        // путь кончился — обновить панель
	}
	walk_paint();
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
	gen_free();
	loaded = 0;
	wk_u = 0xFF;
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
	tiles_req_t q;
	mapgen_sets(q.set, q.tset, &q.ns);
	mapgen_used(q.used);                 // какие части реально попали на карту
	q.page = pf_page;
	uint8_t pages[GEN_SETS], nps[GEN_SETS], npg = 0, nd = 0;
	uint16_t nparts = tiles_load(&q, pages, nps, &npg, &nd);
	if (!npg) return 0;
	// Таблицы собраны в дальней памяти — переносим их к себе в банк (вид частей и смещения
	// нужны в каждом кадре, дальнее чтение тут не годится).
	far_read(FAR(pf_page + 1, TL_TILES), tile_tab, sizeof tile_tab);
	far_read(FAR(pf_page + 1, TL_YOFS), tile_y, sizeof tile_y);
	for (uint8_t i = 0; i < npg; i++) { gen_page[i] = pages[i]; gen_np[i] = nps[i]; }
	gen_ns = npg;
	ndoors = nd;
	if (!gen_ns) return 0;
	m_sx = (uint8_t)mapgen_sx(); m_sy = (uint8_t)mapgen_sy(); m_sz = mapgen_sz();
	m_nt = (uint8_t)nparts;
	cells = mapgen_cells();
	map_phys = cells;
	bmap.cells = cells; bmap.sx = m_sx; bmap.sy = m_sy; bmap.sz = m_sz; bmap.page = pf_page;
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

// Перерисовать кусок вида [lo, hi] x [top, bot] (мировые): в скрытом холсте гасим прямоугольник
// кольца и собираем в него заново все клетки, которые его задевают, а в видимый переносим только
// сам прямоугольник. Гасить приходится потому, что тайлы кладутся прозрачным блитом (BLT1
// пропускает цвет 0) — без гашения под новой картинкой остаётся старая.
static void draw_rect(int16_t lo, int16_t hi, int16_t top, int16_t bot)
{
	tgt_d = vis_pg ^ CANVAS_X;
	ring_fill(lo, hi, top, bot);
	build_range(lo, hi, top, bot);
	dl_run();
	dma_wait();
	ring_copy(lo, hi, top, bot);
}

// То же внутри нарисованного (курсор клетки, шаг бойца, дверь): за его краем холст — мусор,
// его дорисуют полосы, когда туда приедет камера
static void repaint(int16_t lo, int16_t hi, int16_t top, int16_t bot)
{
	if (!dl_ok || dl_page == PG_NONE) { ui_dirty(0); return; }
	if (lo < vx_lo) lo = vx_lo;
	if (hi > vx_hi) hi = vx_hi;
	if (top < vy_lo) top = vy_lo;
	if (bot > vy_hi) bot = vy_hi;
	if (lo <= hi && top <= bot) draw_rect(lo, hi, top, bot);
}

// Перерисовать клетку (её спрайт 32x40) — столько и трогаем при переезде курсора и шаге бойца
static void cell_repaint(uint8_t x, uint8_t y)
{
	int16_t wx = ((int16_t)x - (int16_t)y) * 16;
	int16_t wy = ((int16_t)x + (int16_t)y) * 8 - (int16_t)level * 24;
	repaint(wx, wx + TILE_W - 1, wy, wy + TILE_H - 1);
}

static void draw_map(void)
{
	if (!loaded || dl_page == PG_NONE) return;
	if (dl_ok) {
		// Вид уже в холсте: камера ушла за нарисованное — дорисовать въехавшие полосы (с запасом
		// вперёд) по обеим осям; нарисованное держится не больше кольца — дальний край забывается
		int16_t lo = -cam_x, hi = lo + SCREEN_W - 1, top = -cam_y, bot = top + VIEW_H - 1;
		if (lo < vx_lo) {
			draw_rect(lo - STRIP_X, vx_lo - 1, vy_lo, vy_hi);
			vx_lo = lo - STRIP_X;
			if (vx_hi - vx_lo >= RING) vx_hi = vx_lo + RING - 1;
		}
		if (hi > vx_hi) {
			draw_rect(vx_hi + 1, hi + STRIP_X, vy_lo, vy_hi);
			vx_hi = hi + STRIP_X;
			if (vx_hi - vx_lo >= RING) vx_lo = vx_hi - RING + 1;
		}
		if (top < vy_lo) {
			draw_rect(vx_lo, vx_hi, top - STRIP_Y, vy_lo - 1);
			vy_lo = top - STRIP_Y;
			if (vy_hi - vy_lo >= RING) vy_hi = vy_lo + RING - 1;
		}
		if (bot > vy_hi) {
			draw_rect(vx_lo, vx_hi, vy_hi + 1, bot + STRIP_Y);
			vy_hi = bot + STRIP_Y;
			if (vy_hi - vy_lo >= RING) vy_lo = vy_hi - RING + 1;
		}
		show_cam();
		return;
	}
	build_list();
	tgt_d = vis_pg ^ CANVAS_X;           // собрать в скрытом холсте — на экране пока прежний вид
	fill_buf();
	dl_run();
	dma_wait();
	vis_pg = tgt_d;                      // показать: холст и смещения сменит прерывание
	show_cam();
}

// Прокрутка как в оригинале (Camera::scrollKey / scrollMouse; Map.h SCROLL_INTERVAL = 15 мс,
// Options::battleScrollSpeed = 8, battleEdgeScroll = SCROLL_AUTO): пока зажата стрелка или мышь у
// края экрана (Camera.h SCROLL_BORDER = 5), камера едет на 8 точек за 15 мс; у края возле угла
// (SCROLL_DIAGONAL_EDGE = 60) — по диагонали, вторая ось вдвое медленнее. Кадр — 20 мс: 32/3
// точки, путь копится в третях точки; по X камера сдвигается на чётное (DMA адресует словами).
static uint8_t sc_acc;                   // накопленный путь, трети точки
static uint16_t sc_fr;                   // кадр прошлого такта

static void scroll_tick(void)
{
	int8_t sx = 0, sy = 0;               // 2 — полный ход, 1 — половина; знак — куда едет карта
	uint8_t k = in_prev_keys;
	int16_t mx = cursor_x, my = cursor_y;
	uint16_t f = frames;
	uint8_t n = (uint8_t)(f - sc_fr);
	sc_fr = f;
	if (k == KEY_LEFT) sx = 2;
	else if (k == KEY_RIGHT) sx = -2;
	else if (k == KEY_UP) sy = 2;
	else if (k == KEY_DOWN) sy = -2;
	else {
		if (mx < 5 || mx >= SCREEN_W - 5) {
			sx = mx < 5 ? 2 : -2;
			sy = my < 60 ? 1 : my > 200 - 60 ? -1 : 0;
		}
		if (my < 5 || my >= 200 - 5) {
			sy = my < 5 ? 2 : -2;
			if (mx < 60 || mx > SCREEN_W - 60) { sx = mx < 60 ? 2 : -2; sy /= 2; }
		}
	}
	if (!sx && !sy) { sc_acc = 0; return; }
	if (n > 3) n = 3;                    // отставшая перерисовка: путь — не больше чем за три кадра
	uint16_t a = sc_acc + (uint16_t)n * 32;
	uint8_t s = (uint8_t)(a / 3) & ~1;
	sc_acc = (uint8_t)(a - s * 3);
	int16_t ox = cam_x, oy = cam_y;
	cam_x += (int16_t)sx * s / 2;
	cam_y += (int16_t)sy * s / 2;
	clamp_cam();
	if (cam_x != ox || cam_y != oy) draw_map();
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
	draw_map();                          // собирает в скрытом холсте и показывает разом
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
		if (pf_page == PG_NONE) pf_page = pg_alloc(2, 1);   // рабочая память боя: поиск пути и таблицы
		if (pf_page == PG_NONE) { dbg_puts("battle: no pages\n"); break; }   // иначе — в страницу #FF
		// Карта миссии собирается генератором (16 §3). Пока миссии нет, террейн берётся
		// по очереди — клавиша G дальше пересобирает карту следующего террейна.
		load_gen(gen_terrain);               // карта миссии: генератор (16 §3)
		if (!vis_pg) vis_pg = CANVAS2_PAGE;  // _DATA при старте — нули
		tgt_d = vis_pg;                      // до первой сборки видимый холст — чёрный, а не
		fill_buf();                          //   остатки прошлого боя
		away = 0;
		split_start();
		// У боя своя палитра: цвета #F0..#FF в ней тёмные, и курсор в них не виден.
		// В оригинале у боя свой цвет курсора (Mod::BATTLESCAPE_CURSOR = 144).
		cursor_color(CURSOR_BATTLESCAPE);
		load_cursor();                   // рамка клетки из CURSOR.PCK (рисуется вместе с картой)
		cur_on = 0;
		break;
	case EVT_TICK:
		if (gen_mode && loaded) scroll_tick();
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
		} else if (wk_u != 0xFF) {           // идёт: фаза раз в 30 мс, кадр — 20 мс; отставшая
			uint16_t f = frames;             //   перерисовка не копит фазы — не больше одной за такт
			uint16_t a = wk_acc + (uint16_t)(f - wk_fr) * 20;
			wk_fr = f;
			if (a >= 30) {
				a -= 30;
				if (a >= 30) a = 29;
				walk_phase();
			}
			wk_acc = (uint8_t)a;
		} else if (path_n) {                 // идём по пути: поворот раз в несколько кадров
			if (move_wait) move_wait--;
			else { move_wait = 3; step_unit(); if (!path_n) ui_dirty(0); }   // в конце — обновить панель
		}
		break;
	case EVT_DRAW:
		if (away) {                          // вернулись из окна поверх боя: оно рисовало в строки
			away = 0;                        //   0–199 и в рабочую область ui (200..) основного
			dl_ok = 0;                       //   холста — вид собрать заново, потом вернуть сплит
			draw_all();
			split_start();                   // ставит смещения по умолчанию — вернуть камеру
			show_cam();
			break;
		}
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
		if (pf_page != PG_NONE) { pg_free(pf_page, 2); pf_page = PG_NONE; }
		path_n = 0;
		turn_dir = 0xFF;
		wk_u = 0xFF;
		// Карта боя рисуется в строки холста 200.., а там же задний буфер глобуса (globe.s,
		// BACK_Y 280): после боя геоскейп обязан нарисовать планету заново, иначе на экране
		// остаются тайлы поля боя.
		globe_invalidate();
		globe_det_reset();
		gview_reset();
		globe_prepare();                 // собрать планету в задний буфер целиком, поверх следов боя
		break;
	case EVT_BUTTON:
		switch (arg) {
		case 0: {                            // клик по карте
			if (!gen_mode || !nunits || cursor_y >= VIEW_H || wk_u != 0xFF) break;   // идёт — ждать
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
			pf_req_t q = { cells, m_sx, m_sy, u->z, m_sz, u->x, u->y, tx, ty, u->tu, STEP_TU, pf_page };
			path_n = pf_find(&q, path, PATH_MAX);
			path_i = 0;
			move_wait = 0;
			break;
		}
		case 1: sel_prev(); break;           // боец выше по списку (и камера на него)
		case 2: sel_next(); break;
		case 3: if (level + 1 < m_sz) { level++; center(); ui_dirty(0); } break;
		case 4: if (level) { level--; center(); ui_dirty(0); } break;
		case 5:                              // инвентарь: окно рисуется в строки 0–199 основного
			split_stop();                    //   холста — без сплита и с ним на экране
			away = 1;
			UI_GO(A_PUSH, SCR_INVENTORY);
			break;
		case 6: center_on_unit(); ui_dirty(0); break;
		case 7: sel_next(); break;           // следующий боец
		case 10: do_kneel(); break;          // присесть и встать
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
		// Стрелки здесь не обрабатываются: камера едет плавно, пока клавиша зажата (scroll_tick)
		switch (arg) {
		case 'd': case 'D': {               // отладка: поставить бойца к первой двери
			uint8_t dx, dy, ds;
			if (!gen_mode || !nunits || wk_u != 0xFF || !door_find(&bmap, level, &dx, &dy, &ds)) break;
			unit_t *u = &units[sel < nunits ? sel : 0];
			uint8_t ox = u->x, oy = u->y;
			u->x = dx; u->y = dy; u->z = level;
			u->dir = ds == 1 ? 6 : 0;         // западная стена — лицом на запад, северная — на север
			path_n = 0;
			cell_repaint(ox, oy);
			dbg_puts("door: at "); dbg_dec(dx); dbg_puts(","); dbg_dec(dy);
			dbg_puts(" slot "); dbg_dec(ds); dbg_puts("\n");
			center_on_unit();
			break;
		}
		case 'n': sel_next(); break;         // следующий боец (в оригинале TAB)
		case 'N': sel_prev(); break;         // предыдущий (в оригинале SHIFT)
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
		ui_dirty(0);
		break;
	}
	}
	return 0;
}
