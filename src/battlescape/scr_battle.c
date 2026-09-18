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
static uint8_t __at(0xBE00) tile_y[256];   // вертикальное смещение тайла (MCD.P_Level) — память банка
static far_t cells;                  // начало клеток карты
static uint8_t loaded;
static uint8_t __at(0xBF00) row[MAP_MAXX * 4];   // ряд клеток карты (Win1 занято, стек в Win0 мал)
// Записи кадров тайлсета (SPRSET: x, y, w, h, смещение/2) — в памяти банка, чтобы блит не
// лазил за ними в дальнюю память на каждую часть клетки.
static uint8_t __at(0xB800) tile_tab[256 * 6];
static far_t tile_data;              // данные кадров тайлсета
static far_t tiles_phys;             // где сейчас лежит тайлсет (слот SD мог смениться)
static uint16_t tile_frames;

static void dma_wait(void)
{
	while (TS_DMASTATUS & DMASTATUS_ACT)
		;
}

// Кадр тайлсета в экран одним запуском DMA на полосу: источник лежит подряд (строка = w
// байт), приёмник шагает на 512 (BLT1 | D_ALGN | ASZ, 02 §6). Полоса режется по границе
// страницы экрана (32 строки), кадры с нечётным x или вылезающие за края окна — через
// gfx_sprite (CPU с отсечением).
static void blit_tile(uint8_t t, int16_t x, int16_t y)
{
	const uint8_t *e = tile_tab + (uint16_t)t * 6;
	int16_t w = e[2], h = e[3];
	if (!w) return;
	int16_t bx = x + e[0], by = y + e[1];
	int16_t step = w;                    // строка источника целиком
	far_t src = tile_data + ((uint32_t)((uint16_t)e[4] | ((uint16_t)e[5] << 8)) << 1);
	if (bx & 1) { gfx_sprite(tiles_res, t, x, y); return; }   // DMA адресует словами
	if (bx < 0) {                        // обрезка по краям окна: строки короче шага источника
		src -= bx;
		w += bx;
		bx = 0;
	}
	if (bx + w > SCREEN_W) w = SCREEN_W - bx;
	w &= ~1;
	if (w <= 0) return;
	if (by < 0) {                        // верх кадра выше окна — пропустить строки
		src += (uint32_t)(-by) * (uint16_t)step;
		h += by;
		by = 0;
	}
	if (by + h > VIEW_H) h = VIEW_H - by;   // низ окна карты: дальше панель
	if (h <= 0) return;
	uint8_t sp = FAR_PAGE(src);
	uint16_t so = FAR_OFFS(src);
	uint8_t len = (uint8_t)(w / 2 - 1);
	while (h > 0) {
		uint8_t n = 1;
		if (w == step) {                 // полная строка — источник идёт линейно: одна полоса
			n = 32 - ((uint8_t)by & 31);   // (до конца страницы экрана)
			if (n > h) n = (uint8_t)h;
		}
		uint16_t offs = (((uint16_t)by & 31) << 9) | (uint16_t)bx;
		dma_wait();
		TS_DMASAL = (uint8_t)so; TS_DMASAH = (uint8_t)(so >> 8); TS_DMASAX = sp;
		TS_DMADAL = (uint8_t)offs; TS_DMADAH = (uint8_t)(offs >> 8);
		TS_DMADAX = SCREEN_PAGE + (uint8_t)(by >> 5);
		TS_DMALEN = len;
		TS_DMANUM = n - 1;
		TS_DMACTRL = DMA_BLT1 | DMA_D_ALGN | DMA_ASZ;
		so += (uint16_t)n * (uint16_t)step;
		while (so >= 0x4000) { so -= 0x4000; sp++; }
		by += n;
		h -= n;
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
}

// Камера в центр карты: середина поля попадает в середину окна
static void center(void)
{
	int16_t cx = m_sx / 2, cy = m_sy / 2;
	cam_x = 160 - (cx - cy) * 16;
	cam_y = VIEW_H / 2 - ((cx + cy) * 8 - (int16_t)level * 24);
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
	cells = r.phys + 8 + (uint32_t)m_nt * 4;
	level = m_sz > 1 ? 1 : 0;
	center();
	loaded = 1;
	return 1;
}

// Тайлсет карты: таблица кадров — в память банка (перечитывается, если ресурс переехал)
static uint8_t load_tiles(void)
{
	res_t rs;
	if (!res_find(tiles_res, &rs)) return 0;
	if (rs.phys != tiles_phys) {
		uint8_t hh[4];
		far_read(rs.phys, hh, 4);
		tile_frames = (uint16_t)hh[0] | ((uint16_t)hh[1] << 8);
		far_read(rs.phys + 4, tile_tab, (tile_frames > 256 ? 256 : tile_frames) * 6);
		tiles_phys = rs.phys;
	}
	tile_data = rs.phys + 4 + (uint32_t)tile_frames * 6;
	return 1;
}

static void draw_map(void)
{
	res_t r;
	if (!loaded) return;
	if (!res_find((uint16_t)(RES_BATMAP0 + cur_map), &r)) return;   // слот мог смениться
	if (!load_tiles()) return;
	cells = r.phys + 8 + (uint32_t)m_nt * 4;
	for (uint8_t z = 0; z <= level; z++)
		for (uint8_t y = 0; y < m_sy; y++) {
			int16_t ry = (int16_t)y * 8 - (int16_t)z * 24 + cam_y;
			int16_t rx = cam_x - (int16_t)y * 16;
			// Видимый отрезок ряда: клетка x даёт px = rx + x*16, py = ry + x*8; из
			// -32 < px < 320 и -40 < py < 144 получаем границы x (перебирать все 40 незачем).
			int16_t x0 = (-TILE_W + 1 - rx + 15) >> 4, x1 = (319 - rx) >> 4;
			int16_t t0 = (-TILE_H + 1 - ry + 7) >> 3, t1 = (VIEW_H - 1 - ry) >> 3;
			if (t0 > x0) x0 = t0;
			if (t1 < x1) x1 = t1;
			if (x0 < 0) x0 = 0;
			if (x1 >= m_sx) x1 = m_sx - 1;
			if (x0 > x1) continue;
			far_read(cells + (((uint32_t)z * m_sy + y) * m_sx + x0) * 4, row, (uint16_t)(x1 - x0 + 1) * 4);
			const uint8_t *p = row;
			int16_t px = rx + x0 * 16, py = ry + x0 * 8;
			for (int16_t x = x0; x <= x1; x++, p += 4, px += 16, py += 8)
				for (uint8_t k = 0; k < 4; k++) {
					uint8_t t = p[k];
					if (!t) continue;
					blit_tile((uint8_t)(t - 1), px, py - (int16_t)tile_y[t - 1]);
				}
		}
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
	uint8_t sp = FAR_PAGE(tile_data);
	uint16_t so = FAR_OFFS(tile_data);
	for (uint16_t i = 0; i < BENCH_N; i++) bench_setup(so, sp, 15, 0);
	dma_wait();
}

// 512 запусков по 32 строки (1024 байта каждый): 512 КБ переноса
static void bench_big(void)
{
	uint8_t sp = FAR_PAGE(tile_data);
	uint16_t so = FAR_OFFS(tile_data);
	for (uint16_t i = 0; i < BENCH_N; i++) bench_setup(so, sp, 15, 31);
	dma_wait();
}

// 512 запусков по 8 строк (256 байт) — размер, близкий к настоящей части клетки
static void bench_tile(void)
{
	uint8_t sp = FAR_PAGE(tile_data);
	uint16_t so = FAR_OFFS(tile_data);
	for (uint16_t i = 0; i < BENCH_N; i++) bench_setup(so, sp, 15, 7);
	dma_wait();
}

static void draw_all(void)
{
	gfx_fill(0, 0, 320, VIEW_H, 0);
	draw_map();
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
		if (!load_map() && cur_map) { cur_map = 0; load_map(); }
		break;
	case EVT_DRAW:
		draw_all();
		break;
	case EVT_CLOSE:                      // следующий бой — на следующей карте (генератора ещё нет)
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
