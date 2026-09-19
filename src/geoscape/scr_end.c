// Банк 2: концовки — CutsceneState/SlideshowState (заставки cutscenes.rul: картинки —
// CUTS.PAK с SD, палитры и подписи — таблица RES_CUTSCENES, конвертер Cutscenes) и
// StatisticsState. Видео (FLI/VID) не переносятся — только слайды; музыка — с §3.16.
// Строки статистики, для которых нужен бой на земле (миссии, убийства, точность…), — 0.
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "far.h"
#include "res.h"
#include "rules.h"
#include "text.h"
#include "gfx.h"
#include "state.h"
#include "game.h"
#include "scrdef.h"
#include "music.h"

extern volatile uint16_t frames;         // crt0.s

typedef struct {
	uint16_t img, pal, str;              // картинка (CUTS.PAK), палитра, подпись (NONE16 — нет)
	int16_t x, y, w, h;                  // рамка подписи
	uint8_t color, align, secs, pad;
} slide_t;

static slide_t sl;
static uint8_t cut_type, cut_i, cut_next, cut_def;
static uint16_t cut_at;

// Слайд i заставки cut_type -> sl; 0 — нет такого
static uint8_t slide_load(uint8_t i)
{
	res_t r;
	uint8_t h[4];
	if (!res_find(RES_CUTSCENES, &r)) return 0;
	uint16_t off = far_word(r.phys + cut_type * 2);
	if (!off) return 0;
	far_read(r.phys + off, h, 4);
	if (i >= h[0]) return 0;
	cut_def = h[2];
	if (!i && h[1] != 0xFF) mus_play(MUS_BASE + h[1]);   // тема слайд-шоу (cutscenes.rul musicId)
	far_read(r.phys + off + 4 + (uint16_t)i * sizeof(slide_t), &sl, sizeof(slide_t));
	if (!sl.secs) sl.secs = cut_def;
	return 1;
}

uint8_t cut_start(uint8_t type, uint8_t next) __banked
{
	cut_type = type;
	cut_next = next;
	cut_i = 0;
	return slide_load(0) ? SCR_SLIDESHOW : next;
}

void cut_play(uint8_t type, uint8_t next) __banked
{
	UI_GO(A_SET, cut_start(type, next));
}

static void slide_draw(void)
{
	gfx_palette(sl.pal, -1);
	gfx_bg(sl.img, 0, 0, 320, 200);
	if (sl.str == NONE16) return;
	tbox_t b;
	b.x = sl.x; b.y = sl.y; b.w = sl.w; b.h = sl.h;
	b.font = FNT_BIG; b.color = b.color2 = sl.color;   // Text по умолчанию крупный
	b.flags = TX_WRAP | (sl.align == 1 ? TX_CENTER : sl.align == 2 ? TX_RIGHT : 0);
	text_draw(&b, str_get(sl.str));
}

// screenClick: следующий слайд или конец (screenSkip)
static void slide_next(void)
{
	if (slide_load(++cut_i)) { cut_at = frames; UI_GO(A_REDRAW, 0); }
	else UI_GO(A_SET, cut_next);
}

static const wdef_t w_slide[] = {
	CUS(0, 0, 320, 200, NOSTR, A_CUSTOM, 1),
	HOT(0, 0, 0, 0, A_CUSTOM, 1, ENT),
	HOT(0, 0, 0, 0, A_CUSTOM, 2, ESC),
};

// ---------------------------------------------------------------- статистика

static const wdef_t w_stats[] = {
	WINP(0, 0, 320, 200, UI_EL_WINDOW, POPB),
	BTN(135, 180, 50, 12, UI_EL_BUTTON, STR_OK, A_CUSTOM, 1, ENT),
	TXT(5, 8, 310, 25, UI_EL_TEXT, DYN(0), BIG | TC),
	LST(12, 36, 280, 136, UI_EL_LIST, 1, 0),
	HOT(0, 0, 0, 0, A_CUSTOM, 1, ESC),
};

#define NSTATS 28
static const uint16_t stat_str[NSTATS] = {
	STR_DIFFICULTY, STR_AVERAGE_MONTHLY_RATING, STR_TOTAL_INCOME, STR_TOTAL_EXPENDITURE, STR_MISSIONS_WON,
	STR_MISSIONS_LOST, STR_NIGHT_MISSIONS, STR_BEST_RATING, STR_WORST_RATING, STR_SOLDIERS_RECRUITED,
	STR_SOLDIERS_LOST, STR_ALIEN_KILLS, STR_ALIEN_CAPTURES, STR_FRIENDLY_KILLS, STR_AVERAGE_ACCURACY,
	STR_WEAPON_MOST_KILLS, STR_ALIEN_MOST_KILLS, STR_LONGEST_SERVICE, STR_TOTAL_DAYS_WOUNDED, STR_TOTAL_UFOS,
	STR_TOTAL_ALIEN_BASES, STR_COUNTRIES_LOST, STR_TOTAL_TERROR_SITES, STR_TOTAL_BASES, STR_TOTAL_CRAFT,
	STR_TOTAL_SCIENTISTS, STR_TOTAL_ENGINEERS, STR_TOTAL_RESEARCH,
};
static int32_t stat_v[NSTATS];

static uint8_t bits(const uint8_t *p, uint8_t n)
{
	uint8_t c = 0;
	for (uint8_t i = 0; i < n; i++) for (uint8_t b = p[i]; b; b &= b - 1) c++;
	return c;
}

// StatisticsState::listStats (то, что ведётся без боя на земле)
static void stats_calc(void)
{
	uint8_t hl = ST->hist_len;
	int32_t score = 0, inc = 0, exp = 0;
	memset(stat_v, 0, sizeof stat_v);
	for (uint8_t m = 0; m < hl; m++) {
		score += ST->fin.research[m];
		inc += ST->fin.income[m];
		exp += ST->fin.expenditure[m];
		for (uint8_t r = 0; r < MAX_REGIONS; r++) score += ST->region[r].act_xcom[m] - ST->region[r].act_alien[m];
	}
	stat_v[1] = hl ? score / hl : 0;
	stat_v[2] = inc;
	stat_v[3] = exp;
	stat_v[9] = soldiers_count(NONE8, 0xFE, 0);   // живые (погибших пока нет — боя на земле нет)
	stat_v[19] = ST->ids[ID_UFO];                  // ids — последний выданный номер (у OpenXcom — следующий)
	for (uint8_t b = 0; b < MAX_ALIEN_BASES; b++) if (ST->abase[b].id && (ST->abase[b].flags & AB_DISCOVERED)) stat_v[20]++;
	for (uint8_t c = 0; c < MAX_COUNTRIES; c++) if (ST->country[c].flags & CF_PACT) stat_v[21]++;
	stat_v[22] = ST->ids[ID_TERROR];
	for (uint8_t b = 0; b < MAX_BASES; b++) {
		if (!ST->base[b].name[0]) continue;
		uint16_t sci, eng;
		stat_v[23]++;
		base_personnel(b, &sci, &eng);
		stat_v[25] += sci;
		stat_v[26] += eng;
	}
	for (uint8_t i = ID_CRAFT; i < NIDS; i++) stat_v[24] += ST->ids[i];
	stat_v[27] = bits(ST->discovered, RES_BITS);
}

static const scr_t tab[] = {
	SCR(SCR_SLIDESHOW, UI_SCR_MAINMENU, NOUI, 0, 0, w_slide),
	SCR(SCR_STATISTICS, UI_SCR_ENDGAMESTATISTICS, NOUI, RES_BACK01_SCR, 0, w_stats),
};

uint8_t end_get(uint8_t id, sdef_t *s, wdef_t *w) __banked
{
	return scr_find(tab, sizeof tab / sizeof tab[0], id, s, w);
}

void end_text(uint8_t id, uint8_t slot, uint8_t row, char *buf) __banked
{
	static const uint16_t month_str[12] = {
		STR_JAN, STR_FEB, STR_MAR, STR_APR, STR_MAY, STR_JUN, STR_JUL, STR_AUG, STR_SEP, STR_OCT, STR_NOV, STR_DEC,
	};
	static const uint16_t diff_str[5] = { STR_1_BEGINNER, STR_2_EXPERIENCED, STR_3_VETERAN, STR_4_GENIUS, STR_5_SUPERHUMAN };
	if (id != SCR_STATISTICS) return;
	if (slot == 0) {                              // «Поражение» / «Победа» / «Статистика», дата
		char n[8];
		uint8_t e = ST->ending, d = ST->day % 10;
		str_copy(e == END_WIN ? STR_VICTORY : e == END_LOSE ? STR_DEFEAT : STR_STATISTICS, buf, 64);
		strcat(buf, "\x02");
		fmt_num(n, ST->day, 0);
		uint16_t ds = (ST->day / 10 == 1 || d == 0 || d > 3) ? STR_DATE_FOURTH : d == 1 ? STR_DATE_FIRST : d == 2 ? STR_DATE_SECOND : STR_DATE_THIRD;
		str_fmt(buf + strlen(buf), str_get(ds), n, "");
		strcat(buf, " ");
		str_copy(month_str[(ST->month + 11) % 12], buf + strlen(buf), 32);
		strcat(buf, " ");
		fmt_num(buf + strlen(buf), ST->year, 0);
		return;
	}
	if (row == LIST_COLS) {                       // setColumns(2, 200, 80), setDot
		static const uint8_t cw[] = { 200, 80 }, ca[] = { LC_DOT, 0 };
		list_cols(buf, 2, cw, ca);
		return;
	}
	if (row >= NSTATS) return;
	str_copy(stat_str[row], buf, 64);
	strcat(buf, "\t");
	char *p = buf + strlen(buf);
	switch (row) {
	case 0: str_copy(diff_str[ST->difficulty < 5 ? ST->difficulty : 0], p, 64); break;
	case 2: case 3: fmt_funds(p, stat_v[row]); break;
	case 14: fmt_num(p, stat_v[row], 0); strcat(p, "%"); break;
	case 15: case 16: str_copy(STR_NONE, p, 32); break;
	default: fmt_num(p, stat_v[row], ',');
	}
}

uint8_t end_rows(uint8_t id, uint8_t slot) __banked
{
	(void)slot;
	return id == SCR_STATISTICS ? NSTATS : 0;
}

uint8_t end_event(uint8_t id, uint8_t ev, uint8_t arg) __banked
{
	if (id == SCR_SLIDESHOW) {
		if (ev == EVT_OPEN) { cut_at = frames; cursor_off = 1; }
		else if (ev == EVT_CLOSE) cursor_off = 0;
		else if (ev == EVT_DRAW) slide_draw();
		else if (ev == EVT_BUTTON) {
			if (arg == 2) UI_GO(A_SET, cut_next);   // screenSkip
			else slide_next();
		} else if (ev == EVT_TICK && (uint16_t)(frames - cut_at) >= (uint16_t)sl.secs * 50)
			slide_next();                        // transitionSeconds
		return 0;
	}
	if (ev == EVT_OPEN) stats_calc();
	else if (ev == EVT_BUTTON) {                  // btnOkClick: игра окончена — главное меню
		if (ST->ending != END_NONE) { ctx.in_game = 0; UI_GO(A_SET, SCR_MAIN_MENU); }
		else UI_GO(A_POP, 0);
	}
	return 0;
}
