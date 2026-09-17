// Имена объектов для окон всех банков (банк 2; из банка 1 вынесены, когда он дорос до
// 16.2 КБ): корабль «TRITON-1», НЛО «ALIEN SUB-12», деньги «$1,234,567». Пишут только в
// буфер вызывающего (Win1 или стек).
#include <stdint.h>
#include <string.h>
#include "res.h"
#include "rules.h"
#include "text.h"
#include "state.h"
#include "game.h"
#include "scrdef.h"

void craft_name(uint8_t c, char *buf) __banked
{
	rtab_t t;
	rtab_open(RES_RULE_CRAFTS, &t);
	str_copy(rtab_word(&t, ST->craft[c].type, 0), buf, 40);
	strcat(buf, "-");
	fmt_num(buf + strlen(buf), ST->craft[c].num, 0);
}

// Ufo::getDefaultName: по состоянию — UFO-n, Landing Site-n, Crash Site-n
void ufo_name(uint8_t u, char *buf) __banked
{
	char n[8];
	ufo_t *p = &ST->ufo[u];
	uint16_t id = p->id, s = STR_UFO_;
	if (p->status == US_LANDED) { id = p->land_id; s = STR_LANDING_SITE_; }
	else if (p->status == US_CRASHED) { id = p->crash_id; s = STR_CRASH_SITE_; }
	fmt_num(n, id, 0);
	str_fmt(buf, str_get(s), n, "");
}

char *fmt_funds(char *buf, int32_t v) __banked
{
	char *p = buf;
	if (v < 0) { *p++ = '-'; v = -v; }
	*p++ = '$';
	fmt_num(p, v, ',');
	return buf;
}
