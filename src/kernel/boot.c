// Запуск (банк 11, выполняется один раз): прерывания, самотест джампера и менеджера
// страниц, 320x200 256c, шрифты и строки, мышь, 14 МГц + кэш, интерфейс (ui_run).
// Замеры этапа 0 (CPU, DMA) — project_docs/05_port_plan.md, в истории findings_log.
#include <stdint.h>
#include "tsconf.h"
#include "memmap.h"
#include "pages.h"
#include "far.h"
#include "res.h"
#include "res_ids.h"
#include "dbg.h"
#include "gfx.h"
#include "text.h"
#include "input.h"
#include "ui.h"
#include "game.h"
#include "screens.h"
#include "rules.h"      // сгенерирован OxzConv; _Static_assert проверяют размеры записей таблиц
#include "../test/banktest.h"

extern uint8_t input_on;                 // crt0.s (Win0): защёлка ввода в прерывании

// ---------------------------------------------------------------- шрифты и строки (text.s)

extern uint8_t tx_font_page;
extern uint16_t tx_font_off[4];
extern uint8_t tx_font_cw[4], tx_font_ch[4];
extern int8_t tx_font_sp[4];
extern far_t tx_str_base;
extern uint16_t tx_str_n;

// Шрифты — в свою страницу пула (при выводе она подключается в Win2), строки —
// ресурс STRINGS на месте (text.s читает его через Win3).
void text_init(void) __banked
{
	static const uint16_t ids[4] = { RES_FONT_BIG, RES_FONT_SMALL, RES_FONT_GEO_BIG, RES_FONT_GEO_SMALL };
	res_t r;
	uint16_t off = 0;
	tx_font_page = pg_alloc(1, 1);
	for (uint8_t i = 0; i < 4; i++) {
		if (!res_find(ids[i], &r)) continue;
		tx_font_off[i] = off;
		far_copy(FAR(tx_font_page, off), r.phys, r.size);
		uint8_t h[3];
		far_read(r.phys, h, 3);
		tx_font_cw[i] = h[0]; tx_font_ch[i] = h[1]; tx_font_sp[i] = (int8_t)h[2];
		off += (uint16_t)((r.size + 1) & ~1ul);
	}
	if (res_find(RES_STRINGS, &r)) {
		tx_str_base = r.phys;
		tx_str_n = far_word(r.phys);
	}
}

// ---------------------------------------------------------------- самотест (tests/selftest.oxs)

int16_t bank_result;
uint8_t pg_test[4];

static void selftest(void)
{
	bank_result = bank_a_calc(7);      // 7*3 + (7 + 1000) = 1028: банк 11 -> банк 14 -> банк 15
	uint8_t a = pg_alloc(16, 16);      // #B0
	uint8_t b = pg_alloc(1, 1);        // #C0
	pg_free(a, 16);
	uint8_t c = pg_alloc(2, 8);        // #B0 снова
	pg_test[0] = a;
	pg_test[1] = b;
	pg_test[2] = c;
	pg_test[3] = pg_free_count();      // 64 - 1 - 2 = 61
	pg_free(b, 1);
	pg_free(c, 2);
	dbg_puts("selftest: banks ");
	dbg_dec(bank_result);
	dbg_puts(", pages #");
	dbg_hex8(a); dbg_puts(" #"); dbg_hex8(b); dbg_puts(" #"); dbg_hex8(c);
	dbg_puts(", free ");
	dbg_dec(pg_test[3]);
	dbg_puts("\n");
}

void boot(void) __banked
{
	TS_INTMASK = INT_FRAME;
	TS_VSINTL = 0;
	TS_VSINTH = 0;
	TS_HSINT = 0;
	__asm__("ei");

	dbg_puts("\nOpenXComZX\n");
	selftest();

	gfx_init();
	text_init();                        // шрифты копируются DMA в страницу пула — до кэша
	input_init();
	input_on = 1;
	opt.music = 1;
	opt.sfx = 1;

	// 14 МГц, кэш окон 0-2 (Win3 — экран и дальние данные, DMA туда пишет)
	TS_SYSCONFIG = SYSCONF_ZCLK_14M;
	TS_CACHECONFIG = CACHE_WIN0 | CACHE_WIN1 | CACHE_WIN2;   // после SysConfig

	// StartState -> CutsceneState(intro) -> главное меню (под сценарием эмулятора — сразу меню)
	ui_run(OXZ_DBG_SCRIPT == OXZ_SCRIPT_SIG ? SCR_MAIN_MENU : cut_start(CUT_INTRO, SCR_MAIN_MENU));
}
