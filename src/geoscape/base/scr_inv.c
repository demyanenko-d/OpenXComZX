// Экран инвентаря бойца (InventoryState OpenXcom, REF/OpenXcom/src/Battlescape/InventoryState.cpp):
// кукла в броне, предметы по секциям (правила invs: руки, пояс, рюкзак, плечи, ноги) и «земля» —
// груз корабля. Рисуется поверх фона TAC01.SCR палитрой боя; спрайты предметов — BIGOBS.PCK
// (32x48 = 2x3 клетки по 16). Записи инвентаря — общий пул в состоянии (state.h, inv_t).
//
// Перекладывание — как Inventory::mouseClick: первый клик берёт предмет (он идёт за курсором),
// второй кладёт; правая кнопка возвращает взятый на место, «unload» разряжает взятое оружие
// (обойма — в левую руку, оружие — в правую). Экран открывается из снаряжения корабля
// («Inventory»), поэтому боец берётся из экипажа: стрелки листают экипаж.
//
// Перерисовка частичная: клик перерисовывает только затронутые секции (part), несомый предмет
// ходит за курсором поверх экрана — точки под ним сохраняются и возвращаются (carry_io). Полный
// вывод (кукла, подписи секций) — только при открытии и смене бойца: он идёт ~20 кадров.
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "res.h"
#include "res_ids.h"
#include "rules.h"
#include "ui.h"
#include "text.h"
#include "gfx.h"
#include "state.h"
#include "game.h"
#include "scrdef.h"
#include "input.h"
#include "far.h"
#include "pages.h"

#define SLOT_W    16
#define SLOT_H    16
#define HAND_W    2
#define HAND_H    3
#define INV_GROUND  0                // порядок записей invs: земля первой (inventories.rul)
#define GL_COLS     64               // столбцов земли за всеми страницами (типов груза — до CARGO_ITEMS)
#define AMMO_X      272              // картинка обоймы под курсором (_selAmmo, InventoryState.cpp:99)
#define AMMO_Y      88
#define SEC_MAX     12               // секций invs (у UFO и TFTD — по 9)
#define SV_MAX      32               // предметов у бойца (клеток во всех секциях меньше)
#define P_SEC(i)    (1u << (i))      // part: перерисовать секцию i
#define P_ALL       0x0FFF
#define P_CARRY     0x8000           // part: только несомый предмет

typedef struct {                     // предмет бойца: копия записи пула
	uint16_t rec;
	inv_t v;
	uint8_t w, h;                    // размер в клетках
} sv_t;

// Память банка за кодом (build.ps1: ram банка 27 — с #B000)
static uint8_t __at(0xB000) carry_buf[HAND_W * SLOT_W * HAND_H * SLOT_H];   // точки под несомым
static r_invs_t __at(0xB600) secs[SEC_MAX];          // правила секций (читаются при открытии)
static sv_t __at(0xB700) sv[SV_MAX];                 // предметы бойца
static uint8_t __at(0xB900) gl_wh[CARGO_ITEMS];      // размер вида груза на земле: w | h << 4

static char t1[40], t2[8];
static uint16_t hover_item;          // предмет под курсором (подпись внизу)
static uint16_t hover_ammo;          // его обойма (оружие бойца) — картинка и «патронов осталось»
static uint8_t hover_rounds;
static uint8_t sel_sol;              // боец: запись в state (soldier_nth по экипажу)
static uint8_t crew_i;               // его номер в экипаже корабля
static uint8_t nsec, nsv;
static uint8_t c_grid, c_slots, c_num;   // цвета элементов inventory (interfaces.rul)
static uint16_t part;                // что перерисовать в EVT_DRAW; 0 — всё (перерисовка от ui)
static uint16_t carry;               // взятая запись инвентаря (INV_NONE — ничего)
static uint16_t carry_item;          // её предмет и размер — для вывода у курсора
static uint8_t carry_w, carry_h;
static uint8_t carry_on;             // 1 — несомый на экране, точки под ним в carry_buf
static int16_t carry_x, carry_y;     // курсор, у которого он нарисован
static int16_t carry_sx, carry_sy;   // его кадр 32x48 на экране
static uint8_t ground_x;             // прокрутка «земли»: первый показанный столбец
// Земля (Inventory::arrangeGround): клетка каждого вида груза, размеры земли в клетках
static uint8_t gl_x[CARGO_ITEMS], gl_y[CARGO_ITEMS];   // #FF — не лёг (пусто)
static uint8_t gl_sx, gl_sy, gl_xmax;

static const wdef_t w_inv[] = {
	IMG(0, 0, 320, 200, RES_TAC01_SCR),
	CUSR(0, 0, 320, 200, DYN(9), A_CUSTOM, 1),     // кукла, предметы, земля (рисует экран)
	// Цвета — элементы inventory в interfaces.rul (у UFO и TFTD свои); вес — явный: {ALT}-часть
	// золотая или красная при перегрузе (InventoryState::updateStats)
	TXT(28, 6, 210, 17, UI_EL_TEXTNAME, DYN(0), BIG),
	TXT(245, 24, 70, 9, EL_RAW + 1, DYN(1), 0),    // вес
	TXT(128, 140, 160, 9, UI_EL_TEXTITEM, DYN(2), 0),   // предмет под курсором
	TXT(245, 32, 50, 9, UI_EL_TEXTFIRING, DYN(3), 0),   // меткость
	TXT(245, 40, 50, 9, UI_EL_TEXTREACTION, DYN(4), 0), // реакции
	TXT(245, 48, 50, 9, UI_EL_TEXTPSISKILL, DYN(5), 0), // пси-навык
	TXT(245, 56, 50, 9, UI_EL_TEXTPSISTRENGTH, DYN(6), 0),   // пси-сила
	TXT(254, 64, 66, 24, UI_EL_TEXTAMMO, DYN(7), 0),    // патронов в оружии под курсором
	HOT(237, 1, 35, 22, A_CUSTOM, 6, ESC),         // OK
	HOT(273, 1, 23, 22, A_CUSTOM, 2, 0),           // предыдущий боец
	HOT(297, 1, 23, 22, A_CUSTOM, 3, 0),           // следующий
	HOT(288, 32, 32, 25, A_CUSTOM, 4, 0),          // разрядить взятое оружие (unload)
	HOT(289, 137, 32, 15, A_CUSTOM, 5, 0),         // следующая страница «земли»
};

static const scr_t tab[] = {
	SCR(SCR_INVENTORY, UI_SCR_INVENTORY, RES_PAL_BATTLESCAPE, RES_TAC01_SCR, SF_RAWPAL, w_inv),
};

static uint16_t rec_name_items(uint16_t item)
{
	rtab_t t;
	rtab_open(RES_RULE_ITEMS, &t);
	return item < t.n ? rtab_word(&t, item, 0) : 0xFFFF;
}

static uint8_t item_big(uint16_t item, int16_t *w, int16_t *h)
{
	rtab_t t;
	r_items_t it;
	rtab_open(RES_RULE_ITEMS, &t);
	if (item >= t.n) return 0;
	rtab_get(&t, item, &it);
	*w = it.inv_width ? it.inv_width : 1;
	*h = it.inv_height ? it.inv_height : 1;
	return it.big_sprite >= 0 ? (uint8_t)it.big_sprite : 0xFF;
}

// --- правила секций и предметы бойца

static void secs_load(void)
{
	rtab_t t;
	rtab_open(RES_RULE_INVS, &t);
	nsec = t.n > SEC_MAX ? SEC_MAX : (uint8_t)t.n;
	for (uint8_t i = 0; i < nsec; i++) rtab_get(&t, i, &secs[i]);
}

static uint8_t sec_named(uint16_t name)
{
	for (uint8_t i = 0; i < nsec; i++) if (secs[i].name == name) return i;
	return 0xFF;
}

// Копия записей бойца: по ней рисуются секции и ищется предмет под курсором (пул — до 1024
// записей, перебирать его каждый кадр дорого). Перечитывается после каждого изменения
static void sol_load(void)
{
	inv_t v;
	int16_t w, h;
	nsv = 0;
	if (sel_sol == NONE8) return;
	for (uint16_t i = 0; i < ST->ninv && nsv < SV_MAX; i++) {
		inv_get(i, &v);
		if (v.soldier != sel_sol || v.slot >= nsec) continue;
		sv_t *s = &sv[nsv++];
		item_big(v.item, &w, &h);
		s->rec = i;
		s->v = v;
		s->w = (uint8_t)w;
		s->h = (uint8_t)h;
	}
}

// Предмет бойца в клетке секции (номер в sv или #FF); взятый не считается — его место свободно
static uint8_t item_at(uint8_t slot, uint8_t x, uint8_t y)
{
	for (uint8_t k = 0; k < nsv; k++) {
		const sv_t *s = &sv[k];
		if (s->v.slot != slot || s->rec == carry) continue;
		if (secs[slot].type == 1 || (x >= s->v.x && x < s->v.x + s->w && y >= s->v.y && y < s->v.y + s->h))
			return k;
	}
	return 0xFF;
}

// Ляжет ли взятый предмет в секцию с клетки (x, y) (RuleInventory::fitItemInSlot и overlapItems):
// в руку — если она пуста; в сетку — все его клетки есть в маске и свободны
static uint8_t fits(uint8_t slot, uint8_t x, uint8_t y)
{
	const r_invs_t *r = &secs[slot];
	if (r->type == 1) return item_at(slot, 0, 0) == 0xFF;
	for (uint8_t j = 0; j < carry_h; j++)
		for (uint8_t i = 0; i < carry_w; i++) {
			uint8_t cx = x + i, cy = y + j;
			if (cx >= r->cols || cy >= r->rows || !(r->mask & (1u << (cy * 4 + cx)))) return 0;
			if (item_at(slot, cx, cy) != 0xFF) return 0;
		}
	return 1;
}

// Секция и клетка под точкой экрана (RuleInventory::checkSlotInPosition): рука — вся рамка 2x3,
// земля — до края экрана, сетка — только клетки маски; #FF — мимо
static uint8_t hit_slot(int16_t px, int16_t py, uint8_t *cx, uint8_t *cy)
{
	for (uint8_t i = 0; i < nsec; i++) {
		const r_invs_t *r = &secs[i];
		int16_t dx = px - (int16_t)r->x, dy = py - (int16_t)r->y;
		if (dx < 0 || dy < 0) continue;
		uint8_t x = (uint8_t)((uint16_t)dx / SLOT_W), y = (uint8_t)((uint16_t)dy / SLOT_H);
		if (r->type == 1) {
			if (dx >= HAND_W * SLOT_W || dy >= HAND_H * SLOT_H) continue;
			x = y = 0;
		} else if (r->type == 2) {
			if (px >= 320 || py >= 200) continue;
		} else if (x >= r->cols || y >= r->rows || !(r->mask & (1u << (y * 4 + x)))) continue;
		*cx = x;
		*cy = y;
		return i;
	}
	return 0xFF;
}

// --- сетка

// Клетка как в Inventory::drawGrid: drawRect цветом сетки, внутри — drawRect цветом in (сетка — 0:
// так клетки и стирают прежние предметы). Низ экрана отсекается (у земли последний ряд — неполный)
static void grid_cell(int16_t x, int16_t y, int16_t w, int16_t h, uint8_t c, uint8_t in)
{
	int16_t vis = y + h <= 200 ? h : 200 - y;
	if (vis <= 0) return;
	gfx_fill(x, y, w, vis, c);
	int16_t ih = h - 2 < vis - 1 ? h - 2 : vis - 1;
	if (w > 2 && ih > 0) gfx_fill(x + 1, y + 1, w - 2, ih, in);
}

// Сетка секций из m; labels — и подписи над ними (только при полном выводе: клетки их не задевают)
static void draw_grid(uint16_t m, uint8_t labels)
{
	tbox_t b;
	b.w = 80; b.h = 9; b.font = FNT_SMALL; b.flags = TX_CONTRAST;
	b.color = b.color2 = c_slots;
	for (uint8_t i = 0; i < nsec; i++) {
		if (!(m & P_SEC(i))) continue;
		const r_invs_t *r = &secs[i];
		if (r->type == 1) {                      // рука: одна рамка 2x3 клетки
			grid_cell((int16_t)r->x, (int16_t)r->y, HAND_W * SLOT_W, HAND_H * SLOT_H, c_grid, 0);
		} else if (r->type == 2) {               // земля: клетки до края и до низа экрана — те же
			int16_t gx = (int16_t)r->x, gy = (int16_t)r->y;   // рамки линиями по чёрному полю
			gfx_fill(gx, gy, 320 - gx, 200 - gy, 0);
			for (int16_t y = gy; y < 200; y += SLOT_H) gfx_fill(gx, y, 320 - gx, 1, c_grid);
			for (int16_t x = gx; x < 320; x += SLOT_W) gfx_fill(x, gy, 1, 200 - gy, c_grid);
		} else {                                 // сетка: клетки по маске
			for (uint8_t y = 0; y < r->rows; y++)
				for (uint8_t x = 0; x < r->cols; x++)
					if (r->mask & (1u << (y * 4 + x)))
						grid_cell((int16_t)r->x + x * SLOT_W, (int16_t)r->y + y * SLOT_H,
							  SLOT_W + 1, SLOT_H + 1, c_grid, 0);
		}
		if (!labels) continue;
		// Подпись секции над ней (высота мелкого шрифта с интервалом), высокий контраст
		b.x = (int16_t)r->x;
		b.y = (int16_t)r->y - font_height(FNT_SMALL);
		uint8_t old3 = pg_win3();                // вывод текста оставляет в Win3 страницу экрана, а ST — там
		text_draw(&b, str_get(r->name));
		pg_map3(old3);
	}
}

// --- земля: груз корабля (Inventory::arrangeGround)

// Виды груза в порядке std::map по id предмета (ItemContainer оригинала — так они и ложатся на
// землю), каждый — в самую левую верхнюю клетку, где он помещается: сверху вниз, потом вправо, не
// переходя границу страницы земли. Одинаковые предметы — стопкой в одной клетке.
static void ground_arrange(void)
{
	rtab_t t;
	uint8_t ord[CARGO_ITEMS], n = 0, occ[GL_COLS];
	const r_invs_t *r = &secs[INV_GROUND];
	gl_sx = (uint8_t)((320 - (int16_t)r->x) / SLOT_W);
	gl_sy = (uint8_t)((200 - (int16_t)r->y) / SLOT_H);
	gl_xmax = 0;
	memset(gl_x, 0xFF, sizeof gl_x);
	memset(occ, 0, sizeof occ);
	craft_t *cr = &ST->craft[ctx.craft];
	if (cr->cargo == NONE8 || !gl_sx || !gl_sy) return;
	rtab_open(RES_RULE_ITEMS, &t);
	for (uint8_t i = 0; i < CARGO_ITEMS; i++) {  // вставкой по месту id
		uint8_t it = ST->cargo[cr->cargo].it[i].item;
		if (it == NONE8 || !ST->cargo[cr->cargo].it[i].qty) continue;
		uint16_t rk = rtab_word(&t, it, offsetof(r_items_t, sort_rank));
		uint8_t k = n++;
		while (k && rtab_word(&t, ST->cargo[cr->cargo].it[ord[k - 1]].item, offsetof(r_items_t, sort_rank)) > rk) {
			ord[k] = ord[k - 1];
			k--;
		}
		ord[k] = i;
	}
	for (uint8_t j = 0; j < n; j++) {
		uint8_t ci = ord[j];
		int16_t w, h;
		item_big(ST->cargo[cr->cargo].it[ci].item, &w, &h);
		gl_wh[ci] = (uint8_t)(w | (h << 4));
		if (w > gl_sx || h > gl_sy) continue;
		uint8_t x = 0, y = 0, rows = (uint8_t)(((1u << h) - 1) & 0xFF);
		for (;;) {
			uint8_t ok = x + w <= GL_COLS && (x % gl_sx) + w <= gl_sx;   // не через границу страницы
			for (uint8_t d = 0; ok && d < (uint8_t)w; d++) ok = !(occ[x + d] & (rows << y));
			if (ok) break;
			if (++y > gl_sy - h) { y = 0; if (++x >= GL_COLS) break; }
		}
		if (x >= GL_COLS) continue;
		for (uint8_t d = 0; d < (uint8_t)w; d++) occ[x + d] |= (uint8_t)(rows << y);
		gl_x[ci] = x;
		gl_y[ci] = y;
		if (x + w > gl_xmax) gl_xmax = (uint8_t)(x + w);
	}
}

// Вид груза в клетке земли (cx, cy — клетка на экране; прокрутка учтена); NONE8 — пусто.
// Взятый последний из стопки оставляет пустое место до следующей раскладки — как в оригинале
static uint8_t ground_item(uint8_t cx, uint8_t cy, uint8_t *slot_out)
{
	craft_t *cr = &ST->craft[ctx.craft];
	if (cr->cargo == NONE8) return NONE8;
	uint8_t col = cx + ground_x;
	for (uint8_t i = 0; i < CARGO_ITEMS; i++) {
		uint8_t gx = gl_x[i], gy = gl_y[i], it = ST->cargo[cr->cargo].it[i].item;
		if (gx == 0xFF || it == NONE8) continue;
		if (col < gx || col >= gx + (gl_wh[i] & 15) || cy < gy || cy >= gy + (gl_wh[i] >> 4)) continue;
		if (slot_out) *slot_out = i;
		return it;
	}
	return NONE8;
}

static void ground_add(uint8_t item)
{
	craft_t *cr = &ST->craft[ctx.craft];
	if (cr->cargo == NONE8) return;
	for (uint8_t i = 0; i < CARGO_ITEMS; i++)
		if (ST->cargo[cr->cargo].it[i].item == item) { ST->cargo[cr->cargo].it[i].qty++; return; }
	for (uint8_t i = 0; i < CARGO_ITEMS; i++)
		if (ST->cargo[cr->cargo].it[i].item == NONE8) {
			ST->cargo[cr->cargo].it[i].item = item;
			ST->cargo[cr->cargo].it[i].qty = 1;
			return;
		}
}

// NumberText с обводкой (Interface/NumberText.cpp): цифра 3x5 в клетке 5x7 — углы на 12, стороны на
// 9 светлее… темнее цвета (блок палитры), сама цифра — цвет + 1; цифры через 4 точки. Строка клетки
// — маски слоёв (бит 4 — левая точка) из строк цифры со сдвигами; одна страница экрана на строку
static const uint8_t num_glyph[10][7] = {        // строки 3x5 и по пустой сверху и снизу (сдвиги слоёв)
	{0,7,5,5,5,7,0}, {0,2,6,2,2,7,0}, {0,7,1,7,4,7,0}, {0,7,1,7,1,7,0}, {0,5,5,7,1,1,0},
	{0,7,4,7,1,7,0}, {0,7,4,7,5,7,0}, {0,7,1,1,1,1,0}, {0,7,5,7,5,7,0}, {0,7,5,7,1,7,0},
};

static void num_text(int16_t x, int16_t y, uint8_t v, uint8_t c)
{
	char s[4];
	fmt_num(s, v, 0);
	uint8_t n = (uint8_t)strlen(s);
	if (x < 0 || x + n * 4 + 1 > 320) return;
	uint8_t old = pg_win3();
	for (uint8_t r = 0; r < 7; r++) {
		int16_t py = y + r;
		if (py < 0 || py >= 200) continue;
		uint8_t *p = gfx_map(x, py);
		for (uint8_t i = 0; i < n; i++, p += 4) {
			const uint8_t *g = num_glyph[s[i] - '0'];
			uint8_t g0 = r < 6 ? g[r + 1] : 0, g1 = g[r], g2 = r ? g[r - 1] : 0;   // цифра со сдвигом 0, 1, 2 вниз
			uint8_t dig = g1 << 1;
			uint8_t side = (g1 << 2) | g1 | (g0 << 1) | (g2 << 1);
			uint8_t corner = (g0 << 2) | g0 | (g2 << 2) | g2;
			for (uint8_t k = 0, b = 16; k < 5; k++, b >>= 1)
				if (dig & b) p[k] = c + 1;
				else if (side & b) p[k] = c + 9;
				else if (corner & b) p[k] = c + 12;
		}
	}
	pg_map3(old);
}

// --- вывод

// Спрайт предмета в клетке секции (кадр BIGOBS — 32x48, рисуется как есть)
static void draw_item(uint16_t item, int16_t x, int16_t y)
{
	int16_t w, h;
	uint8_t sp = item_big(item, &w, &h);
	if (sp == 0xFF) return;
	gfx_sprite(RES_BIGOBS_PCK, sp, x, y);
}

static void draw_soldier(void)
{
	soldier_t so;
	rtab_t t;
	r_armors_t ar;
	if (sel_sol == NONE8) return;           // экипажа нет: soldier_get(255) прочёл бы чужое
	soldier_get(sel_sol, &so);
	if (so.armor == NONE8) return;
	rtab_open(RES_RULE_ARMORS, &t);
	rtab_get(&t, so.armor, &ar);
	if (!ar.inv_image) return;
	gfx_key = 1;                                 // кукла — картинка 320x200 с прозрачным фоном
	gfx_blit(ar.inv_image, 0, 0, 0, 0, 320, 200);
	gfx_key = 0;
}

// Предметы бойца в секциях из m и груз корабля на «земле» (Inventory::drawItems): в руке предмет —
// по центру рамки 2x3; на земле — по раскладке, у стопки поверх всех предметов её число (numStack)
static void draw_items(uint16_t m)
{
	for (uint8_t k = 0; k < nsv; k++) {
		const sv_t *s = &sv[k];
		const r_invs_t *r = &secs[s->v.slot];
		if (s->rec == carry || !(m & P_SEC(s->v.slot)) || r->type == 2) continue;
		int16_t x = (int16_t)r->x + s->v.x * SLOT_W, y = (int16_t)r->y + s->v.y * SLOT_H;
		if (r->type == 1) {
			x += (HAND_W - s->w) * SLOT_W / 2;
			y += (HAND_H - s->h) * SLOT_H / 2;
		}
		draw_item(s->v.item, x, y);
	}
	craft_t *cr = &ST->craft[ctx.craft];
	if (!(m & P_SEC(INV_GROUND)) || cr->cargo == NONE8) return;
	const r_invs_t *r = &secs[INV_GROUND];
	for (uint8_t pass = 0; pass < 2; pass++)
		for (uint8_t i = 0; i < CARGO_ITEMS; i++) {
			uint8_t gx = gl_x[i], it = ST->cargo[cr->cargo].it[i].item, q = ST->cargo[cr->cargo].it[i].qty;
			if (gx == 0xFF || it == NONE8 || gx < ground_x || gx - ground_x >= gl_sx) continue;
			int16_t x = (int16_t)r->x + (int16_t)(gx - ground_x) * SLOT_W, y = (int16_t)r->y + (int16_t)gl_y[i] * SLOT_H;
			if (!pass) { draw_item(it, x, y); continue; }
			if (q < 2) continue;
			num_text(x + (gl_wh[i] & 15) * SLOT_W - (q > 9 ? 8 : 4), y + (gl_wh[i] >> 4) * SLOT_H - 6, q, c_num);
		}
}

// Обойма оружия под курсором (InventoryState::invMouseOver): рамка 2x3 клетки цветом сетки, внутри
// цвет 15, обойма по центру; нет — место пустое (там фон TAC01 чёрный)
static void draw_ammo(void)
{
	if (hover_ammo == NONE16) { gfx_fill(AMMO_X, AMMO_Y, HAND_W * SLOT_W, HAND_H * SLOT_H, 0); return; }
	int16_t w, h;
	grid_cell(AMMO_X, AMMO_Y, HAND_W * SLOT_W, HAND_H * SLOT_H, c_grid, 15);
	item_big(hover_ammo, &w, &h);
	draw_item(hover_ammo, AMMO_X + (HAND_W - w) * SLOT_W / 2, AMMO_Y + (HAND_H - h) * SLOT_H / 2);
}

// --- несомый предмет: рисуется поверх всего по центру рамки 2x3 у курсора (Inventory::_selection,
// RuleItem::drawHandSprite); точки под ним — в памяти банка, перед изменениями экрана они
// возвращаются: иначе предмет оставлял след на кукле, сетке и надписях

static void carry_io(uint8_t save)
{
	uint8_t old = pg_win3(), *b = carry_buf;
	for (int16_t r = 0; r < HAND_H * SLOT_H; r++, b += HAND_W * SLOT_W) {
		int16_t y = carry_sy + r, x0 = carry_sx, n = HAND_W * SLOT_W, o = 0;
		if (y < 0 || y >= 200) continue;
		if (x0 < 0) { o = -x0; n += x0; x0 = 0; }
		if (x0 + n > 320) n = 320 - x0;
		if (n <= 0) continue;
		uint8_t *p = gfx_map(x0, y);
		if (save) memcpy(b + o, p, (uint16_t)n); else memcpy(p, b + o, (uint16_t)n);
	}
	pg_map3(old);
}

static void carry_hide(void)
{
	if (!carry_on) return;
	carry_io(0);
	carry_on = 0;
}

static void carry_show(void)
{
	if (carry == INV_NONE) return;
	carry_x = cursor_x;
	carry_y = cursor_y;
	carry_sx = cursor_x - HAND_W * SLOT_W / 2 + (HAND_W - carry_w) * SLOT_W / 2;
	carry_sy = cursor_y - HAND_H * SLOT_H / 2 + (HAND_H - carry_h) * SLOT_H / 2;
	carry_io(1);
	carry_on = 1;
	draw_item(carry_item, carry_sx, carry_sy);
}

static void carry_set(uint16_t rec, uint16_t item)
{
	int16_t w, h;
	item_big(item, &w, &h);
	carry = rec;
	carry_item = item;
	carry_w = (uint8_t)w;
	carry_h = (uint8_t)h;
}

// --- перекладывание: груз корабля («земля») <-> слоты бойца

// Совместима ли обойма с оружием (items.compatibleAmmo)
static uint8_t ammo_fits(uint16_t weapon, uint16_t ammo)
{
	rtab_t t;
	rlist_t l;
	uint16_t v[8];
	rtab_open(RES_RULE_ITEMS, &t);
	far_read(t.base + 8 + (uint32_t)weapon * t.size + offsetof(r_items_t, compatible_ammo), &l, sizeof l);
	uint8_t n = l.n > 8 ? 8 : l.n;
	if (!n) return 0;
	rtab_tail(&t, l.off, v, n * 2);
	for (uint8_t i = 0; i < n; i++) if (v[i] == ammo) return 1;
	return 0;
}

// Патронов в обойме (clipSize; -1 — бесконечно)
static uint16_t clip_rounds(uint16_t item)
{
	rtab_t t;
	rtab_open(RES_RULE_ITEMS, &t);
	int16_t c = (int16_t)rtab_word(&t, item, offsetof(r_items_t, clip_size));
	return c < 0 ? 999 : (uint16_t)c;
}

// Взятый — назад в груз корабля (заряженное оружие — с обоймой); запись освобождается
static void carry_to_ground(inv_t *v)
{
	ground_add((uint8_t)v->item);
	if (v->ammo != NONE16) ground_add((uint8_t)v->ammo);
	v->soldier = NONE8;
	inv_put(carry, v);
	carry = INV_NONE;
	ground_arrange();
	part |= P_SEC(INV_GROUND);
}

// Правая кнопка с предметом в руке: вернуть его на место (взятый с земли — в груз)
static void carry_cancel(void)
{
	inv_t v;
	if (carry == INV_NONE) return;
	inv_get(carry, &v);
	if (v.slot == INV_GROUND) carry_to_ground(&v);
	else { part |= P_SEC(v.slot); carry = INV_NONE; }
	sol_load();
}

// Разрядить взятое оружие (Inventory::unload): обе руки свободны (кроме него самого) — обойма в
// левую руку, оружие в правую. Предупреждения оригинала (WarningMessage) пока не выводятся
static uint8_t unload_weapon(void)
{
	inv_t v;
	if (carry == INV_NONE) return 0;
	inv_get(carry, &v);
	uint8_t rh = sec_named(STR_RIGHT_HAND), lh = sec_named(STR_LEFT_HAND);
	if (v.ammo == NONE16 || rh == 0xFF || lh == 0xFF) return 0;
	for (uint8_t k = 0; k < nsv; k++)
		if (sv[k].rec != carry && secs[sv[k].v.slot].type == 1) return 0;
	uint16_t rec = inv_alloc();
	if (rec == INV_NONE) return 0;
	inv_t a = { sel_sol, lh, 0, 0, v.ammo, NONE16, v.rounds };
	inv_put(rec, &a);
	v.slot = rh; v.x = v.y = 0;
	v.ammo = NONE16;
	v.rounds = 0;
	inv_put(carry, &v);
	carry = INV_NONE;
	part |= P_SEC(rh) | P_SEC(lh);
	sol_load();
	return 1;
}

// Левый клик (Inventory::mouseClick): взять предмет под курсором или положить взятый — клетка
// берётся под левой верхней клеткой несомого предмета, мимо секций — у земли по курсору
static void click_at(int16_t px, int16_t py)
{
	uint8_t cx, cy, slot;
	if (sel_sol == NONE8) return;                // экипажа нет — записи на «бойца 255» не заводить
	if (carry == INV_NONE) {                     // взять
		slot = hit_slot(px, py, &cx, &cy);
		if (slot == 0xFF) return;
		if (secs[slot].type == 2) {
			uint8_t ci, it = ground_item(cx, cy, &ci);
			if (it == NONE8) return;
			uint16_t rec = inv_alloc();
			if (rec == INV_NONE) return;
			inv_t v = { sel_sol, slot, 0, 0, it, NONE16, 0 };
			inv_put(rec, &v);
			uint8_t cg = ST->craft[ctx.craft].cargo;
			if (!--ST->cargo[cg].it[ci].qty) ST->cargo[cg].it[ci].item = NONE8;
			carry_set(rec, it);
		} else {
			uint8_t k = item_at(slot, cx, cy);
			if (k == 0xFF) return;
			carry_set(sv[k].rec, sv[k].v.item);
		}
		part |= P_SEC(slot);
		sol_load();
		return;
	}
	slot = hit_slot(px - HAND_W * SLOT_W / 2 + (HAND_W - carry_w) * SLOT_W / 2 + SLOT_W / 2,
			py - HAND_H * SLOT_H / 2 + (HAND_H - carry_h) * SLOT_H / 2 + SLOT_H / 2, &cx, &cy);
	if (slot == 0xFF) {
		slot = hit_slot(px, py, &cx, &cy);
		if (slot == 0xFF || secs[slot].type != 2) return;
	}
	inv_t v;
	inv_get(carry, &v);
	if (secs[slot].type == 2) {                  // на землю — в груз корабля
		carry_to_ground(&v);
		sol_load();
		return;
	}
	uint8_t k = item_at(slot, cx, cy);
	if (k != 0xFF) {                             // занято: оружие — зарядить взятой обоймой
		inv_t w = sv[k].v;
		if (w.ammo != NONE16 || !ammo_fits(w.item, v.item)) return;
		w.ammo = v.item;
		w.rounds = v.rounds ? v.rounds : clip_rounds(v.item);
		inv_put(sv[k].rec, &w);
		v.soldier = NONE8;                       // обойма ушла в оружие
		inv_put(carry, &v);
	} else {
		if (!fits(slot, cx, cy)) return;
		v.slot = slot; v.x = cx; v.y = cy;
		inv_put(carry, &v);
	}
	carry = INV_NONE;
	part |= P_SEC(slot);
	sol_load();
}

// --- экран

uint8_t inv_scr_get(uint8_t id, sdef_t *s, wdef_t *w) __banked
{
	return scr_find(tab, sizeof tab / sizeof tab[0], id, s, w);
}

// Вес бойца (BattleUnit::getCarriedWeight): броня, предметы и обоймы в оружии; взятый — не в счёт
static uint16_t sol_weight(void)
{
	rtab_t t;
	soldier_t so;
	uint16_t wt = 0;
	soldier_get(sel_sol, &so);
	if (so.armor != NONE8) {
		rtab_open(RES_RULE_ARMORS, &t);
		wt = (uint8_t)rtab_word(&t, so.armor, offsetof(r_armors_t, weight));
	}
	rtab_open(RES_RULE_ITEMS, &t);
	for (uint8_t k = 0; k < nsv; k++) {
		if (sv[k].rec == carry) continue;
		wt += (uint8_t)rtab_word(&t, sv[k].v.item, offsetof(r_items_t, weight));
		if (sv[k].v.ammo != NONE16) wt += (uint8_t)rtab_word(&t, sv[k].v.ammo, offsetof(r_items_t, weight));
	}
	return wt;
}

// Цвета строки веса (InventoryState::updateStats): текст — textWeight, вес/сила — weight: color,
// при перегрузе (вес больше силы) — color2. Цвета виджет берёт до текста — ставятся заранее
static void weight_colors(void)
{
	soldier_t so;
	uint8_t over = 0;
	if (sel_sol != NONE8) {
		soldier_get(sel_sol, &so);
		over = sol_weight() > so.cur.strength;
	}
	ui_raw[1][0] = ui_color_in(UI_SCR_INVENTORY, UI_EL_TEXTWEIGHT, 0);
	ui_raw[1][1] = ui_color_in(UI_SCR_INVENTORY, UI_EL_WEIGHT, over ? 1 : 0);
}

// Исследованы ли все требования предмета (SavedGame::isResearched): иначе его имя — «артефакт»
static uint8_t item_known(uint16_t item)
{
	rtab_t t;
	r_items_t it;
	uint16_t v[8];
	rtab_open(RES_RULE_ITEMS, &t);
	if (item >= t.n) return 1;
	rtab_get(&t, item, &it);
	uint8_t n = it.requires.n > 8 ? 8 : it.requires.n;
	if (n) rtab_tail(&t, it.requires.off, v, n * 2);
	for (uint8_t i = 0; i < n; i++)
		if (v[i] >= RES_BITS * 8 || !((ST->discovered[v[i] >> 3] >> (v[i] & 7)) & 1)) return 0;
	return 1;
}

void inv_scr_text(uint8_t id, uint8_t slot, uint8_t row, char *buf) __banked
{
	(void)id; (void)row;
	soldier_t so;
	buf[0] = 0;
	if (sel_sol == NONE8 && (slot <= 1 || (slot >= 3 && slot <= 6))) return;   // экипажа нет
	switch (slot) {
	case 0:
		soldier_get(sel_sol, &so);
		strcpy(buf, so.name);
		break;
	case 2:                                      // предмет под курсором
		if (hover_item != NONE16)
			str_copy(item_known(hover_item) ? rec_name_items(hover_item) : STR_ALIEN_ARTIFACT, buf, 40);
		break;
	case 7:                                      // патронов в оружии под курсором
		if (hover_ammo == NONE16) break;
		fmt_num(t1, hover_rounds, 0);
		str_fmt(buf, str_get(STR_AMMO_ROUNDS_LEFT), t1, "");
		break;
	case 3: case 4: case 5: case 6: {            // меткость, реакции, пси-навык и сила
		soldier_get(sel_sol, &so);
		uint16_t str_id = slot == 3 ? STR_ACCURACY_SHORT : slot == 4 ? STR_REACTIONS_SHORT
			: slot == 5 ? STR_PSIONIC_SKILL_SHORT : STR_PSIONIC_STRENGTH_SHORT;
		uint8_t v = slot == 3 ? so.cur.firing : slot == 4 ? so.cur.reactions
			: slot == 5 ? so.cur.psi_skill : so.cur.psi_strength;
		if (slot >= 5 && !so.cur.psi_skill) break;   // пси — только у обученных (как в оригинале)
		fmt_num(t1, v, 0);
		str_fmt(buf, str_get(str_id), t1, "");
		break;
	}
	case 1:                                      // «Вес>{ALT}вес/сила» (STR_WEIGHT)
		soldier_get(sel_sol, &so);
		fmt_num(t1, sol_weight(), 0);
		fmt_num(t2, so.cur.strength, 0);
		str_fmt(buf, str_get(STR_WEIGHT), t1, t2);
		break;
	}
}

// Экипаж корабля: номер бойца в state по его месту в списке
static uint8_t crew_nth(uint8_t n)
{
	soldier_t so;
	uint8_t k = 0;
	for (uint8_t i = 0; i < ST->nsoldiers; i++) {
		soldier_get(i, &so);
		if (so.base == NONE8 || so.craft != ctx.craft) continue;
		if (k == n) return i;
		k++;
	}
	return NONE8;
}

static uint8_t crew_count(void)
{
	soldier_t so;
	uint8_t k = 0;
	for (uint8_t i = 0; i < ST->nsoldiers; i++) {
		soldier_get(i, &so);
		if (so.base != NONE8 && so.craft == ctx.craft) k++;
	}
	return k;
}

uint8_t inv_scr_event(uint8_t id, uint8_t ev, uint8_t arg) __banked
{
	(void)id;
	switch (ev) {
	case EVT_OPEN:
		secs_load();
		c_grid = ui_color_in(UI_SCR_INVENTORY, UI_EL_GRID, 0);      // UFO 8, TFTD 5
		c_slots = ui_color_in(UI_SCR_INVENTORY, UI_EL_TEXTSLOTS, 0);
		c_num = ui_color_in(UI_SCR_INVENTORY, UI_EL_NUMSTACK, 0);
		crew_i = 0;
		carry = INV_NONE;
		carry_on = 0;
		part = 0;
		ground_x = 0;
		hover_item = hover_ammo = NONE16;
		sel_sol = crew_nth(crew_i);
		sol_load();
		ground_arrange();
		weight_colors();
		break;
	case EVT_DRAW: {
		// Слои как в InventoryState: кукла, затем инвентарь — сетка с чёрными клетками (стирает
		// прежние предметы) и предметы; несомый — последним, поверх всего
		uint16_t m = part;
		uint8_t full = !m;
		part = 0;
		carry_hide();
		if (full) { draw_soldier(); m = P_ALL; }
		draw_grid(m, full);
		draw_items(m);
		if (full) draw_ammo();
		carry_show();
		break;
	}
	case EVT_TICK: {                             // подпись предмета под курсором и несомый за ним
		uint16_t it = hover_item, am = hover_ammo;
		uint8_t rn = hover_rounds;
		if (carry == INV_NONE) {                 // с предметом в руке подпись не меняется (invMouseOver)
			uint8_t cx, cy, slot = hit_slot(cursor_x, cursor_y, &cx, &cy);
			it = am = NONE16;
			rn = 0;
			if (slot != 0xFF && secs[slot].type == 2) {
				uint8_t g = ground_item(cx, cy, 0);
				if (g != NONE8) it = g;
			} else if (slot != 0xFF) {
				uint8_t k = item_at(slot, cx, cy);
				if (k != 0xFF) { it = sv[k].v.item; am = sv[k].v.ammo; rn = (uint8_t)sv[k].v.rounds; }
			}
		}
		uint8_t moved = carry != INV_NONE && (cursor_x != carry_x || cursor_y != carry_y);
		if (it == hover_item && am == hover_ammo && rn == hover_rounds && !moved) break;
		carry_hide();                            // надписи — по чистому экрану, несомый — после них
		if (it != hover_item) { hover_item = it; ui_dirty(2); }
		if (am != hover_ammo || rn != hover_rounds) {
			hover_ammo = am;
			hover_rounds = rn;
			draw_ammo();
			ui_dirty(7);
		}
		part |= P_CARRY;
		ui_dirty(9);
		break;
	}
	case EVT_BUTTON:
		carry_hide();                            // экран под несомым меняется: он снова — в EVT_DRAW
		if (arg == 1) {                          // клик по кукле, слотам или земле
			if (ui_arrow_max) carry_cancel();
			else click_at(ui_click_x, ui_click_y);
			weight_colors();
			ui_dirty(1);
		} else if (arg == 4) {
			if (unload_weapon()) {               // подпись и обойма под курсором — пустые (btnUnloadClick)
				hover_item = hover_ammo = NONE16;
				draw_ammo();
				weight_colors();
				ui_dirty(1);
				ui_dirty(2);
				ui_dirty(7);
			}
		} else if (arg == 5) {                   // следующая страница земли, за последней — первая
			ground_x = gl_xmax >= ground_x + gl_sx ? ground_x + gl_sx : 0;
			part |= P_SEC(INV_GROUND);
		} else if (carry == INV_NONE) {          // с предметом в руке не уйти и не сменить бойца
			uint8_t n = crew_count();
			if (arg == 6) { UI_GO(A_POP, 0); break; }
			if (!n || sel_sol == NONE8) break;
			if (arg == 2) crew_i = crew_i ? crew_i - 1 : n - 1;
			else if (arg == 3) crew_i = crew_i + 1 < n ? crew_i + 1 : 0;
			else break;
			sel_sol = crew_nth(crew_i);
			sol_load();
			weight_colors();
			part = 0;
			UI_GO(A_REDRAW, 0);                  // кукла другой брони — прежнюю стирает только фон
			break;
		}
		part |= P_CARRY;
		ui_dirty(9);
		break;
	}
	return 0;
}
