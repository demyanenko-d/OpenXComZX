// Графика интерфейса: экран 320x200 256c (страницы SCREEN_PAGE.., строка 512 байт).
// Код — банк 1 (src/kernel/gfx.c, вместе с ui.c), кроме gfx_map (общий, pages.c).
// Координаты — пиксели экрана; всё обрезается по экрану. Функции сохраняют Win3.
#ifndef GFX_H
#define GFX_H

#include <stdint.h>

void gfx_init(void) __banked;
void gfx_fill(int16_t x, int16_t y, int16_t w, int16_t h, uint8_t c) __banked;
// Фаска кнопки/тонкой рамки (inv — цвет середины нажатой кнопки, 0 — без инверсии) и кнопка-стрелка
void gfx_bevel(int16_t x, int16_t y, int16_t w, int16_t h, uint8_t c, uint8_t geo, uint8_t inv) __banked;
void gfx_arrow(int16_t x, int16_t y, uint8_t c, uint8_t down) __banked;
// Маленькая стрелка списка: shape 0 вверх, 1 вниз, 2 влево, 3 вправо
void gfx_arrow_small(int16_t x, int16_t y, uint8_t c, uint8_t shape) __banked;
// Окно: фон bg (0 — заливка c+3) и кольца рамки; thin — фаска тонкой рамки
void gfx_window(int16_t x, int16_t y, int16_t w, int16_t h, uint8_t c, uint16_t bg, uint8_t thin) __banked;
// Подсветка строки списка под курсором (combo — правила ComboBox)
void gfx_selector(int16_t x, int16_t y, int16_t w, uint8_t h, uint8_t combo) __banked;
void gfx_pset(int16_t x, int16_t y, uint8_t c) __banked;
void gfx_line(int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint8_t c) __banked;
// Прямоугольник полноэкранной картинки IMG8 (320 в ширину) в ту же позицию экрана.
// 0 — нет ресурса.
uint8_t gfx_bg(uint16_t res_id, int16_t x, int16_t y, int16_t w, int16_t h) __banked;
// Прямоугольник (sx, sy, w, h) картинки IMG8 в точку экрана (x, y). Непрозрачно;
// x и sx одной чётности, w округляется до чётной (DMA копирует словами). 0 — нет ресурса.
uint8_t gfx_blit(uint16_t res_id, int16_t sx, int16_t sy, int16_t x, int16_t y, int16_t w, int16_t h) __banked;
extern uint8_t gfx_key;                 // 1 — gfx_blit пропускает цвет 0 (DMA BLT1, картинка поверх фона)
// Кадр набора спрайтов (SPRSET) в угол ячейки (x, y), 0 — прозрачно. 0 — нет ресурса.
uint8_t gfx_sprite(uint16_t res_id, uint16_t frame, int16_t x, int16_t y) __banked;
// Палитра экрана: ресурс палитры + блок BACKPALS (16 цветов) в цвета 224..239 (-1 — нет).
void gfx_palette(uint16_t pal_id, int8_t backpal) __banked;
// Сдвиг цветов lo..lo+n-1 текущей палитры на шаг (цикл палитры; следующая gfx_palette сбросит).
void gfx_pal_cycle(uint8_t lo, uint8_t n) __banked;
// Фон под всплывающим окном уровня lvl стека: запомнить прямоугольник экрана (страницы пула; 0 — нет
// страниц) и вернуть на экран (draw = 1) или только забыть (draw = 0). 1 — фон возвращён.
uint8_t gfx_bgsave(uint8_t lvl, int16_t x, int16_t y, int16_t w, int16_t h) __banked;
uint8_t gfx_bgrestore(uint8_t lvl, uint8_t draw) __banked;
// Рамка окна: 5 колец c+3, c+2, c+1, c+2, c+3 (Window::draw)
void gfx_rings(int16_t x, int16_t y, int16_t w, int16_t h, uint8_t c) __banked;
// Window::popup: рамка окна растёт 10 кадров (flags: 2 — по горизонтали, 4 — по вертикали), фон — bg или c+3
void gfx_popup(int16_t x, int16_t y, int16_t w, int16_t h, uint8_t c, uint16_t bg, uint8_t flags) __banked;

// Адрес CPU пикселя (x, y): страница строки подключается в Win3 и не
// восстанавливается — вызывающий сохраняет и восстанавливает pg_win3() сам.
uint8_t *gfx_map(int16_t x, int16_t y);
// Смещение строки для всей графики: 0 — видимый экран, иначе рабочая область (pages.s)
extern uint16_t gfx_yb;
// Прямоугольник экранной памяти на другие строки (2D DMA): сборка виджета -> экран
void gfx_copy(int16_t x, int16_t ysrc, int16_t ydst, int16_t w, int16_t h) __banked;

#endif
