// Ввод (общий код): мышь Kempston с абсолютным курсором (аппаратный спрайт TSU),
// клавиатура ZX. Опрос раз в кадр. Курсор — глобальные cursor_x/cursor_y
// (сценарии эмулятора ставят их напрямую: pokew _cursor_x 100).
#ifndef INPUT_H
#define INPUT_H

#include <stdint.h>

#define EV_NONE   0
#define EV_CLICK  1     // левая кнопка нажата (x, y)
#define EV_RCLICK 2     // правая кнопка
#define EV_KEY    3     // клавиша (key): KEY_*, 'a'..'z', 'A'..'Z' (с CAPS SHIFT), '0'..'9'

#define KEY_ESC   27    // BREAK (CS+SPACE); просто SPACE вне поля ввода — тоже ESC (ui.c)
#define KEY_ENTER 13
#define KEY_SPACE 32
#define KEY_DEL   8     // CS+0 (DELETE)
#define KEY_LEFT  0x1C  // CS+5..8: стрелки ZX
#define KEY_DOWN  0x1D
#define KEY_UP    0x1E
#define KEY_RIGHT 0x1F

typedef struct {
	uint8_t type;
	uint8_t key;
	int16_t x, y;
} event_t;

extern int16_t cursor_x, cursor_y;
extern uint8_t cursor_off;             // 1 — курсор скрыт (cursor.c; спрайт гасится на следующем опросе)
extern uint8_t mouse_buttons;          // кнопки сейчас (бит 0 L, 1 R), обновляет input_poll

void input_isr(void);                  // Win0 (input.c): из кадрового прерывания — защёлкнуть нажатия
// Банк 11 (cursor.c). e — на стеке или в Win1.
void input_init(void) __banked;
uint8_t input_poll(event_t *e) __banked;        // 1 — есть событие
void cursor_color(uint8_t color) __banked;      // цвет курсора экрана (_cursorColor OpenXcom)

#endif
