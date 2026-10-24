// Экран наземного боя: события, панель и логика бойцов (банк 31). Рисует вид — bview.c (банк 28):
// экран говорит ему, что изменилось, а юнитов передаёт записями runit_t (bview.h).
//
// Управление: левая кнопка — идти в клетку (по своему бойцу — выбрать его), правая —
// развернуться туда и открыть дверь (как поворот в оригинале: UnitTurnBState открывает
// дверь, к которой повернулся боец); N — следующий боец, Shift+N — предыдущий (в оригинале
// TAB и SHIFT); стрелки — камера, Q/A — этаж, G — пересобрать карту, ESC — выход.
#include <stdint.h>
#include <string.h>
#include "memmap.h"
#include "res.h"
#include "res_ids.h"
#include "ui.h"
#include "input.h"
#include "far.h"
#include "pages.h"
#include "scrdef.h"
#include "dbg.h"
#include "pathfind.h"
#include "globe.h"
#include "music.h"
#include "units.h"
#include "doors.h"
#include "loader.h"
#include "bview.h"

#define VIEW_H     144               // окно карты; ниже — панель ICONS (56 строк)

static uint16_t gen_terrain;             // какой террейн пробует генератор (клавиша G)
// Параметры миссии, которые приносит геоскейп (bat_mission): развёртывание, карта НЛО,
// карта корабля отряда (всё — номера записей террейнов) и сам корабль в пуле кампании —
// от него берётся экипаж. #FFFF — «не задано»: бой отладочный, всё по умолчанию.
static uint16_t mis_deploy = 0xFFFF, mis_ufo = 0xFFFF, mis_craft = 0xFFFF, mis_crew = 0xFFFF;
static uint8_t m_sx, m_sy, m_sz;
static far_t cells;                  // начало клеток карты
static uint8_t loaded;
static uint8_t gen_mode;             // 1 — показывается сгенерированная карта

// Юниты миссии (unit_t, units.h) — в памяти банка (build.ps1 ram); вид получает их записями
// runit_t через рабочую страницу (ru_put, bv_sync)
static unit_t __at(0xB000) units[MAX_BUNITS];
static uint8_t nunits;
static uint8_t sel;                      // выбранный боец (панель показывает его состояние)
#define KNEEL_TU_DOWN  4                 // цена приседания и вставания (BattlescapeGame::kneel)
#define KNEEL_TU_UP    8
extern volatile uint16_t frames;         // crt0.s
// Рабочая страница боя: массивы поиска пути, свойства частей и таблицы графики миссии
static uint8_t pf_page = PG_NONE;

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

// Запись юнита для вида — в рабочую страницу; bv_sync переносит их виду
static void ru_put(uint8_t i)
{
	const unit_t *u = &units[i];
	runit_t r;
	r.x = u->x; r.y = u->y; r.z = u->z; r.dir = u->dir;
	r.flags = (u->alive ? RU_ALIVE : 0) | (u->kneel ? RU_KNEEL : 0);
	r.sheet = 0;
	r.pad[0] = r.pad[1] = 0;
	far_write(FAR(pf_page + 1, TL_RUNITS) + (uint16_t)i * sizeof r, &r, sizeof r);
}

// Юнит изменился: запись виду и перерисовка его клетки
static void unit_changed(uint8_t i)
{
	ru_put(i);
	bv_sync(nunits);
	bv_cell(units[i].x, units[i].y, units[i].z);
}

// Номер бойца в клетке + 1 (0 — никого): по нему выбирают щелчком
static uint8_t unit_no(uint8_t z, uint8_t y, uint8_t x)
{
	for (uint8_t i = 0; i < nunits; i++) {
		const unit_t *u = &units[i];
		uint8_t uz = u->z, uy = u->y, ux = u->x;
		if (u->alive && uz == z && uy == y && ux == x) return (uint8_t)(i + 1);
	}
	return 0;
}

// Навести камеру на выбранного бойца (кнопка «центрировать», как в оригинале)
static void center_on_unit(void)
{
	if (!nunits) return;
	const unit_t *u = &units[sel < nunits ? sel : 0];
	bv_center(u->x, u->y);
}

// ---------------------------------------------------------------- ход бойца
// Поиск пути — волна по клеткам (pathfind.c, банк 30); рабочие массивы — в своей странице пула.
#define STEP_TU        4                 // единиц времени за шаг (в оригинале зависит от пола)
#define PATH_MAX       24

static uint8_t path[PATH_MAX], path_n, path_i;   // направления шагов
static uint8_t move_wait;                        // кадров до следующего шага
static uint8_t turn_dir = 0xFF;                  // куда разворачивается боец (правая кнопка)
static uint8_t ndoors;                           // дверей среди частей карты (отладка)
static uint8_t wl_x, wl_y;                       // куда идёт шагающий боец

// Смещения восьми направлений (0 — север, дальше по часовой, как в оригинале)
// Своя копия: таблицы из банка поиска пути видны только при подключённом том банке
static const int8_t dir_dx[8] = { 0, 1, 1, 1, 0, -1, -1, -1 };
static const int8_t dir_dy[8] = { -1, -1, 0, 1, 1, 1, 0, -1 };

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

// Створки НЛО едут (doors_anim): следующий кадр в таблицу вида и перерисовка их клеток.
// 1 — створки есть (их клетки уже перерисованы)
static uint8_t an_on, an_tm;             // едут; кадр (frames), когда сменили их кадр
static uint8_t door_anim_step(void)
{
	uint8_t parts[5], cl[15];
	an_on = doors_anim(&bmap, parts, cl);
	an_tm = (uint8_t)frames;
	for (uint8_t i = 0; parts[i]; i++) bv_tile(parts[i]);
	for (uint8_t i = 0; cl[i * 3] != 0xFF; i++) bv_cell(cl[i * 3], cl[i * 3 + 1], cl[i * 3 + 2]);
	return cl[0] != 0xFF;
}

// Открыть дверь в направлении dir и перерисовать изменившиеся клетки.
// 0 — открыли, 1 — не хватило времени, 2 — двери нет, 3 — створка НЛО ещё едет.
static uint8_t door_try(unit_t *u, uint8_t dir, uint8_t rclick)
{
	uint8_t list[8], spent = 0;
	memset(list, 0xFF, sizeof list);     // #FF — конец списка: координаты меньше 64
	uint8_t r = door_open(&bmap, u->x, u->y, u->z, dir, rclick, u->tu, &spent, list, 4);
	if (r) return r;
	u->tu -= spent;
	if (door_anim_step()) return 0;      // створка НЛО поехала: кадр 1 (Tile::openDoor)
	for (uint8_t i = 0; i < 4 && list[i * 2] != 0xFF; i++)
		bv_cell(list[i * 2], list[i * 2 + 1], u->z);
	return 0;
}

// Присесть или встать (BattlescapeGame::kneel): 4 единицы времени вниз, 8 вверх.
// Сидящего бойца хуже видно и точнее стреляет — это появится вместе с видимостью и стрельбой.
static void do_kneel(void)
{
	if (!nunits || bv_walking() != 0xFF) return;
	uint8_t i = sel < nunits ? sel : 0;
	unit_t *u = &units[i];
	uint8_t cost = u->kneel ? KNEEL_TU_UP : KNEEL_TU_DOWN;
	if (u->tu < cost) return;
	u->tu -= cost;
	u->kneel = !u->kneel;
	path_n = 0;
	unit_changed(i);
	ui_dirty(0);
}

// Повернуться на одну восьмую в сторону d (в оригинале поворот при ходьбе бесплатен и
// идёт по одному шагу за такт — отсюда и разворот на месте перед шагом)
static void turn_to(uint8_t i, uint8_t d)
{
	unit_t *u = &units[i];
	uint8_t diff = (uint8_t)((d - u->dir) & 7);
	u->dir = (uint8_t)((u->dir + (diff && diff <= 4 ? 1 : 7)) & 7);
	unit_changed(i);
}

// Один шаг по пути: поворот, открывание двери, перенос в клетку и расход времени
static void step_unit(void)
{
	if (!nunits || path_i >= path_n) { path_n = 0; return; }
	uint8_t i = sel < nunits ? sel : 0;
	unit_t *u = &units[i];
	uint8_t d = path[path_i];
	if (u->kneel) {                                  // идти на коленях нельзя — сперва встать
		if (u->tu < KNEEL_TU_UP) { path_n = 0; return; }
		u->tu -= KNEEL_TU_UP;
		u->kneel = 0;
		unit_changed(i);
		return;
	}
	if (u->dir != d) { turn_to(i, d); return; }      // сперва развернуться лицом к шагу
	uint8_t r = door_try(u, d, 0);                   // дверь открывается вместо шага, створку
	if (r == 1) path_n = 0;                          //   НЛО ждут (UnitWalkBState); не хватило
	if (r != 2) return;                              //   времени — путь прерван
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
		ru_put(i);
		bv_sync(nunits);
		bv_set_level(nz, 0);
		ui_dirty(0);
		return;
	}
	// Шаг — фазами ходьбы (bv_walk): боец числится в прежней клетке до середины пути
	move_wait = 0;                       // между клетками пути паузы нет
	wl_x = (uint8_t)nx; wl_y = (uint8_t)ny;
	bv_walk(i, ox, oy, (uint8_t)nx, (uint8_t)ny, oz, d);
}

// ---------------------------------------------------------------- сгенерированная карта
// Генератор (mapgen.c, банк 29) собирает поле из блоков террейна, загрузка миссии (bload.c, банк
// 30) — наборы частей, листы бойца и курсора, отряд и стрелку; всё складывается в рабочую
// страницу, вид (bview.c) забирает к себе свои таблицы.
static uint8_t load_gen(uint16_t terrain)
{
	bload_t q;
	bvload_t v;
	loaded = 0;
	q.terrain = terrain; q.deploy = mis_deploy; q.ufo = mis_ufo; q.craft = mis_craft; q.crew = mis_crew;
	q.page = pf_page;
	if (!bat_load(&q)) return 0;
	nunits = q.nunits;
	far_read(FAR(pf_page + 1, TL_UNITS), units, (uint16_t)nunits * sizeof(unit_t));
	ndoors = q.ndoors;
	m_sx = q.sx; m_sy = q.sy; m_sz = q.sz;
	cells = q.cells;
	bmap.cells = cells; bmap.sx = m_sx; bmap.sy = m_sy; bmap.sz = m_sz; bmap.page = pf_page;
	v.cells = cells; v.sx = m_sx; v.sy = m_sy; v.sz = m_sz; v.page = pf_page;
	bv_load(&v);
	for (uint8_t i = 0; i < nunits; i++) ru_put(i);
	bv_sync(nunits);
	an_on = 0;
	gen_mode = 1;
	sel = 0;
	if (nunits) center_on_unit();        // камера на первого бойца отряда
	loaded = 1;
	return 1;
}

// Панель бойца рисует банк 30 (units.c)
static void draw_panel(void)
{
	bpanel_t p;
	const unit_t *u = &units[sel < nunits ? sel : 0];
	p.n = nunits; p.level = bv_level();
	p.tu = u->tu; p.tu_max = u->tu_max; p.en = u->en;
	p.hp = u->hp; p.hp_max = u->hp_max; p.mor = u->mor;
	bat_panel(&p);
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
		bv_open();                           // список вида, холсты, сплит, курсор боя
		if (pf_page == PG_NONE) pf_page = pg_alloc(2, 1);   // рабочая память боя: поиск пути и таблицы
		if (pf_page == PG_NONE) { dbg_puts("battle: no pages\n"); break; }   // иначе — в страницу #FF
		// Карта миссии собирается генератором (16 §3). Пока миссии нет, террейн берётся
		// по очереди — клавиша G дальше пересобирает карту следующего террейна.
		load_gen(gen_terrain);
		break;
	case EVT_TICK: {
		if (!gen_mode || !loaded) break;
		if (turn_dir != 0xFF && nunits) {    // правая кнопка: разворот по одной восьмой,
			uint8_t i = sel < nunits ? sel : 0;   // в конце — попытка открыть дверь
			if (move_wait) move_wait--;
			else if (units[i].dir != turn_dir) { turn_to(i, turn_dir); move_wait = 2; }
			else { door_try(&units[i], turn_dir, 1); turn_dir = 0xFF; ui_dirty(0); }
		} else if (bv_walking() != 0xFF) {   // идёт: фазы считает вид
			uint8_t i = bv_walking();
			uint8_t r = bv_walk_tick();
			if (r == 1) {                    // середина пути: числится в новой клетке
				unit_t *u = &units[i];
				u->x = wl_x; u->y = wl_y;
				ru_put(i);
			} else if (r == 2 && !path_n) ui_dirty(0);   // путь кончился — обновить панель
		} else if (path_n) {                 // идём по пути: поворот раз в несколько кадров
			if (move_wait) move_wait--;
			else { move_wait = 3; step_unit(); if (!path_n) ui_dirty(0); }   // в конце — обновить панель
		}
		if (an_on && (uint8_t)((uint8_t)frames - an_tm) >= 5) door_anim_step();   // 100 мс (Tile::animate)
		bv_tick(sel);                        // прокрутка, курсор клетки, стрелка над выбранным
		break;
	}
	case EVT_DRAW:
		bv_draw();
		draw_panel();
		break;
	case EVT_CLOSE:
		// Бой отдаёт всю свою память: карту, наборы тайлов, лист юнитов, список вида и
		// рабочую память поиска пути. Иначе геоскейпу не из чего заводить слоты кэша, и он
		// перечитывает каждый фон с карты — экран залипает (14 §todo).
		bv_close();
		bat_unload();                    // наборы частей, листы бойца и курсора, генератор (bload.c)
		gen_mode = 0;
		loaded = 0;
		nunits = 0;
		if (pf_page != PG_NONE) { pg_free(pf_page, 2); pf_page = PG_NONE; }
		path_n = 0;
		turn_dir = 0xFF;
		an_on = 0;
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
			if (!gen_mode || !nunits || bv_walking() != 0xFF) break;   // идёт — ждать
			uint8_t tx, ty;
			if (!bv_cursor(&tx, &ty)) break;
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
		case 3: { uint8_t l = bv_level(); if (l + 1 < m_sz) { bv_set_level(l + 1, 1); ui_dirty(0); } break; }
		case 4: { uint8_t l = bv_level(); if (l) { bv_set_level(l - 1, 1); ui_dirty(0); } break; }
		case 5:                              // инвентарь: окно рисуется в строки 0–199 основного
			bv_suspend();                    //   холста — без сплита и с ним на экране
			UI_GO(A_PUSH, SCR_INVENTORY);
			break;
		case 6: center_on_unit(); ui_dirty(0); break;
		case 7: sel_next(); break;           // следующий боец
		case 10: do_kneel(); break;          // присесть и встать
		case 9:                              // «1/2»: все этажи или только текущий
			bv_all_levels();
			ui_dirty(0);
			break;
		case 8: {                            // конец хода: время и энергия бойцов восстанавливаются,
			uint8_t list[24];                //   двери НЛО закрываются (BattlescapeGame::endTurn)
			uint8_t n = doors_close_ufo(&bmap, list, 8);
			an_on = 0;
			for (uint8_t i = 0; i < n; i++) bv_cell(list[i * 3], list[i * 3 + 1], list[i * 3 + 2]);
			for (uint8_t i = 0; i < nunits; i++) { units[i].tu = units[i].tu_max; units[i].en = units[i].tu_max; }
			ui_dirty(0);
			break;
		}
		}
		break;
	case EVT_KEY: {
		// Стрелки здесь не обрабатываются: камера едет плавно, пока клавиша зажата (bv_tick)
		switch (arg) {
		case 'd': case 'D': {               // отладка: поставить бойца к первой двери
			uint8_t dx, dy, ds, l = bv_level();
			if (!gen_mode || !nunits || bv_walking() != 0xFF || !door_find(&bmap, l, &dx, &dy, &ds)) break;
			uint8_t i = sel < nunits ? sel : 0;
			unit_t *u = &units[i];
			u->x = dx; u->y = dy; u->z = l;
			u->dir = ds == 1 ? 6 : 0;         // западная стена — лицом на запад, северная — на север
			path_n = 0;                       // вид пересоберёт center_on_unit
			ru_put(i);
			bv_sync(nunits);
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
			dbg_puts(" dl "); dbg_dec(bv_dl_n()); dbg_puts(" units "); dbg_dec(nunits); dbg_puts(" doors "); dbg_dec(ndoors);
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
