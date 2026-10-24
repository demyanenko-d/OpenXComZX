// Окно характеристик юнита (банк 33): UnitInfoState (REF/OpenXcom/src/Battlescape/UnitInfoState.cpp).
// Фон UNIBORD.PCK; имя (у солдата — со званием) крупно по центру в (16, 4) 288x17; с y 38 через 9
// строк — название (8, y) 140x9 цветом text.color, число (150, y) цветом text.color2, полоса Bar
// 150x5 в (170, y + 1) цветом своего элемента категории stats. Меткость, бросок и рукопашная —
// с поправкой на здоровье (× здоровье / максимум), полоса — от базового значения. Пси-сила видна,
// только если есть пси-навык (параметр psiStrengthEval оригинала выключен).
#include <stdint.h>
#include <string.h>
#include "far.h"
#include "res.h"
#include "res_ids.h"
#include "rules.h"
#include "str_ids.h"
#include "ui.h"
#include "ui_ids.h"
#include "gfx.h"
#include "text.h"
#include "state.h"
#include "bstats.h"

static const uint16_t rank_str[6] = { STR_ROOKIE, STR_SQUADDIE, STR_SERGEANT, STR_CAPTAIN, STR_COLONEL, STR_COMMANDER };

// Bar::draw (scale 1) высотой h: рамка max + 1 цвета color + 4, внутри нулём, значение и поверх второе
static void sbar(int16_t y, uint8_t v, uint8_t v2, uint8_t max, uint8_t c, uint8_t c2)
{
	uint8_t w = max < 149 ? max : 149;
	gfx_fill(170, y, w + 1, 5, c + 4);
	gfx_fill(170, y + 1, w, 3, 0);
	if (v) gfx_fill(170, y + 1, v < w ? v : w, 3, c);
	if (v2) gfx_fill(170, y + 1, v2 < w ? v2 : w, 3, c2);
}

static uint8_t row;                      // строка: y = 38 + 9 * row
static uint8_t c_txt, c_num;

static void line(uint16_t name, uint8_t v, uint8_t max, uint8_t el, uint8_t v2)
{
	char n[6];
	int16_t y = 38 + 9 * (int16_t)row++;
	tbox_t tb = { 8, y, 140, 9, FNT_SMALL, c_txt, c_txt, TX_CONTRAST };
	text_draw(&tb, str_get(name));
	fmt_num(n, v, 0);
	tb.x = 150; tb.w = 18; tb.color = tb.color2 = c_num;
	text_draw(&tb, n);
	sbar(y + 1, v, v2, max, ui_color_in(UI_SCR_STATS, el, 0), ui_color_in(UI_SCR_STATS, el, 1));
}

void stats_draw(const ustat_t *s) __banked
{
	char name[48];
	c_txt = ui_color_in(UI_SCR_STATS, UI_EL_TEXT, 0);
	c_num = ui_color_in(UI_SCR_STATS, UI_EL_TEXT, 1);
	gfx_blit(RES_UNIBORD_PCK, 0, 0, 0, 0, 320, 200);
	name[0] = 0;
	if (s->sol != 0xFF) {                // солдат: звание и имя
		soldier_t so;
		far_read(FAR(SOLDIER_PAGE, 0) + (uint16_t)s->sol * sizeof so, &so, sizeof so);
		str_copy(rank_str[so.rank < 6 ? so.rank : 0], name, 24);
		strcat(name, " ");
		uint8_t l = (uint8_t)strlen(name);
		memcpy(name + l, so.name, SOLDIER_NAME);
		name[l + SOLDIER_NAME] = 0;
	} else if (s->type != 0xFFFF) {      // пришелец: название из units
		rtab_t t;
		if (rtab_open(RES_RULE_UNITS, &t) && s->type < t.n) str_copy(rtab_word(&t, s->type, 0), name, 40);
	}
	uint8_t cn = ui_color_in(UI_SCR_STATS, UI_EL_TEXTNAME, 0);
	tbox_t tb = { 16, 4, 288, 17, FNT_BIG, cn, cn, TX_CENTER | TX_CONTRAST };
	text_draw(&tb, name);
	row = 0;
	uint8_t hm = s->hp_max ? s->hp_max : 1;
	line(STR_TIME_UNITS, s->tu, s->tu_max, UI_EL_BARTUS, 0);
	line(STR_ENERGY, s->en, s->en_max, UI_EL_BARENERGY, 0);
	line(STR_HEALTH, s->hp, s->hp_max, UI_EL_BARHEALTH, s->stun);
	line(STR_FATAL_WOUNDS, 0, 0, UI_EL_BARWOUNDS, 0);            // ран пока нет
	line(STR_BRAVERY, s->bravery, s->bravery, UI_EL_BARBRAVERY, 0);
	line(STR_MORALE, s->mor, 100, UI_EL_BARMORALE, 0);
	line(STR_REACTIONS, s->reactions, s->reactions, UI_EL_BARREACTIONS, 0);
	line(STR_FIRING_ACCURACY, (uint8_t)((uint16_t)s->firing * s->hp / hm), s->firing, UI_EL_BARFIRING, 0);
	line(STR_THROWING_ACCURACY, (uint8_t)((uint16_t)s->throwing * s->hp / hm), s->throwing, UI_EL_BARTHROWING, 0);
	line(STR_MELEE_ACCURACY, (uint8_t)((uint16_t)s->melee * s->hp / hm), s->melee, UI_EL_BARMELEE, 0);
	line(STR_STRENGTH, s->strength, s->strength, UI_EL_BARSTRENGTH, 0);
	if (s->psi_skill) {
		line(STR_PSIONIC_STRENGTH, s->psi_str, s->psi_str, UI_EL_BARPSISTRENGTH, 0);
		line(STR_PSIONIC_SKILL, s->psi_skill, s->psi_skill, UI_EL_BARPSISKILL, 0);
	} else row += 2;                     // строки пси пустые, броня — на своих местах
	static const uint16_t an[5] = { STR_FRONT_ARMOR_UC, STR_LEFT_ARMOR_UC, STR_RIGHT_ARMOR_UC, STR_REAR_ARMOR_UC, STR_UNDER_ARMOR_UC };
	static const uint8_t ae[5] = { UI_EL_BARFRONTARMOR, UI_EL_BARLEFTARMOR, UI_EL_BARRIGHTARMOR, UI_EL_BARREARARMOR, UI_EL_BARUNDERARMOR };
	for (uint8_t i = 0; i < 5; i++) line(an[i], s->armor[i], s->armor[i], ae[i], 0);
}
