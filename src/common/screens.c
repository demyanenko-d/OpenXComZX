// Диспетчер экранов (банк 1, рядом с ui.c — зовёт только ядро интерфейса): по номеру
// экрана — в функцию банка экранов. craft_name/ufo_name/fmt_funds — __banked (их
// зовут экраны других банков; пишут только в буфер вызывающего). scr_find и
// list_cols — static inline в scrdef.h (своя копия в каждом банке экранов).
#include <stdint.h>
#include <string.h>
#include "memmap.h"
#include "pages.h"
#include "res.h"
#include "rules.h"
#include "ui.h"
#include "text.h"
#include "state.h"
#include "game.h"
#include "scrdef.h"

__at(0xC000) state_t st_;              // ST: ядро состояния в Win3 (state.h)

// Общие данные экранов (Win1): контекст окон, настройки, события для окон.
ctx_t ctx;
options_t opt;
gevent_t ev_cur;
gevent_t arrivals[GEV_MAX];
uint8_t narr;
combo_t combo;

// craft_name, ufo_name, fmt_funds — src/common/names.c (банк 2).

// На время любого вызова экрана в Win3 подключено ядро состояния кампании
// (STATE_PAGE, state.h: указатель ST); прежняя страница восстанавливается.

// Группа экранов — id / 32 (screens.h): 0 меню, 1 геоскейп, 2 события, 3 база,
// 4 покупка/продажа/переводы, 5 исследования/производство, 6 корабль, 7 Уфопедия
// (240.. — графики).
#define DISPATCH(r, fn, ...) switch (id >> 5) { \
	case 0: if (id >= SCR_SLIDESHOW) r end_##fn(__VA_ARGS__); else r menu_##fn(__VA_ARGS__); break; \
	case 1: r geo_##fn(__VA_ARGS__); break; \
	case 2: r geo2_##fn(__VA_ARGS__); break; \
	case 3: r base_##fn(__VA_ARGS__); break; \
	case 4: r base2_##fn(__VA_ARGS__); break; \
	case 5: r lab_##fn(__VA_ARGS__); break; \
	case 6: if (id >= SCR_DOGFIGHT) r dogf_##fn(__VA_ARGS__); \
		else if (id >= SCR_INTERCEPT) r fly_##fn(__VA_ARGS__); else r ship_##fn(__VA_ARGS__); break; \
	default: if (id >= SCR_GRAPHS) r graph_##fn(__VA_ARGS__); else r ufop_##fn(__VA_ARGS__); break; }

uint8_t scr_get(uint8_t id, sdef_t *s, wdef_t *w)
{
	uint8_t r, old = pg_map3(STATE_PAGE);
	DISPATCH(r =, get, id, s, w)
	pg_map3(old);
	return r;
}

void scr_text(uint8_t id, uint8_t slot, uint8_t row, char *buf)
{
	uint8_t old = pg_map3(STATE_PAGE);
	buf[0] = 0;
	DISPATCH(, text, id, slot, row, buf)
	pg_map3(old);
}

uint8_t scr_rows(uint8_t id, uint8_t slot)
{
	uint8_t r, old = pg_map3(STATE_PAGE);
	DISPATCH(r =, rows, id, slot)
	pg_map3(old);
	return r;
}

uint8_t scr_event(uint8_t id, uint8_t ev, uint8_t arg)
{
	uint8_t r, old = pg_map3(STATE_PAGE);
	DISPATCH(r =, event, id, ev, arg)
	pg_map3(old);
	return r;
}
