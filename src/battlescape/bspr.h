// Значки замеченных пришельцев (bspr.c, банк 33) — аппаратные спрайты TSU над картой.
#ifndef BSPR_H
#define BSPR_H

#include <stdint.h>

void marks_init(void) __banked;                      // плитки значков в лист TSU (вход в бой)
void marks_show(uint8_t n, uint8_t color) __banked;  // показать n значков (0 — спрятать), color — цифр
void marks_blink(void) __banked;                     // шаг мигания заливки (раз в 100 мс)

#endif
