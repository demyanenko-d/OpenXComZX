// Экран наземного боя: события, панель и логика бойцов (банк 31). Рисует вид — bview.c (банк 28):
// экран говорит ему, что изменилось, а юнитов передаёт записями runit_t (bview.h).
//
// Управление: левая кнопка — идти в клетку (по своему бойцу — выбрать его), правая —
// развернуться туда и открыть дверь (как поворот в оригинале: UnitTurnBState открывает
// дверь, к которой повернулся боец); N — следующий боец, Shift+N — предыдущий (в оригинале
// TAB и SHIFT); стрелки — камера, Q/A — этаж, G — пересобрать карту, ESC — выход.
#include <stdint.h>
#include <string.h>
#include <stddef.h>
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
#include "deploy.h"
#include "vision.h"
#include "rules.h"
#include "text.h"
#include "gfx.h"
#include "bitems.h"
#include "bhud.h"
#include "bspr.h"
#include "bmenu.h"
#include "state.h"
#include "bstats.h"
#include "bfire.h"
#include "bfx.h"
#include "bact.h"
#include "bai.h"

#define VIEW_H     144               // окно карты; ниже — панель ICONS (56 строк)

static uint16_t gen_terrain;             // какой террейн пробует генератор (клавиша G)
// Параметры миссии, которые приносит геоскейп (bat_mission): развёртывание, карта НЛО,
// карта корабля отряда (всё — номера записей террейнов) и сам корабль в пуле кампании —
// от него берётся экипаж. #FFFF — «не задано»: бой отладочный, всё по умолчанию.
static uint16_t mis_deploy = 0xFFFF, mis_ufo = 0xFFFF, mis_craft = 0xFFFF, mis_crew = 0xFFFF;
// Пришельцы: тип НЛО (ufos, #FF — миссия без НЛО) — по нему развёртывание с его составом, раса
static uint8_t mis_utype = 0xFF, mis_race = 0xFF;
static uint8_t m_sx, m_sy, m_sz;
static uint8_t m_cx, m_cy, m_cw, m_cl, m_cbase;   // корабль отряда: прямоугольник, его первая часть
static far_t cells;                  // начало клеток карты
static uint8_t loaded;
static uint8_t gen_mode;             // 1 — показывается сгенерированная карта

// Юниты миссии (unit_t, units.h) — в памяти банка (build.ps1 ram); вид получает их записями
// runit_t через рабочую страницу (ru_put, bv_sync)
static unit_t __at(0xB880) units[MAX_BUNITS];   // до конца страницы
static uint8_t nunits;
static uint8_t nsold;                    // бойцы X-COM — первые nsold юнитов, дальше пришельцы
static uint8_t bt_turn = 1, bt_side;     // номер хода и чей ход (FACTION_*)
static uint8_t bt_reserve, bt_res_kneel; // резерв времени (RES_*) и «на колено» — BattlescapeGame
static uint8_t bt_action, bt_action_tu, bt_hand;   // выбранное действие, его цена и рука
static uint8_t bt_acc, bt_aim;                     // меткость действия; ждём клетку-цель (прицел)
static void next_turn_close(void);       // окно хода (ниже)
static void unit_armor(unit_t *u, uint16_t arm);   // броня бойца (ниже)
static uint8_t reserve_ok(uint8_t i, uint8_t cost);   // резерв времени (ниже)
static void do_kneel(void);
static uint16_t ufo_deploy(uint8_t utype);   // развёртывание по типу НЛО (ниже)
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
	HOT(48, 144, 32, 16, A_CUSTOM, 1, 0),            // боец на этаж выше (btnUnitUpClick: гравилифт, полёт)
	HOT(48, 160, 32, 16, A_CUSTOM, 2, 0),            // на этаж ниже
	HOT(80, 144, 32, 16, A_CUSTOM, 3, 'q'),          // этаж вверх (и Q)
	HOT(80, 160, 32, 16, A_CUSTOM, 4, 'a'),          // этаж вниз (и A)
	HOT(112, 160, 32, 16, A_CUSTOM, 10, 'k'),        // присесть и встать
	HOT(144, 144, 32, 16, A_CUSTOM, 5, 'i'),         // инвентарь
	HOT(144, 160, 32, 16, A_CUSTOM, 6, 'c'),         // центрировать на бойце
	HOT(176, 144, 32, 16, A_CUSTOM, 7, 0),           // следующий боец
	HOT(176, 160, 32, 16, A_CUSTOM, 20, 0),          // следующий, этого больше не выбирать (btnNextStopClick)
	CUSR(49, 177, 10, 23, NOSTR, A_CUSTOM, 21),      // потратить всё время — правой кнопкой (btnZeroTUsClick)
	HOT(208, 144, 32, 16, A_CUSTOM, 9, 0),           // показывать все этажи или только текущий
	HOT(208, 160, 32, 16, A_CUSTOM, 22, 0),          // меню паузы (btnHelpClick)
	HOT(240, 144, 32, 16, A_CUSTOM, 8, 0),           // конец хода
	HOT(240, 160, 32, 16, A_CUSTOM, 18, ESC),        // прервать миссию (btnAbortClick)
	// резерв времени (BattlescapeState::btnReserveClick): не резервировать, навскидку, очередь, прицельный
	HOT(60, 177, 17, 11, A_CUSTOM, 11, 0),
	HOT(78, 177, 17, 11, A_CUSTOM, 12, 0),
	HOT(78, 189, 17, 11, A_CUSTOM, 13, 0),
	HOT(60, 189, 17, 11, A_CUSTOM, 14, 0),
	HOT(96, 177, 10, 23, A_CUSTOM, 15, 0),           // резерв на колено (btnReserveKneelClick)
	HOT(8, 148, 32, 48, A_CUSTOM, 16, 0),            // предмет в левой руке (btnLeftHandItemClick)
	HOT(280, 148, 32, 48, A_CUSTOM, 17, 0),          // в правой
	HOT(107, 177, 164, 23, A_CUSTOM, 19, 0),         // показатели — окно характеристик (btnStatsClick)
};

// UnitInfoState: экран рисует bstats.c, «<<» и «>>» — соседний боец, клик ниже — закрыть
static const wdef_t w_uinfo[] = {
	CUS(0, 20, 320, 180, DYN(0), A_CUSTOM, 1),
	BTN(2, 2, 14, 18, UI_EL_BUTTON, DYN(1), A_CUSTOM, 2, 0),
	BTN(304, 2, 14, 18, UI_EL_BUTTON, DYN(2), A_CUSTOM, 3, 0),
};

// ActionMenuState: окно поверх боя без своего фона — пункты рисует bmenu.c, клики и наведение
// разбирает экран (правая кнопка — закрыть)
static const wdef_t w_amenu[] = {
	CUSR(0, 0, 320, 200, DYN(0), A_CUSTOM, 1),
};

// NextTurnState (REF/OpenXcom/src/Battlescape/NextTurnState.cpp): окно 320x200 на фоне TAC00.SCR,
// крупно по центру — ход, сторона и «нажмите кнопку»; любая кнопка или клавиша закрывает.
// Заголовок STR_OPENXCOM — добавка OpenXcom, в оригинале его нет.
static const wdef_t w_next[] = {
	WIN(0, 0, 320, 200, UI_EL_MESSAGEWINDOWS),
	TXT(0, 92, 320, 17, UI_EL_MESSAGEWINDOWS, DYN(0), TC | BIG),
	TXT(0, 108, 320, 17, UI_EL_MESSAGEWINDOWS, DYN(1), TC | BIG),
	TXT(0, 132, 320, 17, UI_EL_MESSAGEWINDOWS, STR_PRESS_BUTTON_TO_CONTINUE, TC | BIG),
	HOT(0, 0, 320, 200, A_CUSTOM, 1, 0),
};

// AbortMissionState: окно 320x144 на фоне TAC00.SCR — кто в корабле, кто снаружи, вопрос, OK и
// CANCEL (выхода END_POINT в наших картах нет — строки «в корабле» и «снаружи» стоят в 26 и 54)
static const wdef_t w_abort[] = {
	WIN(0, 0, 320, 144, UI_EL_MESSAGEWINDOWBORDER),
	TXT(16, 26, 304, 17, UI_EL_MESSAGEWINDOWS, DYN(0), BIG),
	TXT(16, 54, 304, 17, UI_EL_MESSAGEWINDOWS, DYN(1), BIG),
	TXT(0, 80, 320, 17, UI_EL_MESSAGEWINDOWS, STR_ABORT_MISSION_QUESTION, TC | BIG),
	BTN(16, 110, 120, 16, UI_EL_MESSAGEWINDOWBUTTONS, STR_OK, A_CUSTOM, 1, ENT),
	BTN(184, 110, 120, 16, UI_EL_MESSAGEWINDOWBUTTONS, STR_CANCEL_UC, A_POP, 0, ESC),
};

// PauseState(OPT_BATTLESCAPE): окно 216x160 в (52, 20), всё цветом mainMenu.battlescapeTheme с
// контрастом, фон TAC00.SCR (applyBattlescapeTheme). Кнопки «сохранить» нет: сохранение боя ещё
// не сделано, а запись кампании посреди боя вернула бы на геоскейп до высадки (14_todo)
static const wdef_t w_bpause[] = {
	WINP(52, 20, 216, 160, UI_EL_BATTLESCAPETHEME, POPB),
	BTN(70, 52, 180, 18, UI_EL_BATTLESCAPETHEME, STR_LOAD_GAME, A_PUSH, SCR_LOAD, 0),
	BTN(70, 96, 180, 18, UI_EL_BATTLESCAPETHEME, STR_ABANDON_GAME, A_PUSH, SCR_ABANDON, 0),
	BTN(70, 122, 180, 18, UI_EL_BATTLESCAPETHEME, STR_GAME_OPTIONS, A_PUSH, SCR_OPTIONS, 0),
	BTN(70, 150, 180, 18, UI_EL_BATTLESCAPETHEME, STR_CANCEL_UC, A_POP, 0, ESC),
	TXT(57, 32, 206, 17, UI_EL_BATTLESCAPETHEME, STR_OPTIONS_UC, BIG | TC),
};

static const scr_t tab[] = {
	SCR(SCR_BATTLE, UI_SCR_SAVEMENUS, RES_PAL_BATTLESCAPE, 0, SF_RAWPAL, w_battle),
	SCR(SCR_NEXT_TURN, UI_SCR_BATTLESCAPE, RES_PAL_BATTLESCAPE, RES_TAC00_SCR, SF_RAWPAL, w_next),
	SCR(SCR_ACTION_MENU, UI_SCR_BATTLESCAPE, RES_PAL_BATTLESCAPE, 0, SF_POPUP | SF_RAWPAL, w_amenu),
	SCR(SCR_ABORT, UI_SCR_BATTLESCAPE, RES_PAL_BATTLESCAPE, RES_TAC00_SCR, SF_POPUP | SF_RAWPAL, w_abort),
	SCR(SCR_UNIT_INFO, UI_SCR_STATS, RES_PAL_BATTLESCAPE, 0, SF_RAWPAL, w_uinfo),
	SCR(SCR_BATTLE_PAUSE, UI_SCR_MAINMENU, RES_PAL_BATTLESCAPE, RES_TAC00_SCR, SF_POPUP | SF_RAWPAL, w_bpause),
};

// Запись юнита для вида — в рабочую страницу; bv_sync переносит их виду
static void ru_put(uint8_t i)
{
	const unit_t *u = &units[i];
	runit_t r;
	r.x = u->x; r.y = u->y; r.z = u->z; r.dir = u->dir;
	// без своего листа рисуется только боец X-COM (лист по частям)
	r.flags = (u->alive && u->seen && (u->sheet || u->faction == FACTION_PLAYER) ? RU_ALIVE : 0)
		| (u->kneel ? RU_KNEEL : 0) | (u->size > 1 ? RU_BIG : 0);
	r.sheet = u->sheet;
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

// Видимость (vision.c): кого из пришельцев отряд заметил впервые — показать (запись виду и
// перерисовка их клеток; крупный — все четыре)
static uint8_t nmarks, marks_u[10];     // значки замеченных: сколько и чьи
static void marks_on(void)
{
	marks_show(nmarks, ui_color_in(UI_SCR_BATTLESCAPE, UI_EL_VISIBLEUNITS, 0));
}

static void see_update(void)
{
	uint8_t list[16];
	vu_t v[MAX_BUNITS];                  // копия на стеке: банку 32 память банка 31 не видна
	for (uint8_t i = 0; i < nunits; i++) {
		const unit_t *u = &units[i];
		v[i].x = u->x; v[i].y = u->y; v[i].z = u->z; v[i].dir = u->dir;
		v[i].flags = (u->alive ? VU_ALIVE : 0) | (u->size > 1 ? VU_BIG : 0) | (u->seen ? VU_SEEN : 0);
	}
	uint8_t n = vis_seen(v, nunits, nsold, list, sizeof list);
	// значки над панелью — кого видит выбранный боец (updateSoldierInfo: getVisibleUnits)
	for (uint8_t i = 0; i < nunits; i++) if (units[i].seen) v[i].flags |= VU_SEEN;
	nmarks = sel < nsold ? vis_list(v, nunits, nsold, sel, marks_u, sizeof marks_u) : 0;
	marks_on();
	if (!n) return;
	for (uint8_t i = 0; i < n; i++) units[list[i]].seen = 1;
	for (uint8_t i = 0; i < n; i++) ru_put(list[i]);
	bv_sync(nunits);
	for (uint8_t i = 0; i < n; i++) {
		const unit_t *a = &units[list[i]];
		for (uint8_t q = 0; q < a->size * a->size; q++)
			bv_cell((uint8_t)(a->x + (q & 1)), (uint8_t)(a->y + (q >> 1)), a->z);
	}
}

// Значок на экране: клетка под курсором — значок i (y = 128 - 13 i, x 300..314, 12 строк)
static uint8_t mark_at(void)
{
	if (cursor_x < 300 || cursor_x >= 315 || cursor_y > 139) return 0xFF;
	uint8_t i = (uint8_t)((139 - cursor_y) / 13);
	if (i >= nmarks || cursor_y < 128 - 13 * (int16_t)i) return 0xFF;
	return i;
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
// SavedBattleGame::selectNextPlayerUnit (checkReselect): живой, не отмеченный «больше не выбирать»
static void sel_step(uint8_t back)
{
	uint8_t s = sel;
	for (uint8_t k = 0; k < nsold; k++) {
		s = (uint8_t)(back ? (s + nsold - 1) % nsold : (s + 1) % nsold);
		if (units[s].alive && !units[s].done) break;
	}
	sel = s;
}

static void sel_next(void)
{
	if (!nsold) return;
	sel_step(0);
	path_n = 0;
	see_update();
	center_on_unit();
	ui_dirty(0);
}

static void sel_prev(void)
{
	if (!nsold) return;
	sel_step(1);
	path_n = 0;
	see_update();
	center_on_unit();
	ui_dirty(0);
}

// Карта для банка клеток (doors.c): двери и цена прохода считаются там
static dmap_t bmap;

// Створки НЛО едут (doors_anim): следующий кадр в таблицу вида и перерисовка их клеток.
// 1 — створки есть (их клетки уже перерисованы)
static uint8_t an_on, an_tm;             // едут; кадр (frames), когда сменили их кадр
static uint8_t bl_tm;                    // кадр прошлого шага мигания значков
static uint8_t door_anim_step(void)
{
	uint8_t parts[5], cl[15];
	an_on = doors_anim(&bmap, parts, cl);
	an_tm = (uint8_t)frames;
	for (uint8_t i = 0; parts[i]; i++) bv_tile(parts[i]);
	for (uint8_t i = 0; cl[i * 3] != 0xFF; i++) {
		bv_cell(cl[i * 3], cl[i * 3 + 1], cl[i * 3 + 2]);
		vis_cell(cells, cl[i * 3], cl[i * 3 + 1], cl[i * 3 + 2], pf_page);   // створка уже не держит обзор
	}
	if (cl[0] != 0xFF) see_update();
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
	for (uint8_t i = 0; i < 4 && list[i * 2] != 0xFF; i++) {
		bv_cell(list[i * 2], list[i * 2 + 1], u->z);
		vis_cell(cells, list[i * 2], list[i * 2 + 1], u->z, pf_page);
	}
	see_update();
	return 0;
}

// Присесть или встать (BattlescapeGame::kneel): 4 единицы времени вниз, 8 вверх.
// Сидящего бойца хуже видно и точнее стреляет — это появится вместе с видимостью и стрельбой.
// Боец на этаж выше или ниже (BattlescapeState::btnUnitUpClick -> Pathfinding::validateUpDown,
// BattlescapeGame::moveUpDown): гравилифт в клетке и над (под) ней, или летающий в пустоту
// (вверх — если над ним нет пола, вниз — если под ним); цена — 8 (getTUCost для вертикали),
// сидящий сперва встаёт
#define PF_GRAVLIFT 16
static uint8_t floor_flags(uint8_t x, uint8_t y, uint8_t z, uint8_t *part)
{
	uint8_t c;
	far_read(cells + ((uint32_t)((uint16_t)z * m_sy + y) * m_sx + x) * 4, &c, 1);
	*part = c;
	return c ? far_byte(FAR(pf_page, PF_FLAGS) + c) : 0;
}

static void unit_updown(uint8_t up)
{
	if (!nsold || bv_walking() != 0xFF || bt_side != FACTION_PLAYER) return;
	uint8_t i = sel < nsold ? sel : 0;
	unit_t *u = &units[i];
	if (up ? u->z + 1 >= m_sz : !u->z) return;
	uint8_t nz = up ? u->z + 1 : u->z - 1, ps, pd;
	uint8_t fs = floor_flags(u->x, u->y, u->z, &ps), fd = floor_flags(u->x, u->y, nz, &pd);
	uint8_t ok = ps && pd && (fs & PF_GRAVLIFT) && (fd & PF_GRAVLIFT);
	if (!ok && u->fly) ok = up ? (!pd || (fd & 1)) : (!ps || (fs & 1));
	if (!ok) return;
	if (u->kneel) do_kneel();
	if (u->kneel) return;
	if (!reserve_ok(i, 8)) return;
	if (u->tu < 8) { hud_warn(STR_NOT_ENOUGH_TIME_UNITS); return; }
	u->tu -= 8;
	u->z = nz;
	ru_put(i);
	bv_sync(nunits);
	bv_set_level(nz, 0);
	see_update();
	ui_dirty(0);
}

// Резерв времени для хода ценой cost (BattlescapeGame::checkReservedTU, bhud.c)
static const uint8_t res_ba[4] = { BA_NONE, BA_SNAPSHOT, BA_AUTOSHOT, BA_AIMEDSHOT };
static uint8_t reserve_ok(uint8_t i, uint8_t cost)
{
	const unit_t *u = &units[i];
	rsreq_t r;
	r.unit = i; r.page = pf_page; r.tu_max = u->tu_max; r.tu = u->tu; r.kneeled = u->kneel;
	r.reserve = res_ba[bt_reserve & 3]; r.res_kneel = bt_res_kneel; r.hand = u->hand;
	return hud_reserve(&r, cost);
}

static void do_kneel(void)
{
	if (!nunits || bv_walking() != 0xFF) return;
	uint8_t i = sel < nunits ? sel : 0;
	unit_t *u = &units[i];
	uint8_t cost = u->kneel ? KNEEL_TU_UP : KNEEL_TU_DOWN;
	// BattlescapeGame::kneel: присесть при резерве «на колено» можно всегда, иначе — резерв
	if (!(!u->kneel && bt_res_kneel) && !reserve_ok(i, cost)) return;
	if (u->tu < cost) { hud_warn(STR_NOT_ENOUGH_TIME_UNITS); return; }
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
	see_update();                        // повернулся — другой сектор обзора (UnitTurnBState)
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
	if (r == 1) { path_n = 0; hud_warn(STR_NOT_ENOUGH_TIME_UNITS); }   //   НЛО ждут; не хватило
	if (r != 2) return;                              //   времени — путь прерван
	int16_t nx = (int16_t)u->x + dir_dx[d], ny = (int16_t)u->y + dir_dy[d];
	if (nx < 0 || ny < 0 || nx >= m_sx || ny >= m_sy) { path_n = 0; return; }
	uint8_t cost = cell_cost(&bmap, (uint8_t)nx, (uint8_t)ny, u->z, d, STEP_TU);
	if (!reserve_ok(i, cost)) { path_n = 0; return; }   // UnitWalkBState: checkReservedTU
	if (u->tu < cost) { path_n = 0; hud_warn(STR_NOT_ENOUGH_TIME_UNITS); return; }
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
		see_update();
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
	aliens_free();                       // листы пришельцев прошлой карты
	if (!bat_load(&q)) return 0;
	nunits = nsold = q.nunits;
	far_read(FAR(pf_page + 1, TL_UNITS), units, (uint16_t)nunits * sizeof(unit_t));
	// Пришельцы: состав — развёртывание миссии (у сбитого или севшего НЛО — развёртывание его
	// типа, BattlescapeGenerator::run), места — узлы маршрутов карты
	{
		adreq_t ar;
		ar.deploy = mis_utype != 0xFF ? ufo_deploy(mis_utype) : mis_deploy;
		ar.race = mis_race != 0xFF ? mis_race : 0xFFFF;
		ar.ufo = mis_ufo != 0xFFFF || mis_deploy == 0xFFFF;
		ar.page = pf_page;
		uint8_t na = aliens_deploy(&ar, nsold, q.sx, q.sy);
		far_read(FAR(pf_page + 1, TL_AUNITS), units + nsold, (uint16_t)na * sizeof(unit_t));
		nunits = nsold + na;
	}
	vis_build(q.cells, q.sx, q.sy, q.sz, pf_page);   // маска обзора (vision.c)
	// Снаряжение отряда (bitems.c): раскладка из кампании — до записей вида (им нужно
	// оружие в руках), но после листов пришельцев и маски обзора: HANDOB берёт слот кэша SD
	{
		uint8_t sol[MAX_UNITS];
		for (uint8_t i = 0; i < nsold; i++) sol[i] = units[i].sol;
		bi_crew(pf_page, sol, nsold);
	}
	ndoors = q.ndoors;
	m_sx = q.sx; m_sy = q.sy; m_sz = q.sz;
	m_cx = q.cx; m_cy = q.cy; m_cw = q.cw; m_cl = q.cl; m_cbase = q.cbase;
	cells = q.cells;
	bmap.cells = cells; bmap.sx = m_sx; bmap.sy = m_sy; bmap.sz = m_sz; bmap.page = pf_page;
	v.cells = cells; v.sx = m_sx; v.sy = m_sy; v.sz = m_sz; v.page = pf_page;
	bv_load(&v);
	for (uint8_t i = 0; i < nunits; i++) ru_put(i);
	bv_sync(nunits);
	see_update();                        // кого отряд видит с места высадки
	an_on = 0;
	bt_turn = 1;
	bt_side = FACTION_PLAYER;
	gen_mode = 1;
	sel = 0;
	for (uint8_t i = 0; i < nsold; i++) {   // боевые показатели: солдат кампании или отладочные
		unit_t *u = &units[i];
		uint16_t arm = 0xFFFF;
		uint8_t st = 0;                      // тип солдата (soldiers): рост и броня по умолчанию
		u->hand = 1;                         // активная рука — правая (BattleUnit: STR_RIGHT_HAND)
		if (u->sol != 0xFF) {
			soldier_t s;
			far_read(FAR(SOLDIER_PAGE, 0) + (uint16_t)u->sol * sizeof s, &s, sizeof s);
			u->bravery = s.cur.bravery; u->reactions = s.cur.reactions; u->firing = s.cur.firing;
			u->throwing = s.cur.throwing; u->strength = s.cur.strength; u->psi_str = s.cur.psi_strength;
			u->psi_skill = s.cur.psi_skill; u->melee = s.cur.melee;
			arm = s.armor == 0xFF ? 0xFFFF : s.armor;
			st = s.type;
		} else {
			u->bravery = 50; u->reactions = 50; u->firing = 60; u->throwing = 60;
			u->strength = 30; u->melee = 40;
		}
		{
			rtab_t t;
			r_soldiers_t sr;
			if (rtab_open(RES_RULE_SOLDIERS, &t) && st < t.n) {
				rtab_get(&t, st, &sr);
				u->h_stand = sr.stand_height; u->h_kneel = sr.kneel_height;
				if (u->sol == 0xFF) arm = sr.armor;   // отладочный боец — броня типа солдата
			}
		}
		unit_armor(u, arm);
	}
	bt_reserve = RES_NONE;
	bt_res_kneel = 0;
	if (nunits) center_on_unit();        // камера на первого бойца отряда
	loaded = 1;
	return 1;
}

// Панель (bhud.c, банк 33): показатели выбранного бойца, предметы в руках, нажатые кнопки
static void draw_panel(void)
{
	bhud_t p;
	uint8_t i = sel < nsold ? sel : 0;
	const unit_t *u = &units[i];
	p.on = nsold && u->alive;
	p.level = bv_level();
	p.tu = u->tu; p.tu_max = u->tu_max; p.en = u->en; p.en_max = u->en_max;
	p.hp = u->hp; p.hp_max = u->hp_max; p.stun = u->stun; p.mor = u->mor;
	p.sol = u->sol; p.unit = i; p.kneel = u->kneel;
	p.reserve = bt_reserve; p.res_kneel = bt_res_kneel;
	p.page = pf_page;
	hud_draw(&p);
}

// Развёртывание НЛО типа utype — то, что называется как сам НЛО (alienDeployments STR_SMALL_SCOUT
// и т. п., BattlescapeGenerator::run: getDeployment(ufo->getRules()->getType())); #FFFF — нет
static uint16_t ufo_deploy(uint8_t utype)
{
	rtab_t t;
	if (!rtab_open(RES_RULE_UFOS, &t) || utype >= t.n) return 0xFFFF;
	uint16_t name = rtab_word(&t, utype, 0);
	if (!rtab_open(RES_RULE_ALIENDEPLOYMENTS, &t)) return 0xFFFF;
	uint16_t i;
	for (i = 0; i < t.n; i++) {
		uint16_t w = rtab_word(&t, i, 0);    // сравнение — с копией (SDCC 4.5, checkasm)
		if (w == name) break;
	}
	return i < t.n ? i : 0xFFFF;
}

// ---------------------------------------------------------------- ход сторон
// BattlescapeGame::endTurn / SavedBattleGame::endTurn: X-COM -> пришельцы -> (мирные) -> X-COM,
// номер хода растёт, когда ход снова у X-COM. Сторона, к которой перешёл ход, восстанавливает
// время и энергию (BattleUnit::prepareNewTurn -> recoverTimeUnits; вес снаряжения и раны — со
// стрельбой), чужие прячутся до нового пересчёта обзора. После хода — окно NextTurnState.
// Хода пришельцев (ИИ) ещё нет: их ход кончается сразу.
static void side_prepare(uint8_t side)
{
	for (uint8_t i = 0; i < nunits; i++) {
		unit_t *u = &units[i];
		if (!u->alive || u->faction != side) continue;
		u->done = 0;                     // _dontReselect = false
		u->tu = u->tu_max;
		uint16_t e = (uint16_t)u->en + u->en_rec;
		u->en = e > u->en_max ? u->en_max : (uint8_t)e;
	}
}

static void end_turn(void)
{
	bt_side = FACTION_HOSTILE;
	side_prepare(FACTION_HOSTILE);
	for (uint8_t i = nsold; i < nunits; i++) units[i].seen = 0;   // setVisible(false) не своим
	for (uint8_t i = 0; i < nunits; i++) ru_put(i);
	bv_sync(nunits);
	path_n = 0;
	turn_dir = 0xFF;
	bv_pause();                          // окно хода рисует в основной холст, карту не трогает
	marks_show(0, 0);
	UI_GO(A_PUSH, SCR_NEXT_TURN);
}

static void next_turn_close(void)
{
	if (bt_side == FACTION_HOSTILE) ai_start();   // ход пришельцев — ИИ по тактам (bai.c)
	UI_GO(A_POP, 0);                     // назад в бой: вид соберётся заново (bv_draw)
}

// Такт хода пришельцев (bai.c): сдвинулись или стреляли — обзор отряда заново; походили все —
// ход X-COM и его окно
// BattlescapeMessage «Hidden Movement» (Map.cpp:96-101, 221-228): окно на месте карты на фоне
// TAC00.SCR, крупно по центру, пока ходит невидимый пришелец (bai.c AI_HIDDEN). Не окно
// интерфейса: рисуется в основной холст, а вид просто перестаёт показывать карту — её холсты
// целы, и вернуть её — без пересборки (окна интерфейса в бою закрываются полной перерисовкой)
static uint8_t hid_on;
static void hidden_show(uint8_t on)
{
	hid_on = on;
	bv_message(on);
	if (!on) { marks_on(); return; }
	marks_show(0, 0);
	gfx_window(0, 0, SCREEN_W, VIEW_H, ui_color_in(UI_SCR_BATTLESCAPE, UI_EL_MESSAGEWINDOWBORDER, 0), RES_TAC00_SCR, 0);
	char s[32];
	tbox_t b;
	str_copy(STR_HIDDEN_MOVEMENT, s, sizeof s);
	b.x = 0; b.y = 64; b.w = SCREEN_W; b.h = 17; b.font = FNT_BIG; b.flags = TX_CENTER;
	b.color = ui_color_in(UI_SCR_BATTLESCAPE, UI_EL_MESSAGEWINDOWS, 0);
	b.color2 = ui_color_in(UI_SCR_BATTLESCAPE, UI_EL_MESSAGEWINDOWS, 1);
	text_draw(&b, s);
}

static void ai_run(void)
{
	aictx_t c;
	c.page = pf_page; c.cells = cells; c.sx = m_sx; c.sy = m_sy; c.sz = m_sz; c.n = nunits; c.nsold = nsold;
	uint8_t r = ai_tick(&c);
	if (r & AI_SEE) see_update();
	if (r & AI_HIT) ui_dirty(0);         // панель — только после выстрела: каждый такт она мигала
	if (!(r & AI_DONE)) {                // «Hidden Movement»: ходит невидимый — окно вместо карты
		uint8_t want = (r & AI_HIDDEN) != 0, on = hid_on;
		if (want != on) hidden_show(want);
		return;
	}
	bt_side = FACTION_PLAYER;
	bt_turn++;
	side_prepare(FACTION_PLAYER);
	if (sel < nsold && !units[sel].alive) sel_step(0);   // выбранного убили — следующий живой
	see_update();                        // заново: кого отряд видит с мест, где стоит
	if (hid_on) hidden_show(0);
	bv_pause();
	marks_show(0, 0);
	UI_GO(A_PUSH, SCR_NEXT_TURN);
}

// ---------------------------------------------------------------- прервать миссию
// В корабле — на его полу (части от m_cbase) внутри его прямоугольника: так лежат клетки
// START_POINT (MCD.Special_Type) у карт кораблей; самого признака в наших MCDSET нет
static uint8_t in_craft(const unit_t *u)
{
	if (!m_cw || u->x < m_cx || u->y < m_cy || u->x >= m_cx + m_cw || u->y >= m_cy + m_cl) return 0;
	uint8_t c;
	far_read(cells + ((uint32_t)((uint16_t)u->z * m_sy + u->y) * m_sx + u->x) * 4, &c, 1);
	return c >= m_cbase && c;
}

// Броня бойца (BattleUnit(Soldier*)): значения по сторонам и прибавка к характеристикам
// (_stats += armor->getStats())
static void unit_armor(unit_t *u, uint16_t arm)
{
	rtab_t t;
	r_armors_t a;
	u->armor_rule = arm;
	if (!rtab_open(RES_RULE_ARMORS, &t) || arm >= t.n) return;
	rtab_get(&t, arm, &a);
	u->armor[0] = a.front_armor; u->armor[1] = u->armor[2] = a.side_armor;
	u->armor[3] = a.rear_armor; u->armor[4] = a.under_armor;
	u->tu += a.stats.tu; u->tu_max += a.stats.tu; u->en += a.stats.stamina; u->en_max += a.stats.stamina;
	u->hp += a.stats.health; u->hp_max += a.stats.health; u->bravery += a.stats.bravery;
	u->reactions += a.stats.reactions; u->firing += a.stats.firing; u->throwing += a.stats.throwing;
	u->strength += a.stats.strength; u->psi_str += a.stats.psi_strength; u->psi_skill += a.stats.psi_skill;
	u->melee += a.stats.melee;
}

// ---------------------------------------------------------------- характеристики
static void uinfo_draw(void)
{
	const unit_t *u = &units[sel < nsold ? sel : 0];
	ustat_t s;
	s.sol = u->sol; s.type = u->type;
	s.tu = u->tu; s.tu_max = u->tu_max; s.en = u->en; s.en_max = u->en_max;
	s.hp = u->hp; s.hp_max = u->hp_max; s.stun = u->stun; s.mor = u->mor;
	s.bravery = u->bravery; s.reactions = u->reactions; s.firing = u->firing; s.throwing = u->throwing;
	s.melee = u->melee; s.strength = u->strength; s.psi_str = u->psi_str; s.psi_skill = u->psi_skill;
	memcpy(s.armor, u->armor, 5);
	stats_draw(&s);
}

// ---------------------------------------------------------------- выстрел
// ProjectileFlyBState::init: времени хватает, обойма есть и не пуста, цель в пределах maxRange;
// боец поворачивается к цели, тратит время, очередь — autoShots выстрелов по патрону. Траектория
// и урон — bfire.c (банк 35); выбывший (здоровье 0 или оглушение не меньше здоровья) — со
// сцены (трупа и падения ещё нет). Полёт пули и вспышка попадания — дальше.
static void aim_off(void)
{
	bt_aim = 0;
	bv_aim(0);
}

static void shoot(uint8_t tx, uint8_t ty)
{
	uint8_t i = sel < nsold ? sel : 0;
	unit_t *u = &units[i];
	bitem_t b;
	rtab_t t;
	r_items_t w;
	if (!bi_hand(pf_page, i, bt_hand, &b) || !rtab_open(RES_RULE_ITEMS, &t) || b.item >= t.n) { aim_off(); return; }
	rtab_get(&t, b.item, &w);
	if (u->tu < bt_action_tu) { hud_warn(STR_NOT_ENOUGH_TIME_UNITS); return; }
	if (w.compatible_ammo.n && b.ammo == 0xFFFF) { hud_warn(STR_NO_AMMUNITION_LOADED); return; }
	if (w.compatible_ammo.n && !b.rounds) { hud_warn(STR_NO_ROUNDS_LEFT); return; }
	int16_t dx = (int16_t)tx - u->x, dy = (int16_t)ty - u->y;
	if ((uint16_t)(dx * dx + dy * dy) > (uint16_t)w.max_range * w.max_range) { hud_warn(STR_OUT_OF_RANGE); return; }
	u->dir = dir_to(dx, dy);             // lookAt
	u->tu -= bt_action_tu;
	uint8_t shots = bt_action == BA_AUTOSHOT ? w.auto_shots : 1;
	bashot_t q;                              // пуля — bact.c (банк 36): траектория, полёт, урон
	q.page = pf_page; q.cells = cells; q.sx = m_sx; q.sy = m_sy; q.sz = m_sz;
	q.shooter = i; q.n = nunits; q.tx = tx; q.ty = ty; q.tz = bv_level();
	q.action = bt_action; q.acc = bt_acc; q.weapon = b.item; q.ammo = b.ammo;
	for (uint8_t k = 0; k < shots; k++) {
		if (w.compatible_ammo.n) {           // BattleItem::spendBullet; кончились — очередь обрывается
			bitem_t c;
			if (!bi_hand(pf_page, i, bt_hand, &c) || !c.rounds) break;
			bi_spend(pf_page, i, bt_hand);
		}
		ba_shot(&q);
	}
	unit_changed(i);                     // повернулся
	aim_off();
	see_update();
	ui_dirty(0);
}

// ---------------------------------------------------------------- меню действий
// BattlescapeState::handleItemClick -> ActionMenuState: пункты — по предмету в руке (bmenu.c),
// выбранное действие ждёт цели (прицел и выстрел — дальше)
static uint8_t am_hl = 0xFF;             // пункт под курсором

static void amenu_open(uint8_t right)
{
	if (!nsold || bv_walking() != 0xFF || bt_side != FACTION_PLAYER) return;
	uint8_t i = sel < nsold ? sel : 0;
	const unit_t *u = &units[i];
	amreq_t r;
	r.unit = i; r.right = right; r.tu_max = u->tu_max; r.firing = u->firing; r.throwing = u->throwing;
	r.melee = u->melee; r.psi_skill = u->psi_skill; r.hp = u->hp; r.hp_max = u->hp_max;
	r.kneel = u->kneel; r.page = pf_page;
	if (!am_build(&r)) return;
	bt_hand = right;
	units[i].hand = right;               // setActiveHand
	am_hl = 0xFF;
	path_n = 0;
	bv_snapshot();                       // карта остаётся под меню картинкой
	marks_show(0, 0);
	UI_GO(A_PUSH, SCR_ACTION_MENU);
}

static void amenu_event(uint8_t ev, uint8_t arg)
{
	(void)arg;
	switch (ev) {
	case EVT_DRAW: am_draw(am_hl); break;
	case EVT_TICK: {
		uint8_t h = am_hit(cursor_x, cursor_y), o = am_hl;   // ActionMenuItem::mouseIn / mouseOut
		if (h != o) { am_hl = h; am_hover(o, h); }
		break;
	}
	case EVT_BUTTON: {
		if (ui_arrow_max) { UI_GO(A_POP, 0); break; }   // правая кнопка — закрыть
		uint8_t h = am_hit(cursor_x, cursor_y);
		if (h == 0xFF) break;
		bt_action = am_action(h, &bt_action_tu, &bt_acc);
		// выстрелы ждут цель: курсор — прицел (BattlescapeGame::setupCursor, CT_AIM)
		bt_aim = bt_action == BA_AUTOSHOT || bt_action == BA_SNAPSHOT || bt_action == BA_AIMEDSHOT;
		bv_aim(bt_aim);
		UI_GO(A_POP, 0);
		break;
	}
	case EVT_KEY:
		if (arg == KEY_ESC) UI_GO(A_POP, 0);
		break;
	}
}

// Геоскейп сообщает, какая миссия начинается: развёртывание (state.h site_t.deployment),
// террейн НЛО и террейн корабля отряда; #FFFF — не задано. utype — тип НЛО (#FF — не НЛО),
// race — раса миссии (#FF — из развёртывания).
void bat_mission(uint16_t deploy, uint16_t ufo, uint16_t craft_map, uint16_t crew, uint8_t utype, uint8_t race) __banked
{
	mis_utype = utype;
	mis_race = race;
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
	(void)line;
	buf[0] = 0;
	if (id == SCR_UNIT_INFO) {
		if (slot) { buf[0] = slot == 1 ? '<' : '>'; buf[1] = buf[0]; buf[2] = 0; }
		return;
	}
	if (id == SCR_ABORT) {               // бойцы на полу своего корабля (START_POINT) и прочие
		uint8_t in = 0, out = 0;
		for (uint8_t i = 0; i < nsold; i++) {
			const unit_t *u = &units[i];
			if (!u->alive) continue;
			if (in_craft(u)) in++; else out++;
		}
		str_plural(buf, slot ? STR_UNITS_OUTSIDE_one : STR_UNITS_IN_CRAFT_one, slot ? out : in);
		return;
	}
	if (id != SCR_NEXT_TURN) return;
	char a[16];
	if (slot == 0) fmt_num(a, bt_turn, 0);                              // TURN> n
	else str_copy(bt_side == FACTION_PLAYER ? STR_XCOM : STR_ALIENS, a, sizeof a);   // SIDE> ...
	str_fmt(buf, str_get(slot ? STR_SIDE : STR_TURN), a, "");
}

uint8_t bat_rows(uint8_t id, uint8_t slot) __banked
{
	(void)id; (void)slot;
	return 0;
}

uint8_t bat_event(uint8_t id, uint8_t ev, uint8_t arg) __banked
{
	if (id == SCR_NEXT_TURN) {
		if (ev == EVT_BUTTON || ev == EVT_KEY) next_turn_close();
		return 0;
	}
	if (id == SCR_ACTION_MENU) { amenu_event(ev, arg); return 0; }
	if (id == SCR_BATTLE_PAUSE) return 0;   // кнопки ведут в окна меню сами
	if (id == SCR_UNIT_INFO) {
		if (ev == EVT_DRAW) uinfo_draw();
		else if (ev == EVT_BUTTON) {
			if (arg == 1) UI_GO(A_POP, 0);
			else {                       // btnPrevClick / btnNextClick
				if (arg == 3) sel_next(); else sel_prev();
				ui_dirty(0);
			}
		} else if (ev == EVT_KEY && arg == KEY_ESC) UI_GO(A_POP, 0);
		return 0;
	}
	if (id == SCR_ABORT) {               // OK: finishBattle(aborted) — разбора ещё нет, просто выход
		if (ev == EVT_BUTTON && arg == 1) UI_GO(A_POP2, 0);
		return 0;
	}
	switch (ev) {
	case EVT_MUSIC: {
		// Тема боя: роль MK_TACTIC из таблицы MUSGRP (22 §1.3), как GMTACTIC в оригинале
		uint16_t id = mus_kind(MK_TACTIC);
		if (id) mus_play(id);
		break;
	}
	case EVT_OPEN:
		bv_open();                           // список вида, холсты, сплит, курсор боя
		marks_init();                        // плитки значков замеченных (спрайты TSU)
		hud_draw(0);                         // плашки предупреждения нет
		nmarks = 0;
		// Память банка при старте не обнуляется: без этого aliens_free отдавал в пул мусорные
		// номера страниц (а с ними — каталог SD, и карта не находила своих блоков)
		aliens_reset();
		globe_det_reset();                   // кэш подписей глобуса отдаёт страницы бою
		if (pf_page == PG_NONE) pf_page = pg_alloc(3, 1);   // рабочая память боя: поиск пути, таблицы, воксели
		if (pf_page == PG_NONE) { dbg_puts("battle: no pages\n"); break; }   // иначе — в страницу #FF
		// Карта миссии собирается генератором (16 §3). Пока миссии нет, террейн берётся
		// по очереди — клавиша G дальше пересобирает карту следующего террейна.
		load_gen(gen_terrain);
		break;
	case EVT_TICK: {
		if (!gen_mode || !loaded) break;
		if (bt_side == FACTION_HOSTILE) { ai_run(); break; }
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
				see_update();                // шаг — пересчёт обзора (UnitWalkBState)
			} else if (r == 2) {             // следующий шаг — в том же такте, без кадра стойки
				if (path_n) step_unit();     //   (обзор — после: он может перерисовать вид)
				bv_walk_done();              // не пошёл (поворот, дверь, конец) — стойка
				see_update();
				if (!path_n) ui_dirty(0);    // путь кончился — обновить панель
			}
		} else if (path_n) {                 // идём по пути: поворот раз в несколько кадров
			if (move_wait) move_wait--;
			else { move_wait = 3; step_unit(); if (!path_n) ui_dirty(0); }   // в конце — обновить панель
		}
		if (an_on && (uint8_t)((uint8_t)frames - an_tm) >= 5) door_anim_step();   // 100 мс (Tile::animate)
		if ((uint8_t)((uint8_t)frames - bl_tm) >= 5) { bl_tm = (uint8_t)frames; marks_blink(); }   // _animTimer
		if (hud_warn_tick()) ui_dirty(0);    // плашка исчезла — панель заново
		bv_tick(sel);                        // прокрутка, курсор клетки, стрелка над выбранным
		break;
	}
	case EVT_DRAW:
		if (arg) break;                      // свой виджет есть и у кнопки «потратить время» — рисуем раз
		bv_draw();
		draw_panel();
		marks_on();                          // поверх боя могли открывать окно — значки снова
		break;
	case EVT_CLOSE:
		// Бой отдаёт всю свою память: карту, наборы тайлов, лист юнитов, список вида и
		// рабочую память поиска пути. Иначе геоскейпу не из чего заводить слоты кэша, и он
		// перечитывает каждый фон с карты — экран залипает (14 §todo).
		bv_close();
		marks_show(0, 0);
		aliens_free();                   // листы брони пришельцев
		vis_free();
		bat_unload();                    // наборы частей, листы бойца и курсора, генератор (bload.c)
		gen_mode = 0;
		loaded = 0;
		nunits = 0;
		if (pf_page != PG_NONE) { pg_free(pf_page, 3); pf_page = PG_NONE; }
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
		if (bt_side != FACTION_PLAYER) break;   // ход пришельцев: интерфейс ждёт (BattlescapeState)
		switch (arg) {
		case 0: {                            // клик по карте
			uint8_t mk = mark_at();
			if (mk != 0xFF) {                // значок пришельца — камера на него (btnVisibleUnitClick)
				const unit_t *a = &units[marks_u[mk]];
				if (a->z != bv_level()) bv_set_level(a->z, 0);
				bv_center(a->x, a->y);
				ui_dirty(0);
				break;
			}
			if (!gen_mode || !nunits || bv_walking() != 0xFF) break;   // идёт — ждать
			uint8_t tx, ty;
			if (!bv_cursor(&tx, &ty)) break;
			if (bt_aim) {                    // прицел: левая — выстрел, правая — отмена
				if (ui_arrow_max) aim_off(); else shoot(tx, ty);
				break;
			}
			unit_t *u = &units[sel < nunits ? sel : 0];
			if (ui_arrow_max) {              // правая кнопка: развернуться туда и открыть дверь
				path_n = 0;
				turn_dir = dir_to((int16_t)tx - (int16_t)u->x, (int16_t)ty - (int16_t)u->y);
				move_wait = 0;
				break;
			}
			uint8_t no = unit_no(u->z, ty, tx);
			if (no && no - 1 != sel && no <= nsold) {   // свой боец — выбрать его (primaryAction оригинала)
				sel = (uint8_t)(no - 1);
				path_n = 0;
				see_update();
				ui_dirty(0);
				break;
			}
			pf_req_t q = { cells, m_sx, m_sy, u->z, m_sz, u->x, u->y, tx, ty, u->tu, STEP_TU, pf_page };
			path_n = pf_find(&q, path, PATH_MAX);
			path_i = 0;
			move_wait = 0;
			break;
		}
		case 1: case 2: unit_updown(arg == 1); break;   // этаж выше / ниже (moveUpDown)
		case 20:                             // NextStop: этого больше не выбирать, следующий
			if (sel < nsold) units[sel].done = 1;
			sel_next();
			break;
		case 21:                             // правой кнопкой — время выбранного бойца в ноль
			if (ui_arrow_max && sel < nsold && bv_walking() == 0xFF) { units[sel].tu = 0; path_n = 0; ui_dirty(0); }
			break;
		case 3: { uint8_t l = bv_level(); if (l + 1 < m_sz) { bv_set_level(l + 1, 1); ui_dirty(0); } break; }
		case 4: { uint8_t l = bv_level(); if (l) { bv_set_level(l - 1, 1); ui_dirty(0); } break; }
		case 5:                              // инвентарь: окно рисуется в строки 0–199 основного
			bv_suspend();                    //   холста — без сплита и с ним на экране
			marks_show(0, 0);
			UI_GO(A_PUSH, SCR_INVENTORY);
			break;
		case 6: center_on_unit(); ui_dirty(0); break;
		case 7: sel_next(); break;           // следующий боец
		case 10: do_kneel(); break;          // присесть и встать
		case 11: case 12: case 13: case 14:  // резерв времени — группа кнопок
			bt_reserve = (uint8_t)(arg - 11);
			ui_dirty(0);
			break;
		case 15: bt_res_kneel = !bt_res_kneel; ui_dirty(0); break;
		case 22:                             // меню паузы поверх боя (видно и карту, и панель)
			if (bv_walking() != 0xFF) break;
			bv_snapshot();
			marks_show(0, 0);
			UI_GO(A_PUSH, SCR_BATTLE_PAUSE);
			break;
		case 19:                             // характеристики выбранного бойца
			if (!nsold || bv_walking() != 0xFF) break;
			bv_pause();                      // окно рисует в основной холст, карту не трогает
			marks_show(0, 0);
			UI_GO(A_PUSH, SCR_UNIT_INFO);
			break;
		case 18:                             // прервать миссию: окно поверх карты, панель видна
			if (bv_walking() != 0xFF) break;
			bv_pause();
			marks_show(0, 0);
			UI_GO(A_PUSH, SCR_ABORT);
			break;
		case 16: case 17: amenu_open((uint8_t)(arg - 16)); break;   // handleItemClick
		case 9:                              // «1/2»: все этажи или только текущий
			bv_all_levels();
			ui_dirty(0);
			break;
		case 8: {                            // конец хода: время и энергия бойцов восстанавливаются,
			uint8_t list[24];                //   двери НЛО закрываются (BattlescapeGame::endTurn)
			uint8_t n = doors_close_ufo(&bmap, list, 8);
			an_on = 0;
			for (uint8_t i = 0; i < n; i++) {
				bv_cell(list[i * 3], list[i * 3 + 1], list[i * 3 + 2]);
				vis_cell(cells, list[i * 3], list[i * 3 + 1], list[i * 3 + 2], pf_page);
			}
			end_turn();
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
			see_update();
			dbg_puts("door: at "); dbg_dec(dx); dbg_puts(","); dbg_dec(dy);
			dbg_puts(" slot "); dbg_dec(ds); dbg_puts("\n");
			center_on_unit();
			break;
		}
		case 'j': case 'J': {               // отладка: камера на следующего пришельца; Shift+J — ещё и
			static uint8_t dbg_al;          //   выбранного бойца к нему (3 клетки, лицом к нему)
			if (nunits <= nsold) break;
			if (dbg_al < nsold || dbg_al >= nunits) dbg_al = nsold;
			unit_t *a = &units[dbg_al];
			if (arg == 'J' && nsold) {
				uint8_t i = sel < nsold ? sel : 0;
				unit_t *u = &units[i];
				u->x = a->x + 3 < m_sx ? a->x + 3 : a->x - 3; u->y = a->y; u->z = a->z;
				u->dir = dir_to((int16_t)a->x - u->x, 0);
				ru_put(i);
				bv_sync(nunits);
				see_update();
			}
			if (a->z != bv_level()) bv_set_level(a->z, 0);
			bv_center(a->x, a->y);
			dbg_puts("alien "); dbg_dec(dbg_al - nsold); dbg_puts(" at "); dbg_dec(a->x); dbg_puts(",");
			dbg_dec(a->y); dbg_puts(","); dbg_dec(a->z); dbg_puts(" sheet "); dbg_dec(a->sheet);
			dbg_puts(" hp "); dbg_dec(a->hp); dbg_puts(" tu "); dbg_dec(a->tu); dbg_puts("\n");
			dbg_al++;
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
