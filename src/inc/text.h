// Шрифты и строки (src/kernel/win0/text.s, Win0, ассемблер). Формат шрифта — ресурс FONT
// (09 §7), строки — STR. Строки движка — номера из str_ids.h. Выводимая строка
// копируется в свой буфер до подключения шрифта в Win2 — можно передавать и
// константы банка вызывающего. Выходные буферы — в Win1 или на стеке.
#ifndef TEXT_H
#define TEXT_H

#include <stdint.h>

#define FNT_BIG       0
#define FNT_SMALL     1
#define FNT_GEO_BIG   2
#define FNT_GEO_SMALL 3

// Флаги вывода (как Text в OpenXcom)
#define TX_CENTER   0x01
#define TX_RIGHT    0x02
#define TX_MIDDLE   0x04    // по вертикали
#define TX_BOTTOM   0x08
#define TX_WRAP     0x10
#define TX_CONTRAST 0x20    // mul = 3
#define TX_INVERT   0x40

typedef struct {
	int16_t x, y, w, h;
	uint8_t font;
	uint8_t color, color2;  // color2 — после {ALT}
	uint8_t flags;
} tbox_t;

void text_init(void) __banked;              // src/kernel/boot.c (банк 11), один раз при запуске
void text_draw(const tbox_t *b, const char *s);
// Прокрутка (Text::setScrollable): следующий text_draw пропускает столько первых строк
// раскладки (до 24); после вызова — снова 0.
extern uint8_t tx_skip;
int16_t text_height(const tbox_t *b, const char *s);  // высота в рамке b (перенос, мелкий вместо крупного)
int16_t text_width(uint8_t font, const char *s);     // до конца строки/перевода строки
uint8_t font_height(uint8_t font);                   // высота строки (высота + spacing)

// Строка по номеру -> общий буфер (до следующего вызова). Нет строки — "#номер".
const char *str_get(uint16_t id);
// То же в свой буфер.
void str_copy(uint16_t id, char *dst, uint16_t max);
// Подстановка {0}/{1} (байты #10/#11) и {N} (#03): out не меньше 256 байт.
void str_fmt(char *out, const char *pattern, const char *a0, const char *a1);
// Мн. число: one — STR_X_one (следом STR_X_other), {N} — n
void str_plural(char *out, uint16_t one, int32_t n);
// Число в десятичный вид; sep — разделитель тысяч (0 — без).
char *fmt_num(char *out, int32_t v, char sep);

#endif
