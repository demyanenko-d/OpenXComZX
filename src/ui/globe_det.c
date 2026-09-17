// Банк 26: детали глобуса — Globe::drawDetail OpenXcom (REF/OpenXcom/src/Geoscape/Globe.cpp): линии рек
// и границ (зум >= 1), подписи стран (>= 2), значки и подписи городов, подписи баз (>= 3). Рисуются
// после кадра прямо в задний буфер (строки экрана 280..479, globe.s): копия на экран уже с ними, и
// перерисовка меток поверх глобуса их не стирает. Данные — ресурс GLOBEDET (конвертер
// Core/GlobeDetail.cs, группы линий и подписей), отсев групп и проекция вершин — globe.s detail
// (в рабочую страницу: список взятых групп DT_VIS, проекции подряд с DT_V), точки и линии —
// globe_det_s.s. project_docs/globe.md §12.12.
#include <stdint.h>
#include <string.h>
#include "res.h"
#include "res_ids.h"
#include "far.h"
#include "text.h"
#include "pages.h"
#include "memmap.h"
#include "state.h"
#include "globe.h"
#include "rules.h"
#include <stddef.h>

#define DT_VIS    0x2540          // рабочая страница (globe.s): число взятых групп, их номера
#define DT_V      0x2C00          //   проекции их вершин подряд: x, y (1/4 точки: 512 / 400 — центр), z
#define G_MAX     96              // GlobeDetail.MaxGroup, MaxVert
#define V_MAX     340
#define DP_MAX    512             // globe.s DT_VMAX
#define BACK_Y    280

typedef struct {
	uint8_t col[4];               // линии, страны, города, базы
	uint8_t mark[9];              // значок города 3x3
	uint8_t pad;
	uint16_t ngroup, nvert, r0, r1, r2;
} det_hdr_t;

// Рабочая память — в хвосте страницы банка (окно 1 занято): globe_det_s.s знает адреса DP_PTR, DL_*
static det_hdr_t __at(0xA800) dh;
static uint8_t __at(0xA818) dp_ok;
static uint8_t __at(0xA820) grec[G_MAX * 16];   // записи групп: u16 первая вершина, u8 вершин, u8 вид …
static uint8_t __at(0xAE20) gstr[V_MAX * 2];    // строки подписей по вершинам
static uint8_t __at(0xB0D0) gvis[1 + G_MAX];    // взятые группы
static uint8_t __at(0xB140) dp[DP_MAX * 6];
static uint16_t __at(0xBFFA) dp_ptr;            // det_poly: вершины линии
static uint8_t __at(0xBFFC) dp_cnt;
static uint16_t __at(0xBFE0) base_sig;          // globe_det_check: подпись баз в заднем буфере
static uint16_t __at(0xBFE2) dl_yb;             // globe_det_s.s: строка экрана под y = 0 (280 — задний буфер)
// Метки (globe_marks): кадры GlobeMarkers, фаза мигания, позиции и кадры последнего вывода
#define MK_MAX    64
#define MK_OK     0xC3
static uint8_t __at(0xBD40) mk_spr[81];
static uint8_t __at(0xBD91) mk_ok;             // MK_OK — кадры и список годны (память банка при старте не обнулена)
static uint8_t __at(0xBD92) mk_blink;
static uint8_t __at(0xBD93) mk_n;
static uint8_t __at(0xBDA0) mk_list[MK_MAX * 3];
static int16_t __at(0xBFF0) dl_x0;
static int16_t __at(0xBFF2) dl_y0;
static int16_t __at(0xBFF4) dl_x1;
static int16_t __at(0xBFF6) dl_y1;
static uint8_t __at(0xBFF8) dl_c;

void det_line(void);              // globe_det_s.s: отрезок (dl_x0, dl_y0) — (dl_x1, dl_y1) цветом dl_c, концы в окне
void det_pset(void);              // точка (dl_x0, dl_y0), если в окне
void det_poly(void);              // globe_det_s.s: линия dp_cnt вершин с dp_ptr (отсечение — det_clip)
int16_t det_muldiv(int16_t a, int16_t b, int16_t c);   // globe_det_s.s: a · b / c к нулю

// SDL_gfx _clipLine (Коэн — Сазерленд по окну 0..255 x 0..199; Surface::drawLine -> lineColor):
// 1 — есть что рисовать, концы в dl_*. Наклон SDL_gfx — float, здесь то же целочисленным делением
// с отбрасыванием дробной части (приведение (Sint16) — к нулю).
#define CL_L 1
#define CL_R 2
#define CL_B 4
#define CL_T 8
static uint8_t code(int16_t x, int16_t y)
{
	uint8_t c = 0;
	if (x < 0) c = CL_L; else if (x > 255) c = CL_R;
	if (y < 0) c |= CL_T; else if (y > 199) c |= CL_B;
	return c;
}
uint8_t det_clip(void)
{
	for (;;) {
		uint8_t c1 = code(dl_x0, dl_y0), c2 = code(dl_x1, dl_y1);
		if (!(c1 | c2)) return 1;
		if (c1 & c2) return 0;
		if (!c1) {                                // первый конец — снаружи
			int16_t t = dl_x0; dl_x0 = dl_x1; dl_x1 = t;
			t = dl_y0; dl_y0 = dl_y1; dl_y1 = t;
			c1 = c2;
		}
		int16_t dx = dl_x1 - dl_x0, dy = dl_y1 - dl_y0;
		if (c1 & (CL_L | CL_R)) {
			int16_t e = c1 & CL_L ? 0 : 255;
			if (dx) dl_y0 += det_muldiv(e - dl_x0, dy, dx);
			else dl_y0 += e - dl_x0;              // m = 1.0f
			dl_x0 = e;
		} else {
			int16_t e = c1 & CL_B ? 199 : 0;
			if (dx) dl_x0 += dy ? det_muldiv(e - dl_y0, dx, dy) : 0;
			dl_y0 = e;
		}
	}
}

#define px(p) ((int16_t)((p)[0] | (p)[1] << 8) >> 2)
#define py(p) ((int16_t)((p)[2] | (p)[3] << 8) >> 2)

#define FRONT(p) (((p)[5] & 0x80) == 0)   // Globe::pointBack: z < 0 — сзади

// Рамка подписи 100 x 9 с (x − 50, y) целиком вне окна 256 x 200 — не выводить (разметка строки стоит
// дороже всего остального)
static uint8_t off_win(int16_t x, int16_t y) { return x <= -50 || x >= 306 || y <= -9 || y >= 200; }

static void label(int16_t x, int16_t y, uint8_t color, const char *s)
{
	tbox_t b;
	b.x = x - 50; b.y = y; b.w = 100; b.h = 9;
	b.font = FNT_SMALL; b.color = color; b.color2 = color; b.flags = TX_CENTER;
	text_draw(&b, s);
}

// fresh — globe.s только что отобрал группы и спроецировал вершины для этого вида (иначе — прошлые)
void globe_det(uint8_t fresh, uint8_t z, uint8_t work) __banked
{
	res_t r;
	uint16_t n;
	uint8_t i, k, *p;
	if (fresh) {
		dp_ok = 0;
		if (!res_find(RES_GLOBEDET, &r)) return;
		far_read(r.phys, &dh, sizeof dh);
		if (dh.ngroup > G_MAX || dh.nvert > V_MAX) return;
		far_read(r.phys + sizeof dh, grec, dh.ngroup * 16);
		far_read(r.phys + sizeof dh + dh.ngroup * 28, gstr, dh.nvert * 2);
		far_read(FAR(work, DT_VIS), gvis, sizeof gvis);
		if (gvis[0] > G_MAX) return;
		for (n = 0, i = 0; i < gvis[0]; i++) n += grec[(gvis[i + 1] & 0x7F) * 16 + 2];
		if (n > DP_MAX) return;
		far_read(FAR(work, DT_V), dp, n * 6);
		dp_ok = 1;
	}
	if (!dp_ok || !z) return;
	uint8_t old3 = pg_win3();
	tx_yb = BACK_Y;
	dl_yb = BACK_Y;
	p = dp;
	for (i = 0; i < gvis[0]; i++) {
		const uint8_t *g = grec + (gvis[i + 1] & 0x7F) * 16;
		uint8_t m = g[2];
		const uint8_t *s = gstr + (g[0] | g[1] << 8) * 2;
		if (g[3] == 0) {                        // линия: оба конца спереди, отсечение по окну
			dl_c = dh.col[0];
			dp_ptr = (uint16_t)p; dp_cnt = m;
			det_poly();
		} else {                                // подписи стран; города — ещё значок, подпись ниже на 2
			const uint8_t *q = p;
			for (k = 0; k < m; k++, q += 6, s += 2) {
				if (!FRONT(q)) continue;
				int16_t x = px(q), y = py(q);
				if (g[3] == 1) {
					if (!off_win(x, y)) label(x, y, dh.col[1], str_get(s[0] | s[1] << 8));
					continue;
				}
				if (off_win(x, y + 2)) continue;
				const uint8_t *mk = dh.mark;       // Globe::drawTarget: значок 3x3 по центру
				for (int16_t my = y - 1; my <= y + 1; my++)
					for (int16_t mx = x - 1; mx <= x + 1; mx++, mk++)
						if (*mk) { dl_x0 = mx; dl_y0 = my; dl_c = *mk; det_pset(); }
				label(x, y + 2, dh.col[2], str_get(s[0] | s[1] << 8));
			}
		}
		p += m * 6;
	}
	if (z >= 3) {
		char name[NAME_LEN];                    // подписи баз
		for (k = 0; k < MAX_BASES; k++) {
			int16_t x, y;
			pg_map3(STATE_PAGE);
			if (!ST->base[k].name[0] || !globe_xy(&ST->base[k].pos, &x, &y) || off_win(x, y + 2)) continue;
			memcpy(name, ST->base[k].name, NAME_LEN);
			label(x, y + 2, dh.col[3], name);
		}
	}
	tx_yb = 0;
	pg_map3(old3);
}

// Подписи баз лежат в заднем буфере: база построена, переименована или закрыта без смены вида — глобус
// заново (scr_geo.c перед globe_draw; ST — в Win3)
void globe_det_check(void) __banked
{
	uint16_t h = 0;
	for (uint8_t k = 0; k < MAX_BASES; k++) {
		const base_t *b = &ST->base[k];
		if (!b->name[0]) continue;
		h = (h << 5 | h >> 11) ^ (k + 1) ^ (uint16_t)b->pos.lon ^ (uint16_t)b->pos.lat;
		for (uint8_t i = 0; i < NAME_LEN && b->name[i]; i++) h = (h << 3 | h >> 13) ^ (uint8_t)b->name[i];
	}
	if (h != base_sig) { base_sig = h; globe_invalidate(); }
}

// ---------------------------------------------------------------- метки (Globe::drawMarkers, blink)

static void mk_add(const geo_t *pos, int8_t fr)
{
	int16_t x, y;
	if (fr < 0 || fr > 8 || mk_n >= MK_MAX || !globe_xy(pos, &x, &y)) return;
	uint8_t *e = mk_list + mk_n * 3;
	e[0] = (uint8_t)x; e[1] = (uint8_t)y; e[2] = (uint8_t)fr;
	mk_n++;
}

// Точки меток на экране (не в заднем буфере): кадр 3x3 по центру, цвет + фаза мигания (кроме города)
static void mk_draw(void)
{
	uint8_t old3 = pg_win3();
	dl_yb = 0;
	for (uint8_t i = 0; i < mk_n; i++) {
		const uint8_t *e = mk_list + i * 3, *sp = mk_spr + e[2] * 9;
		uint8_t add = e[2] == 8 ? 0 : mk_blink;
		for (int16_t my = e[1] - 1; my <= e[1] + 1; my++)
			for (int16_t mx = e[0] - 1; mx <= e[0] + 1; mx++, sp++)
				if (*sp) { dl_x0 = mx; dl_y0 = my; dl_c = *sp + add; det_pset(); }
	}
	pg_map3(old3);
}

static int8_t rule_i8(uint16_t id, uint16_t i, uint8_t off)
{
	rtab_t t;
	rtab_open(id, &t);
	return (int8_t)rtab_word(&t, i, off);
}

// Метки поверх глобуса после копии заднего буфера (scr_geo.c draw_globe; ST — в Win3): базы, путевые
// точки, места миссий, базы пришельцев, НЛО, корабли в полёте — порядок Globe::drawMarkers
void globe_marks(void) __banked
{
	uint8_t k;
	res_t r;
	det_hdr_t h;
	mk_ok = 0;
	if (!res_find(RES_GLOBEDET, &r)) return;
	far_read(r.phys, &h, sizeof h);
	far_read(r.phys + h.r0, mk_spr, sizeof mk_spr);
	mk_n = 0;
	for (k = 0; k < MAX_BASES; k++)
		if (ST->base[k].name[0]) mk_add(&ST->base[k].pos, 0);
	for (k = 0; k < MAX_WAYPOINTS; k++)
		if (ST->waypoint[k].id) mk_add(&ST->waypoint[k].pos, 6);
	for (k = 0; k < MAX_SITES; k++)
		if (ST->site[k].id && (ST->site[k].flags & SITE_DETECTED)) {
			int8_t m = rule_i8(RES_RULE_ALIENDEPLOYMENTS, ST->site[k].deployment, offsetof(r_alienDeployments_t, marker_icon));
			mk_add(&ST->site[k].pos, m < 0 ? 5 : m);
		}
	for (k = 0; k < MAX_ALIEN_BASES; k++)
		if (ST->abase[k].id && (ST->abase[k].flags & AB_DISCOVERED))
			mk_add(&ST->abase[k].pos, rule_i8(RES_RULE_ALIENDEPLOYMENTS, ST->abase[k].deployment, offsetof(r_alienDeployments_t, marker_icon)));
	for (k = 0; k < MAX_UFOS; k++) {
		const ufo_t *u = &ST->ufo[k];
		if (u->type == NONE8 || !(u->flags & UF_DETECTED)) continue;
		uint8_t off = offsetof(r_ufos_t, marker), def = 2;
		if (u->status == US_LANDED) { off = offsetof(r_ufos_t, marker_land); def = 3; }
		else if (u->status == US_CRASHED) { off = offsetof(r_ufos_t, marker_crash); def = 4; }
		int8_t m = rule_i8(RES_RULE_UFOS, u->type, off);
		mk_add(&u->pos, m < 0 ? def : m);
	}
	for (k = 0; k < MAX_CRAFTS; k++)
		if (ST->craft[k].type != NONE8 && ST->craft[k].status == CS_OUT) {
			int8_t m = rule_i8(RES_RULE_CRAFTS, ST->craft[k].type, offsetof(r_crafts_t, marker));
			mk_add(&ST->craft[k].pos, m < 0 ? 1 : m);
		}
	mk_ok = MK_OK;
	mk_draw();
}

// Globe::blink (раз в 100 мс): фаза мигания и точки меток заново (без копии глобуса)
void globe_blink(void) __banked
{
	if (mk_ok != MK_OK) return;
	mk_blink ^= 1;
	mk_draw();
}
