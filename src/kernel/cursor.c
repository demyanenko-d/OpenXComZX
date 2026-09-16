// Ввод, опрос (банк 11): мышь Kempston с абсолютным курсором — спрайт TSU 0,
// события для интерфейса. Защёлки нажатий ведёт кадровое прерывание (input.c, Win0).
#include <stdint.h>
#include "tsconf.h"
#include "memmap.h"
#include "pages.h"
#include "input.h"

int16_t cursor_x = 160, cursor_y = 100;
uint8_t cursor_off;                      // 1 — курсор скрыт (заставки: Cursor::setVisible(false))
uint8_t mouse_buttons;                   // бит 0 L, 1 R (1 — нажата)
static uint8_t mouse_px, mouse_py;

extern uint8_t in_prev_btn;
extern volatile uint8_t in_btn_latch, in_key_latch, in_key_rep, in_prev_keys;

// Стрелка 11 строк: '#' — контур, '.' — заливка
static const char arrow[11][9] = {
	"#       ", "##      ", "#.#     ", "#..#    ", "#...#   ", "#....#  ",
	"#.....# ", "#..#### ", "#.#     ", "##      ", "#       ",
};

static void sfile_write(uint8_t idx, uint16_t v)
{
	volatile uint16_t *sfile = (volatile uint16_t *)0xC200;
	sfile[idx] = v;
}

// Спрайт курсора в палитре 15 (CRAM #F0..#FF): контур — пиксель 15, заливка — c & 15.
void cursor_color(uint8_t color) __banked
{
	uint8_t old = pg_win3();
	pg_map3(TSU_PAGE);
	uint8_t *sheet = (uint8_t *)0xC000;
	for (uint8_t y = 0; y < 16; y++)
		for (uint8_t x = 0; x < 8; x++) sheet[y * 256 + x] = 0;
	uint8_t fill = color & 15;
	for (uint8_t y = 0; y < 11; y++)
		for (uint8_t x = 0; x < 8; x++) {
			uint8_t c = arrow[y][x] == '#' ? 0x0F : arrow[y][x] == '.' ? fill : 0;
			uint8_t *p = sheet + y * 256 + x / 2;
			*p |= (x & 1) ? c : (uint8_t)(c << 4);
		}
	pg_map3(old);
}

static void cursor_update(void)
{
	uint8_t old = pg_win3();
	pg_map3(SCRATCH_PAGE);
	TS_FMADDR = FMADDR_EN | 0x0C;
	sfile_write(0, ((uint16_t)cursor_y & 0x1FF) | (1 << 9) | (cursor_off ? 0 : 1 << 13) | (1 << 14));
	sfile_write(1, ((uint16_t)cursor_x & 0x1FF) | (1 << 9));
	sfile_write(2, (uint16_t)15 << 12);
	TS_FMADDR = 0;
	pg_map3(old);
}

void input_init(void) __banked
{
	uint8_t old = pg_win3();
	pg_map3(SCRATCH_PAGE);
	TS_FMADDR = FMADDR_EN | 0x0C;
	for (uint16_t i = 0; i < 256; i++) sfile_write((uint8_t)i, 0);
	TS_FMADDR = 0;
	pg_map3(old);
	TS_SGPAGE = TSU_PAGE;
	cursor_color(0x0C);
	TS_TSCONFIG = TSCONF_S_EN;
	mouse_px = KMOUSE_X;
	mouse_py = KMOUSE_Y;
	in_prev_btn = ~KMOUSE_BTN & 3;
	cursor_update();
}

uint8_t input_poll(event_t *e) __banked
{
	uint8_t x = KMOUSE_X, y = KMOUSE_Y;
	cursor_x += (int8_t)(x - mouse_px);
	cursor_y -= (int8_t)(y - mouse_py);
	mouse_px = x; mouse_py = y;
	if (cursor_x < 0) cursor_x = 0;
	if (cursor_x > SCREEN_W - 1) cursor_x = SCREEN_W - 1;
	if (cursor_y < 0) cursor_y = 0;
	if (cursor_y > SCREEN_H - 1) cursor_y = SCREEN_H - 1;
	cursor_update();

	uint8_t pressed, k = 0;
	__asm__("di");                       // по одному событию за вызов
	pressed = in_btn_latch;
	if (pressed & 1) in_btn_latch = pressed & 2;
	else if (pressed) in_btn_latch = 0;
	// защёлка от автоповтора, а клавишу уже отпустили (за долгую перерисовку успевает
	// взвестись лишний раз) — выбросить: иначе глобус доворачивается после отпускания
	else { k = in_key_latch; in_key_latch = 0; if (k && in_key_rep && in_prev_keys != k) k = 0; }
	__asm__("ei");
	mouse_buttons = in_prev_btn;
	e->x = cursor_x; e->y = cursor_y;
	e->type = EV_NONE;
	if (pressed & 1) { e->type = EV_CLICK; return 1; }
	if (pressed & 2) { e->type = EV_RCLICK; return 1; }
	if (!k) return 0;
	e->type = EV_KEY;
	e->key = k;
	return 1;
}
