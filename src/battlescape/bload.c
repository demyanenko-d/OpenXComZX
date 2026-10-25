// Загрузка миссии для экрана боя (банк 30): банк экрана 28 занят отрисовкой и полон, поэтому всё,
// что нужно один раз при входе в бой и по клавише G, собирается здесь и складывается в рабочую
// страницу боя (loader.h TL_*) — экран забирает к себе по одному чтению на таблицу.
// Генератор карты (mapgen.c, 16 §3) -> наборы частей (loader.c) -> лист бойца с ходьбой и
// рамка курсора клетки -> отряд (units.c) -> стрелка над выбранным (Map::init).
#include <stdint.h>
#include <string.h>
#include "pages.h"
#include "far.h"
#include "res.h"
#include "res_ids.h"
#include "mapgen.h"
#include "loader.h"
#include "units.h"

static uint8_t gen_page[TL_SETS], gen_np[TL_SETS], gen_ns;   // страницы тайлсетов наборов
static uint8_t unit_page = PG_NONE, unit_np;                // лист бойца
static uint8_t cur_page = PG_NONE, cur_np;                  // рамка курсора клетки
static uint8_t hand_page = PG_NONE;                         // пиксели кадров оружия в руках (bitems.c)
static uint8_t fx_page = PG_NONE;                           // пиксели частиц пуль и вспышек (bfx.c)

// Стрелка (Map::init): по строке — начало каймы и начало жёлтого (строка симметрична, 9 — нет);
// 15 — чёрный, 16 — жёлтый (Palette::blockOffset(1))
static const uint8_t arrow_rows[9][2] = {
	{ 2, 9 }, { 2, 3 }, { 2, 3 }, { 0, 3 }, { 0, 1 }, { 1, 2 }, { 2, 3 }, { 3, 4 }, { 4, 9 },
};

static void sets_free(void)
{
	for (uint8_t i = 0; i < gen_ns; i++)
		if (gen_page[i] != PG_NONE) pg_free(gen_page[i], gen_np[i]);
	gen_ns = 0;
}

void bat_unload(void) __banked
{
	sets_free();
	if (unit_page != PG_NONE) { pg_free(unit_page, unit_np); unit_page = PG_NONE; }
	if (cur_page != PG_NONE) { pg_free(cur_page, cur_np); cur_page = PG_NONE; }
	if (hand_page != PG_NONE) { pg_free(hand_page, 1); hand_page = PG_NONE; }
	if (fx_page != PG_NONE) { pg_free(fx_page, 1); fx_page = PG_NONE; }
	mapgen_free();
}

uint8_t bat_load(bload_t *q) __banked
{
	far_t t = FAR(q->page + 1, 0);
	sets_free();
	q->nunits = 0;
	q->cw = 0;
	// Параметры миссии: размер поля, этажи, скрипт и террейн — из развёртывания, если оно задано
	// геоскейпом; корабль отряда и НЛО — от цели, иначе первые попавшиеся (отладка)
	uint8_t mods = 4, levels = 4;
	uint16_t terrain = q->terrain;
	if (q->deploy != 0xFFFF) {
		uint16_t d = mapgen_deploy(q->deploy, &mods, &levels);
		if (d != 0xFFFF) terrain = d;
	}
	mapgen_set_extra(q->craft != 0xFFFF ? q->craft : mapgen_find_kind(1),
			 q->ufo != 0xFFFF ? q->ufo : mapgen_find_kind(2));
	if (!mapgen_run(terrain, mods, levels)) return 0;
	tiles_req_t r;
	mapgen_sets(r.set, r.tset, &r.ns);
	mapgen_used(r.used);                 // какие части реально попали на карту
	r.page = q->page;
	uint8_t nd = 0;
	q->nparts = (uint8_t)tiles_load(&r, gen_page, gen_np, &gen_ns, &nd);
	if (!gen_ns) return 0;
	q->ndoors = nd;
	q->sx = (uint8_t)mapgen_sx(); q->sy = (uint8_t)mapgen_sy(); q->sz = mapgen_sz();
	q->cells = mapgen_cells();
	// Лист брони X-COM (TDXCOM_0 у TFTD, XCOM_0 у UFO): 232 кадра — стойка, колено и ходьба
	if (unit_page != PG_NONE) { pg_free(unit_page, unit_np); unit_page = PG_NONE; }
	if (sheet_load(res_game() == 2 ? RES_UNIT_TDXCOM_0 : RES_UNIT_XCOM_0, &unit_page, &unit_np,
		       232, 0, t + TL_SHEET)) {
		sheet_more(unit_page, 232, 32, t + TL_HARMS);   // руки с оружием (кадры 232..263)
		// Отряд: экипаж корабля миссии со своими показателями, в отладочном бою — шесть фигур
		crew_req_t c;
		cunit_t cu[MAX_UNITS];
		unit_t u;
		mapgen_craft(&c.cx, &c.cy, &c.cw, &c.cl, &c.part_min);
		q->cx = c.cx; q->cy = c.cy; q->cw = c.cw; q->cl = c.cl; q->cbase = c.part_min;
		c.cells = q->cells; c.sx = q->sx; c.sy = q->sy;
		c.craft = q->crew; c.debug_n = 6;
		uint8_t n = crew_deploy(&c, cu, MAX_UNITS);
		memset(&u, 0, sizeof u);
		u.size = 1;
		u.type = 0xFFFF; u.node = 0xFFFF;   // боец X-COM (FACTION_PLAYER), лист по частям
		u.seen = 1;
		for (uint8_t i = 0; i < n; i++) {
			u.x = cu[i].x; u.y = cu[i].y; u.z = cu[i].z;
			u.dir = 0; u.alive = 1; u.kneel = 0;
			u.tu = u.tu_max = cu[i].tu;
			u.hp = u.hp_max = cu[i].hp;
			u.en = u.en_max = cu[i].en; u.mor = cu[i].mor;
			u.en_rec = cu[i].tu / 3;     // бойцу — треть начального времени (у нас — текущего)
			u.sol = cu[i].sol;
			far_write(t + TL_UNITS + (uint16_t)i * sizeof u, &u, sizeof u);
		}
		q->nunits = n;
	}
	// Оружие в руках: страница под пиксели кадров HANDOB (заполняет bi_crew), слотов пока нет
	if (hand_page == PG_NONE) hand_page = pg_alloc(1, 1);
	far_write(t + TL_HANDPG, &hand_page, 1);
	if (fx_page == PG_NONE) fx_page = pg_alloc(1, 1);
	far_write(FAR(q->page + 2, TV_FXPG), &fx_page, 1);
	far_fill(t + TL_UHS, 0, MAX_UNITS * 2);
	far_fill(t + TL_ANUM, 0, 1);                 // пришельцев ещё нет (deploy.c)
	// Рамка курсора клетки из CURSOR.PCK: задние и передние половины красной и жёлтой
	if (cur_page == PG_NONE) {
		uint8_t want[9] = { 0, 1, 3, 4, 6, 7, 8, 9, 10 };   // рамка и прицел (Map.cpp:684-696)
		sheet_load(RES_CURSOR_PCK, &cur_page, &cur_np, 9, want, t + TL_CURSOR);
	}
	// Стрелка: 10 точек в строке (DMA адресует словами), запись для блита рядом
	uint8_t row[10];
	for (uint8_t y = 0; y < 9; y++) {
		uint8_t b = arrow_rows[y][0], f = arrow_rows[y][1];
		for (uint8_t x = 0; x < 10; x++)
			row[x] = x >= f && x + f <= 8 ? 16 : x >= b && x + b <= 8 ? 15 : 0;
		far_write(t + TL_ARROW + y * 10, row, 10);
	}
	uint8_t e[TE_SIZE] = { 0, (uint8_t)TL_ARROW, TL_ARROW >> 8, 4, 9, 0, 0, 10 };
	e[0] = q->page + 1;
	far_write(t + TL_ARROWE, e, TE_SIZE);
	return 1;
}
