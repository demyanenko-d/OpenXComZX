// Ввод, опрос (банк 11): события для интерфейса. Курсор — аппаратный спрайт TSU 0, и мышь, и
// положение спрайта ведёт кадровое прерывание (src/kernel/win0/input.s): из главного цикла курсор
// залипал на кадре глобуса и блитах. Здесь остались только картинка курсора и разбор нажатий.
#include <stdint.h>
#include "tsconf.h"
#include "memmap.h"
#include "pages.h"
#include "input.h"

extern uint8_t in_prev_btn;
extern uint8_t cur_lock;
extern uint8_t cell_cur_on;              // 1 — в списке спрайтов есть курсор клетки (input.s)
extern uint8_t cur_pal;                  // группа палитры спрайтов курсора (input.s)                 // 1 — S-file занят: прерывание не трогает спрайт курсора
void cursor_sync(void);                  // input.s: запомнить показания мыши, не двигая курсор
extern volatile uint8_t in_btn_latch, in_key_latch, in_key_rep, in_key_reps, in_prev_keys;
uint8_t in_reps;                     // нажатий за один опрос (поворот глобуса делает столько шагов)

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

// Цвет курсора — номер цвета палитры игры, как в OpenXcom (Mod::GEOSCAPE_CURSOR 252,
// Mod::BATTLESCAPE_CURSOR 144): старший полубайт выбирает группу палитры спрайта TSU
// (CRAM cur_pal · 16 + пиксель), младший — цвет заливки. Контур на 3 светлее (Cursor::draw
// ведёт четыре линии цветами color..color+3). Пиксель 0 у TSU прозрачен, поэтому заливка
// не бывает нулевой (у боя цвет 144 — как раз нулевой в своей группе).
void cursor_color(uint8_t color) __banked
{
	uint8_t old = pg_win3();
	pg_map3(TSU_PAGE);
	uint8_t *sheet = (uint8_t *)0xC000;
	for (uint8_t y = 0; y < 16; y++)
		for (uint8_t x = 0; x < 8; x++) sheet[y * 256 + x] = 0;
	uint8_t fill = color & 15;
	if (!fill) fill = 1;
	uint8_t edge = (uint8_t)(fill + 3 > 15 ? 15 : fill + 3);
	cur_pal = (uint8_t)(color >> 4);
	for (uint8_t y = 0; y < 11; y++)
		for (uint8_t x = 0; x < 8; x++) {
			uint8_t c = arrow[y][x] == '#' ? edge : arrow[y][x] == '.' ? fill : 0;
			uint8_t *p = sheet + y * 256 + x / 2;
			*p |= (x & 1) ? c : (uint8_t)(c << 4);
		}
	pg_map3(old);
}

// Курсор клетки для наземного боя: ромб 32x16 в листе спрайтов, спрайт 1 в S-file
// (спрайт 0 занят стрелкой мыши и ведётся прерыванием). Рисуется рядом со стрелкой —
// с x = 8, тайлы 1..4 и 65..68. color — пиксель внутри группы палитры курсора (не 0).
void cell_cursor_init(uint8_t color) __banked
{
	uint8_t old = pg_win3();
	pg_map3(TSU_PAGE);
	uint8_t *sheet = (uint8_t *)0xC000;
	for (uint8_t y = 0; y < 16; y++)
		for (uint8_t x = 8; x < 40; x++) sheet[y * 256 + x / 2] = 0;
	// Ромб клетки: верхняя и нижняя грани. Клетка 32x16, грань идёт на полпикселя по Y.
	for (uint8_t i = 0; i < 32; i++) {
		uint8_t c = color & 15;
		uint8_t yt = (uint8_t)(i < 16 ? (15 - i) / 2 : (i - 16) / 2);
		uint8_t yb = (uint8_t)(15 - yt);
		uint8_t x = (uint8_t)(8 + i);
		for (uint8_t k = 0; k < 2; k++) {
			uint8_t y = k ? yb : yt;
			uint8_t *p = sheet + (uint16_t)y * 256 + x / 2;
			*p = (uint8_t)((x & 1) ? ((*p & 0xF0) | c) : ((*p & 0x0F) | (uint8_t)(c << 4)));
		}
	}
	pg_map3(old);
}

// Поставить курсор клетки в экранную точку (левый верхний угол ромба) или убрать его.
void cell_cursor(int16_t x, int16_t y, uint8_t on) __banked
{
	uint8_t old = pg_win3();
	cur_lock = 1;                      // прерывание не должно трогать список, пока пишем
	pg_map3(SCRATCH_PAGE);
	TS_FMADDR = FMADDR_EN | 0x0C;
	if (on && x > -32 && y > -16 && x < 320 && y < 200) {
		// бит 14 — последний спрайт слоя: теперь это курсор клетки, а не стрелка
		sfile_write(3, (uint16_t)((y & 0x1FF) | (1 << 9) | (1 << 13) | (1 << 14)));
		sfile_write(4, (uint16_t)((x & 0x1FF) | (3 << 9)));               // ширина 32
		sfile_write(5, (uint16_t)(1 | ((uint16_t)cur_pal << 12)));        // тайл 1, группа палитры
		cell_cur_on = 1;
	} else {
		sfile_write(3, 0);
		cell_cur_on = 0;
	}
	TS_FMADDR = 0;
	pg_map3(old);
	cur_lock = 0;
}

void input_init(void) __banked
{
	uint8_t old = pg_win3();
	cur_lock = 1;                      // весь список спрайтов — под запрет прерыванию курсора
	pg_map3(SCRATCH_PAGE);
	TS_FMADDR = FMADDR_EN | 0x0C;
	for (uint16_t i = 0; i < 256; i++) sfile_write((uint8_t)i, 0);
	TS_FMADDR = 0;
	pg_map3(old);
	TS_SGPAGE = TSU_PAGE;
	cursor_color(CURSOR_GEOSCAPE);
	TS_TSCONFIG = TSCONF_S_EN;
	cursor_sync();                     // показания мыши — без скачка курсора на первом кадре
	in_prev_btn = ~KMOUSE_BTN & 3;
	cur_lock = 0;                      // дальше спрайт ведёт прерывание
}

uint8_t input_poll(event_t *e) __banked
{
	uint8_t pressed, k = 0;             // мышь и спрайт курсора — в прерывании (input.s)
	__asm__("di");
	in_reps = 0;                       // по одному событию за вызов
	pressed = in_btn_latch;
	if (pressed & 1) in_btn_latch = pressed & 2;
	else if (pressed) in_btn_latch = 0;
	// защёлка от автоповтора, а клавишу уже отпустили (за долгую перерисовку успевает
	// взвестись лишний раз) — выбросить: иначе глобус доворачивается после отпускания
	else {
		k = in_key_latch; in_key_latch = 0;
		in_reps = in_key_reps; in_key_reps = 0;
		if (k && in_key_rep && in_prev_keys != k) { k = 0; in_reps = 0; }
	}
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
