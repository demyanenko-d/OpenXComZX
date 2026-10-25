// Вид наземного боя (банк 28): отрисовка карты, юнитов, курсора клетки и стрелки, камера и
// фазы ходьбы (16_battlescape_plan.md §8, 17_battle_render.md). Логика боя — экран
// scr_battle.c (банк 31): он решает, что происходит, и говорит виду, что перерисовать (bview.h).
//
// Геометрия (Camera::convertMapToScreen): X = (x - y) * 16, Y = (x + y) * 8 - z * 24; спрайт
// клетки 32x40, вертикальное смещение части — MCD.P_Level. Порядок художника: уровни снизу
// вверх, внутри уровня — ряды Y, внутри ряда X (перекрывающие клетки идут позже), внутри
// клетки — пол, западная стена, северная стена, объект.
#include <stdint.h>
#include <string.h>
#include "tsconf.h"
#include "memmap.h"
#include "res.h"
#include "ui.h"
#include "gfx.h"
#include "input.h"
#include "far.h"
#include "pages.h"
#include "units.h"
#include "loader.h"
#include "bview.h"

#define VIEW_H     144               // окно карты; ниже — панель ICONS (56 строк)
#define TILE_W     32
#define TILE_H     40
#define MAP_MAXX   64                // предел ширины карты (буфер ряда)

static uint8_t m_sx, m_sy, m_sz;
static uint8_t level;                // этаж камеры
// Показывать все этажи снизу до текущего или только текущий: кнопка «1/2» на панели,
// в оригинале Camera::toggleShowAllLayers. С одним этажом крыша корабля не мешает смотреть
// внутрь, но верхние этажи не видны — как в оригинале.
static uint8_t all_levels = 1;
static int16_t cam_x, cam_y;         // начало координат карты на экране
static int16_t vx_lo, vx_hi;         // мировой диапазон X, который сейчас нарисован (Y — vy_lo, vy_hi)
static uint8_t __at(0xBC00) tile_y[256];   // вертикальное смещение тайла (MCD.P_Level) — память банка
static far_t cells;                  // начало клеток карты
static uint8_t loaded;
static uint8_t __at(0xBD00) row[MAP_MAXX * 4];   // ряд клеток карты (Win1 занято, стек в Win0 мал)
// Кадры тайлсета в готовом для блита виде (17_battle_render.md §2): при загрузке карты записи
// SPRSET (x, y, w, h, смещение/2) разворачиваются так, чтобы в кадре не осталось ни 32-битной
// арифметики far_t, ни делений — только сложения.
//   [0] страница источника, [1..2] смещение в ней, [3] DMALEN (w/2-1), [4] высота,
//   [5] dx, [6] dy (угол кадра в ячейке), [7] ширина
static uint8_t __at(0xB400) tile_tab[256 * TE_SIZE];

// Юниты (runit_t, bview.h): их приносит логика через рабочую страницу (bv_sync). Спрайт бойца
// X-COM собирается как в UnitSprite::drawRoutine0 — ноги, торс и руки по направлению (draw_unit).
// Память банка (build.ps1 ram = #B190): код вида кончается ниже, с #B300 — рабочие таблицы.
static runit_t __at(0xB190) units[MAX_BUNITS];
static uint8_t __at(0xB2D0) rl[MAX_BUNITS];   // юниты текущего ряда сборки (build_range)
static uint8_t __at(0xB2F8) rn;
static uint8_t nunits;
static uint8_t sel;                      // выбранный боец: над ним стрелка
static uint8_t __at(0xBF00) unit_tab[32 * TE_SIZE];   // кадры листа в виде для блита
// Ноги на колене — восемь кадров листа (32..39), они идут после позы стоя
#define KNEEL_DY       4                 // всё, кроме ног, опускается на 4 точки (offYKneel)
static uint8_t __at(0xBEB0) kneel_tab[8 * TE_SIZE];
// Бойцы TFTD рисуются правилом 13 (armors.rul STR_NONE_UC drawingRoutine): на колене всё, кроме
// ног, ниже на offYKneel + 1, при ходьбе торс качается на YoffWalk + 1 (UnitSprite.cpp:341, 493)
static uint8_t u_tftd;
// Стрелка над выбранным бойцом (Map::init, Map::drawTerrain): жёлтая (блок 1 палитры) с чёрной
// каймой, качается с кадром анимации боя (100 мс, arrowBob). Пиксели — в рабочей странице
// (TL_ARROW), запись для блита — в памяти банка; рисуется последней в сборке вида.
static uint8_t __at(0xBEF0) arrow_e[TE_SIZE];
static uint8_t ar_on;
static uint8_t fx_n;                     // спрайтов поверх вида (bv_fx, записи — loader.h TV_FX)
static int16_t ar_x, ar_y;               // где нарисована (мировые)

// Ходьба (BattleUnit::startWalking / keepWalking): идёт один юнит за раз. Фаза растёт раз в
// 30 мс (Options::battleXcomSpeed); на середине пути позиция юнита уже клетка назначения —
// «рисовальный» переход оригинала, к концу он стоит. По диагонали фаз вдвое больше.
static uint8_t wk_u = 0xFF;              // кто идёт (#FF — никто)
static uint8_t wk_ph, wk_mid, wk_end;    // фаза, середина и конец
static uint8_t wk_lx, wk_ly, wk_dx, wk_dy, wk_z;   // откуда и куда (этаж тот же)
static uint8_t wk_acc;                   // мс к следующей фазе
static uint16_t wk_fr;                   // кадр прошлого такта
static int16_t wk_ox, wk_oy;             // смещение спрайта от клетки, где юнит числится (Map::calculateWalkingOffset)
// Шаг кончился, а стойку ещё не рисовали: следующий шаг пути начинается в том же такте
// (UnitWalkBState::think), и кадр стойки между клетками мелькал бы рывком рук и ног
static uint8_t wk_pend, wk_plx, wk_ply;  // прежняя клетка законченного шага — перерисовать с новым
extern volatile uint16_t frames;         // crt0.s
static uint8_t pf_page = PG_NONE;        // рабочая страница боя (таблицы — во второй)

// Курсор клетки — как в оригинале (Map.cpp:679 и :968): рамка клетки в CURSOR.PCK разрезана
// на две части. Задняя (кадр 0) рисуется до содержимого клетки, передняя (кадр 3) — после
// бойца, поэтому рамка охватывает клетку объёмно: боец стоит внутри неё, а не поверх.
// Аппаратным спрайтом такого не сделать — спрайты TSU всегда поверх графики.
// Кадры CURSOR.PCK (в cur_tab по порядку): 0 и 1 — задняя половина рамки, 3 и 4 — передняя (на
// клетке юнит — мигают 0/1 и 3/4 раз в 100 мс: _animFrame % 2, пусто — красные 0 и 3), 6 — красный
// прицел, 7..10 — жёлтый анимированный прицел над юнитом (7 + _animFrame / 2)
#define CUR_FRAMES 9
static uint8_t __at(0xB340) cur_tab[CUR_FRAMES * TE_SIZE];   // до #B388
static uint8_t cur_on, cur_cx, cur_cy;   // курсор на клетке (cur_cx, cur_cy) текущего этажа
static uint8_t cur_aim;                  // курсор прицела (CT_AIM)
static uint8_t cur_anim;                 // _animFrame: 0..7, шаг 100 мс

// Дисплей-лист (17_battle_render.md §2): вид раскладывается в список готовых команд DMA —
// по 8 байт, ровно то, что выгружается в регистры. Строится при смене камеры, этажа или карты,
// а перерисовка становится циклом «прочитал 8 байт — записал 8 портов».
// Список живёт в двух страницах: карта миссии бывает 60x60 клеток в четыре этажа, и в одну
// страницу (2048 записей) вид перестал влезать — часть клеток просто не рисовалась.
#define DL_PAGE_N 2048                   // записей в странице (16 КБ / 8)
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
// прямоугольник мира [vx_lo, vx_hi] x [vy_lo, vy_hi] (меньше кольца на вылет спрайтов — KEEP_X, KEEP_Y): пока окно внутри него,
// движение камеры — запись смещений без единого блита, вышло — дорисовываются въехавшие полосы.
// Полная сборка — окно с запасом PAD (с вылетом спрайтов за край — меньше кольца).
#define RING    512
#define PAD_X   80                   // 320 + 2*80 + 32 вылета = 512
#define PAD_Y   56                   // 144 + 2*56 = 256
#define STRIP_X 48                   // полоса дорисовывается с запасом вперёд — реже и дешевле
#define STRIP_Y 32
// Нарисованное держится меньше кольца на вылет спрайтов за край прямоугольника: клетки, задевшие
// его, рисуются целиком, и вылет за правый край по кольцу ложится на левый край нарисованного
// (за пределами карты оставались куски земли). Вылет по X — до 31 точки, по Y — до 64 (P_Level 24,
// спрайт 40) с запасом на бойца; полная сборка (480 x 256) укладывается.
#define KEEP_X  (RING - 32)
#define KEEP_Y  (RING - 96)
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

static void walk_paint(void);                     // перерисовка шага идущего юнита (ниже)
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

// Камера на клетку: она попадает в середину окна
static void center_at(int16_t cx, int16_t cy)
{
	cam_x = 160 - (cx - cy) * 16;
	cam_y = VIEW_H / 2 - ((cx + cy) * 8 - (int16_t)level * 24);
	dl_ok = 0;
	clamp_cam();
}

// Вид — в дисплей-лист (build_range ниже): уровни снизу вверх, ряды Y, внутри ряда X, внутри
// клетки — пол, западная стена, северная стена, объект (порядок художника, Map.cpp:626-966).
// Мировой точке отвечает точка кольца ((X + BIAS) & 511, (Y + BIAS) & 511): при движении камеры
// ничего не переезжает — дорисовываются только въехавшие полосы.

// Номер юнита в клетке + 1 (0 — никого): рамка курсора на юните жёлтая
static uint8_t unit_no(uint8_t z, uint8_t y, uint8_t x)
{
	for (uint8_t i = 0; i < nunits; i++) {
		const runit_t *u = &units[i];
		uint8_t uz = u->z, uy = u->y, ux = u->x;
		if ((u->flags & RU_ALIVE) && uz == z && uy == y && ux == x) return (uint8_t)(i + 1);
	}
	return 0;
}

// Части бойца в порядке наложения по направлению (UnitSprite::drawRoutine0, без предметов в
// руках): 0 — левая рука, 1 — правая, 2 — ноги, 3 — торс, 4 — предмет в правой, 5 — в левой;
// у направлений 3, 5, 7 с двуручным в руках — свой порядок (ряды 8..10)
static const uint8_t part_order[11][6] = {
	{ 4, 5, 0, 2, 3, 1 }, { 0, 2, 5, 3, 4, 1 }, { 0, 2, 3, 5, 4, 1 }, { 2, 3, 0, 1, 4, 5 },
	{ 2, 1, 3, 0, 4, 5 }, { 1, 2, 4, 5, 3, 0 }, { 1, 4, 5, 2, 3, 0 }, { 4, 5, 0, 1, 2, 3 },
	{ 2, 3, 0, 4, 5, 1 }, { 1, 2, 3, 0, 4, 5 }, { 1, 4, 5, 0, 2, 3 },
};
// Руки, держащие предмет (кадры листа 232.. в TL_HARMS, номер — от 232): rarm1H, larm2H, rarm2H;
// у UFO 232, 240, 248, у TFTD (правило 13) 248, 232, 240
static const uint8_t harm_ufo[3] = { 0, 8, 16 }, harm_tftd[3] = { 16, 0, 8 };
// Предмет в левой руке: сдвиг кадра (offX2/offY2, у двуручного — offX6/offY6)
static const int8_t lh_x[2][8] = { { -8, 3, 5, 12, 6, -1, -5, -13 }, { 0, 6, 6, 12, -4, -5, -5, -13 } };
static const int8_t lh_y[2][8] = { { 1, -4, -2, 0, 3, 3, 5, 0 }, { -4, -4, -1, 0, 5, 0, 1, 0 } };
static const int8_t walk_bob[8] = { 1, 0, -1, 0, 1, 0, -1, 0 };   // YoffWalk: торс при ходьбе

// Боец (кадр в точке px, py): стоя — руки, ноги и торс листа; на колене — свои ноги, остальное
// ниже; идущий — кадры фазы ходьбы (40 + 24·d: руки и ноги по восемь фаз, читаются из рабочей
// страницы), торс качается, всё обрезано маской клетки [cx0, cx1) x [cy0, cy0 + 72)
// Юнит с собранным листом (пришельцы, мирные): один кадр на позу; у крупного — четверть q
static void draw_sheet(const runit_t *u, uint8_t walking, uint8_t q, int16_t px, int16_t py, int16_t cx0, int16_t cx1, int16_t cy0)
{
	uint8_t d = u->dir & 7, e[TE_SIZE], f;
	if (u->flags & RU_BIG) f = (uint8_t)(d * 4 + q);
	else f = walking ? (uint8_t)(8 + d * 8 + (wk_ph & 7)) : (u->flags & RU_KNEEL) ? (uint8_t)(72 + d) : d;
	far_read(FAR(pf_page + 1, TL_ASH) + (uint16_t)((u->sheet - 1) * ASH_N + f) * TE_SIZE, e, TE_SIZE);
	if (walking) dl_blit_clip(e, px, py, cx0, cx1, cy0, cy0 + 72);
	else dl_blit(e, px, py);
}

// Оружие в руках (u->pad: слоты кадров предметов правой и левой руки, loader.h TL_UHS): рука с
// предметом — свой кадр, не машет при ходьбе, качается с торсом; предмет — кадр HANDOB поверх
static void draw_unit(const runit_t *u, uint8_t walking, int16_t px, int16_t py, int16_t cx0, int16_t cx1, int16_t cy0)
{
	if (u->sheet) { draw_sheet(u, walking, 0, px, py, cx0, cx1, cy0); return; }
	uint8_t d = u->dir & 7, ph = wk_ph & 7, kn = u->flags & RU_KNEEL, e[TE_SIZE];
	int16_t dy = walking ? walk_bob[ph] + u_tftd : kn ? KNEEL_DY + u_tftd : 0;
	uint8_t hr = u->pad[0], hl = u->pad[1], two = (hr | hl) & 0x80;
	const uint8_t *hm = u_tftd ? harm_tftd : harm_ufo;
	uint8_t held_l = hl || (hr & 0x80);
	const uint8_t *ord = part_order[two && (d & 1) && d >= 3 ? 8 + ((d - 3) >> 1) : d];
	for (uint8_t k = 0; k < 6; k++) {
		uint8_t p = ord[k];
		const uint8_t *f = e;
		int16_t x = px, y = py;
		if (p >= 4) {                                // предмет: кадр слота по направлению
			uint8_t h = p == 4 ? hr : hl;
			if (!h) continue;
			far_read(FAR(pf_page + 1, TL_HAND) + (uint16_t)(((h & 0x7F) - 1) * 8 + d) * TE_SIZE, e, TE_SIZE);
			if (!e[7]) continue;
			y += dy;
			if (p == 5) { uint8_t t2 = (hl & 0x80) != 0; x += lh_x[t2][d]; y += lh_y[t2][d]; }
		} else if (p == 3) { f = unit_tab + (uint16_t)(24 + d) * TE_SIZE; y += dy; }
		else if (p < 2 && (p ? hr : held_l)) {       // рука держит предмет
			uint8_t a = p ? hm[(hr & 0x80) ? 2 : 0] : hm[1];
			far_read(FAR(pf_page + 1, TL_HARMS) + (uint16_t)(a + d) * TE_SIZE, e, TE_SIZE);
			y += dy;
		} else if (walking) {                        // машет; при двуручном в другой — качается
			far_read(FAR(pf_page + 1, TL_SHEET) + (uint16_t)(40 + p * 8 + 24 * d + ph) * TE_SIZE, e, TE_SIZE);
			if (p == 1 && (hl & 0x80)) y += dy;
		} else if (p == 2) f = kn ? kneel_tab + (uint16_t)d * TE_SIZE : unit_tab + (uint16_t)(16 + d) * TE_SIZE;
		else { f = unit_tab + (uint16_t)(p * 8 + d) * TE_SIZE; y += dy; }
		if (walking) dl_blit_clip(f, x, y, cx0, cx1, cy0, cy0 + 72);
		else dl_blit(f, x, y);
	}
}

// Маска идущего юнита в клетке (x, y) — Map::drawUnit: 1 — вся колонка клетки, 2 — левая
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

// Юниты, которых рисует клетка (Map::drawTerrain). После её содержимого (back = 0) — стоящий в
// ней и идущий, если он числится в ней или в клетке «перед» ней (frontPos: (-1,+1), (0,+1),
// (+1,+1), (+1,0), (+1,-1)); сразу после пола (back = 1) — идущий из клеток «за» ней (backPos:
// (0,-1), (-1,-1), (-1,0)). Так идущего между клетками закрывает то, что ближе к зрителю.
// Наборы — биты по (ay + 1) * 3 + ax + 1.
static void draw_units_cell(uint8_t y, uint8_t x, int16_t px, int16_t py, uint8_t back)
{
	uint8_t wu = wk_u;
	uint16_t set = back ? 0x00B : 0x1F4;
	for (uint8_t k = 0; k < rn; k++) {
		uint8_t i = rl[k];
		const runit_t *u = &units[i];
		int8_t ax = (int8_t)(u->x - x), ay = (int8_t)(u->y - y);
		if (i != wu) {
			if (back) continue;
			if (!ax && !ay) draw_unit(u, 0, px, py, 0, 0, 0);
			// крупный (2x2): клетка рисует свою четверть (Map::drawTerrain, part по смещению клетки)
			else if ((u->flags & RU_BIG) && ax <= 0 && ax >= -1 && ay <= 0 && ay >= -1)
				draw_sheet(u, 0, (uint8_t)(-ax - ay * 2), px, py, 0, 0, 0);
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
			// Кого рисовать в этом ряду: юниты ряда и идущий из соседнего — клетка перебирает
			// только их (раньше — всех юнитов на каждую клетку, треть времени шага)
			uint8_t ru = 0;
			for (uint8_t i = 0; i < nunits; i++) {
				const runit_t *u = &units[i];
				uint8_t uy = u->y;
				if ((u->flags & RU_ALIVE) && u->z == z && (uy == y || ((i == wu || (u->flags & RU_BIG)) && uy + 1 == y) || (i == wu && uy == y + 1))) rl[ru++] = i;
			}
			rn = ru;
			for (int16_t x = x0; x <= x1; x++, p += 4, px += 16, py += 8) {
				uint8_t cx = cur_cx, cy = cur_cy;   // сравнения байтов — с копиями (SDCC 4.5)
				uint8_t here = cur_on && z == level && (uint8_t)x == cx && (uint8_t)y == cy;
				// на клетке юнит — рамка мигает жёлтым, пустая клетка — красная (Map.cpp:684-696)
				uint8_t un = here && unit_no(z, (uint8_t)y, (uint8_t)x), cb = 0, cf = 2;
				if (cur_aim) cb = cf = un ? (uint8_t)(5 + (cur_anim >> 1)) : 4;
				else if (un) { cb = cur_anim & 1; cf = (uint8_t)(2 + (cur_anim & 1)); }
				for (uint8_t k = 0; k < 4; k++) {
					uint8_t t = p[k];
					if (t) dl_tile((uint8_t)(t - 1), px, py - (int16_t)tile_y[t - 1]);   // px — мировая X
					if (k) continue;
					// после пола (Map.cpp:668-721): задняя половина рамки курсора и идущий юнит
					// из клеток «за» этой — до её стен, чтобы стены его закрывали
					if (here) dl_blit(cur_tab + cb * TE_SIZE, px, py);
					if (ru && wu != 0xFF) draw_units_cell((uint8_t)y, (uint8_t)x, px, py, 1);
				}
				// Юнит идёт сразу за своей клеткой: клетки правее и ниже рисуются позже и
				// закрывают его — иначе он виден сквозь стены и закрытые двери.
				if (ru) draw_units_cell((uint8_t)y, (uint8_t)x, px, py, 0);
				if (here) dl_blit(cur_tab + cf * TE_SIZE, px, py);    // передняя половина
			}
			pg_map3(old);
		}
	}
	// стрелка над выбранным бойцом — поверх всего (Map.cpp:1185); только задевшая прямоугольник:
	// дальняя по кольцу легла бы в него же
	if (ar_on && ar_x + 9 >= lo && ar_x <= hi && ar_y + 8 >= top && ar_y <= bot) {
		uint8_t old = pg_map3(dl_n >= DL_PAGE_N ? dl_page + 1 : dl_page);
		dl_blit(arrow_e, ar_x, ar_y);
		pg_map3(old);
	}
	for (uint8_t i = 0; i < fx_n; i++) {         // пуля и вспышка (bfx.c) — поверх всего
		uint8_t e[4 + TE_SIZE];
		far_read(FAR(pf_page + 2, TV_FX + 2) + (uint16_t)i * sizeof e, e, sizeof e);
		int16_t x = (int16_t)(e[0] | (e[1] << 8)), y = (int16_t)(e[2] | (e[3] << 8));
		if (x + 40 < lo || x > hi || y + 40 < top || y > bot) continue;
		uint8_t old = pg_map3(dl_n >= DL_PAGE_N ? dl_page + 1 : dl_page);
		dl_blit(e + 4, x, y);
		pg_map3(old);
	}
}

void bv_fx(uint8_t n, int16_t lo, int16_t hi, int16_t top, int16_t bot) __banked
{
	fx_n = n;
	repaint(lo, hi, top, bot);
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

// Смещение спрайта на фазе (Map::calculateWalkingOffset, юниты 1x1, без смены высоты пола):
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

void bv_walk(uint8_t i, uint8_t lx, uint8_t ly, uint8_t dx, uint8_t dy, uint8_t z, uint8_t d) __banked
{
	wk_u = i;
	wk_z = z;
	wk_mid = 4 + 4 * (d & 1);
	wk_end = 8 + 8 * (d & 1);
	wk_ph = 0;
	wk_acc = 0;
	wk_fr = frames;
	if (wk_pend) {                       // хвост прошлого шага (прежняя клетка) — уже с фазой 0
		wk_pend = 0;
		wk_lx = wk_plx; wk_ly = wk_ply;
		wk_dx = lx; wk_dy = ly;
		walk_paint();
	}
	wk_lx = lx; wk_ly = ly;
	wk_dx = dx; wk_dy = dy;
	walk_paint();
}

// Шаг кончился, нового нет (путь кончился, поворот, дверь): стойка в клетках шага
void bv_walk_done(void) __banked
{
	if (!wk_pend) return;
	wk_pend = 0;
	walk_paint();
}

// Фаза раз в 30 мс, кадр — 20 мс; отставшая перерисовка не копит фазы — не больше одной за такт.
// На середине юнит переходит в новую клетку, на последней — уже стоит в ней.
uint8_t bv_walk_tick(void) __banked
{
	if (wk_u == 0xFF) return 0;
	uint8_t r = 0;
	uint16_t f = frames;
	uint16_t a = wk_acc + (uint16_t)(f - wk_fr) * 20;
	wk_fr = f;
	if (a >= 30) {
		a -= 30;
		if (a >= 30) a = 29;
		wk_ph++;
		if (wk_ph == wk_mid) { units[wk_u].x = wk_dx; units[wk_u].y = wk_dy; r = 1; }
		if (wk_ph >= wk_end) {               // стойку не рисуем: дальше bv_walk или bv_walk_done
			wk_u = 0xFF;
			wk_pend = 1;
			wk_plx = wk_lx; wk_ply = wk_ly;
			wk_acc = 0;
			return 2;
		}
		walk_paint();
	}
	wk_acc = (uint8_t)a;
	return r;
}

uint8_t bv_walking(void) __banked
{
	return wk_u;
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

// То же внутри нарисованного (курсор клетки, шаг юнита, дверь): за его краем холст — мусор,
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

// Перерисовать клетку (её спрайт 32x40) — столько и трогаем при переезде курсора и шаге юнита.
// Этаж — клетки, не камеры: дверь и юнит внизу, когда смотрим с этажа выше, лежат ниже на 24·dz
static void cell_repaint(uint8_t x, uint8_t y, uint8_t z)
{
	int16_t wx = ((int16_t)x - (int16_t)y) * 16;
	int16_t wy = ((int16_t)x + (int16_t)y) * 8 - (int16_t)z * 24;
	repaint(wx, wx + TILE_W - 1, wy, wy + TILE_H - 1);
}

void bv_cell(uint8_t x, uint8_t y, uint8_t z) __banked
{
	cell_repaint(x, y, z);
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
			if (vx_hi - vx_lo >= KEEP_X) vx_hi = vx_lo + KEEP_X - 1;
		}
		if (hi > vx_hi) {
			draw_rect(vx_hi + 1, hi + STRIP_X, vy_lo, vy_hi);
			vx_hi = hi + STRIP_X;
			if (vx_hi - vx_lo >= KEEP_X) vx_lo = vx_hi - KEEP_X + 1;
		}
		if (top < vy_lo) {
			draw_rect(vx_lo, vx_hi, top - STRIP_Y, vy_lo - 1);
			vy_lo = top - STRIP_Y;
			if (vy_hi - vy_lo >= KEEP_Y) vy_hi = vy_lo + KEEP_Y - 1;
		}
		if (bot > vy_hi) {
			draw_rect(vx_lo, vx_hi, vy_hi + 1, bot + STRIP_Y);
			vy_hi = bot + STRIP_Y;
			if (vy_hi - vy_lo >= KEEP_Y) vy_lo = vy_hi - KEEP_Y + 1;
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

static const uint8_t arrow_bob[8] = { 0, 1, 2, 1, 0, 1, 2, 1 };   // Map.cpp:500 arrowBob
uint8_t bat_still;                       // 1 — стрелка не качается (сверка кадров в сценариях: poke _bat_still 1)
// Стрелка — над головой выбранного бойца: середина клетки, 24 - рост (стоя 22, на колене 14 и
// ещё 2) - высота стрелки, плюс качание (Map.cpp:1185-1198); у идущего — со смещением шага.
// Сменилось место или фаза качания — перерисовать прежний и новый прямоугольники
static void arrow_tick(void)
{
	uint8_t on = 0;
	int16_t x = 0, y = 0;
	if (sel < nunits) {
		const runit_t *u = &units[sel];
		if ((u->flags & RU_ALIVE) && u->z <= level) {
			on = 1;
			x = ((int16_t)u->x - u->y) * 16 + 12;
			y = ((int16_t)u->x + u->y) * 8 - (int16_t)u->z * 24 + ((u->flags & RU_KNEEL) ? -1 : -7)
				+ (bat_still ? 0 : arrow_bob[(frames / 5) & 7]);
			if (wk_u == sel) { x += wk_ox; y += wk_oy; }
		}
	}
	int16_t ox = ar_x, oy = ar_y;
	uint8_t oon = ar_on;                 // сравнения — с копиями (SDCC 4.5)
	if (on == oon && x == ox && y == oy) return;
	ar_on = on; ar_x = x; ar_y = y;
	if (oon) repaint(ox, ox + 9, oy, oy + 8);
	if (on) repaint(x, x + 9, y, y + 8);
}

// Клетка под мышью (окно карты): 1 — на поле
static uint8_t cursor_cell(uint8_t *ox, uint8_t *oy)
{
	int16_t x, y;
	if (cursor_y >= VIEW_H || !screen_cell(cursor_x, cursor_y, &x, &y)) return 0;
	*ox = (uint8_t)x; *oy = (uint8_t)y;
	return 1;
}

void bv_tick(uint8_t s) __banked
{
	if (!loaded) return;
	sel = s;
	scroll_tick();
	// Курсор клетки: рамка вокруг клетки под мышью. Она часть картинки карты (задняя
	// половина под содержимым клетки, передняя поверх), поэтому при переезде на другую
	// клетку перерисовываются только две клетки — прежняя и новая.
	uint8_t cx = 0, cy = 0, on = cursor_cell(&cx, &cy);
	uint8_t ox = cur_cx, oy = cur_cy, oon = cur_on;
	if (on != oon || (on && (cx != ox || cy != oy))) {
		cur_cx = cx; cur_cy = cy; cur_on = on;
		if (oon) cell_repaint(ox, oy, level);
		if (on) cell_repaint(cx, cy, level);
	}
	// такт анимации курсора (100 мс): над юнитом рамка мигает, прицел крутится — перерисовать клетку
	uint8_t an = bat_still ? 0 : (uint8_t)((frames / 5) & 7), oa = cur_anim;
	if (an != oa) {
		cur_anim = an;
		if (on && (cur_aim || unit_no(level, cy, cx))) cell_repaint(cx, cy, level);
	}
	arrow_tick();                        // стрелка над выбранным — после шага и прокрутки
}

void bv_aim(uint8_t aim) __banked
{
	cur_aim = aim;
	if (cur_on) cell_repaint(cur_cx, cur_cy, level);
}

uint8_t bv_cursor(uint8_t *ox, uint8_t *oy) __banked
{
	return cursor_cell(ox, oy);
}

void bv_open(void) __banked
{
	if (dl_page == PG_NONE) dl_page = pg_alloc(2, 1);   // список вида: две страницы подряд
	if (!vis_pg) vis_pg = CANVAS2_PAGE;  // _DATA при старте — нули
	tgt_d = vis_pg;                      // до первой сборки видимый холст — чёрный, а не
	fill_buf();                          //   остатки прошлого боя
	away = 0;
	split_start();
	// У боя своя палитра: цвета #F0..#FF в ней тёмные, и курсор в них не виден.
	// В оригинале у боя свой цвет курсора (Mod::BATTLESCAPE_CURSOR = 144).
	cursor_color(CURSOR_BATTLESCAPE);
	cur_on = 0;
}

void bv_close(void) __banked
{
	split_stop();
	cur_on = 0;
	cursor_color(CURSOR_GEOSCAPE);       // вернуть цвет курсора геоскейпа
	loaded = 0;
	nunits = 0;
	if (dl_page != PG_NONE) { pg_free(dl_page, 2); dl_page = PG_NONE; }
	dl_ok = 0;
	wk_u = 0xFF;
	ar_on = 0;
}

// Таблицы вида из рабочей страницы (их собрала загрузка миссии, bload.c): вид частей и смещения
// нужны в каждом кадре — дальнее чтение тут не годится. Номер части в клетке — как в MAP
// оригинала: 0 пусто, иначе номер части миссии; рендер берёт tile_tab[t - 1].
uint8_t bv_load(const bvload_t *q) __banked
{
	far_t t = FAR(q->page + 1, 0);
	pf_page = q->page;
	far_read(t + TL_TILES, tile_tab, sizeof tile_tab);
	far_read(t + TL_YOFS, tile_y, sizeof tile_y);
	far_read(t + TL_SHEET, unit_tab, sizeof unit_tab);           // стойка (0..31)
	far_read(t + TL_SHEET + 32 * TE_SIZE, kneel_tab, sizeof kneel_tab);   // колено (32..39)
	far_read(t + TL_CURSOR, cur_tab, sizeof cur_tab);
	far_read(t + TL_ARROWE, arrow_e, TE_SIZE);
	m_sx = q->sx; m_sy = q->sy; m_sz = q->sz;
	cells = q->cells;
	u_tftd = res_game() == 2;
	level = 0;                           // камера начинается на земле, как в оригинале
	wk_u = 0xFF;
	ar_on = 0;
	nunits = 0;
	dl_ok = 0;
	center_at(m_sx / 2, m_sy / 2);
	loaded = 1;
	return 1;
}

void bv_sync(uint8_t n) __banked
{
	if (n > MAX_BUNITS) n = MAX_BUNITS;
	nunits = n;
	if (pf_page == PG_NONE || !n) return;
	far_read(FAR(pf_page + 1, TL_RUNITS), units, (uint16_t)n * sizeof(runit_t));
	for (uint8_t i = 0; i < n && i < MAX_UNITS; i++)   // оружие в руках бойцов (bitems.c)
		far_read(FAR(pf_page + 1, TL_UHS) + i * 2, units[i].pad, 2);
}

void bv_tile(uint8_t part) __banked
{
	uint16_t o = (uint16_t)(part - 1) * TE_SIZE;
	far_read(FAR(pf_page + 1, TL_TILES) + o, tile_tab + o, TE_SIZE);
}

void bv_draw(void) __banked
{
	if (away) {                          // вернулись из окна поверх боя: оно рисовало в строки
		away = 0;                        //   0–199 и в рабочую область ui (200..) основного
		dl_ok = 0;                       //   холста — вид собрать заново, потом вернуть сплит
		draw_map();
		split_start();                   // ставит смещения по умолчанию — вернуть камеру
		show_cam();
		return;
	}
	draw_map();                          // собирает в скрытом холсте и показывает разом
}

void bv_redraw(void) __banked
{
	dl_ok = 0;
}

void bv_suspend(void) __banked
{
	split_stop();                        // окно рисуется в строки 0–199 основного холста —
	away = 1;                            //   без сплита и с ним на экране
}

// Окно поверх боя, сквозь которое видна карта (меню действий): видимый кусок кольца копируется в
// строки 0–143 основного холста, сплит снимается — карта остаётся неподвижной картинкой
void bv_snapshot(void) __banked
{
	uint16_t gx = (uint16_t)(BIAS - cam_x) & (RING - 1), gy = (uint16_t)(BIAS - cam_y) & (RING - 1);
	uint16_t w1 = RING - gx;
	if (w1 > SCREEN_W) w1 = SCREEN_W;
	for (uint8_t r = 0; r < VIEW_H; r++) {
		uint16_t sy = (gy + r) & (RING - 1);
		far_t src = FAR(vis_pg + (sy >> 5), (sy & 31) << 9), dst = FAR(SCREEN_PAGE + (r >> 5), (uint16_t)(r & 31) << 9);
		far_copy(dst, src + gx, w1);
		if (w1 < SCREEN_W) far_copy(dst + w1, src, SCREEN_W - w1);
	}
	split_stop();
	away = 1;
}

void bv_center(uint8_t x, uint8_t y) __banked
{
	center_at(x, y);
}

uint8_t bv_level(void) __banked
{
	return level;
}

void bv_set_level(uint8_t l, uint8_t recenter) __banked
{
	level = l;
	dl_ok = 0;
	if (recenter) center_at(m_sx / 2, m_sy / 2);
}

void bv_all_levels(void) __banked
{
	all_levels = !all_levels;
	dl_ok = 0;
}

uint16_t bv_dl_n(void) __banked
{
	return dl_n;
}
