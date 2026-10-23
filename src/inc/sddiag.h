// Диагностика SD-карты (src/kernel/sddiag.c, банк 14): почему не читаются пакеты
// OXZ/<игра>/*.PAK. Экран SCR_SDINFO (scr_menu.c) показывает её на машине — отладочные
// порты #F8AF/#F9AF видит только наш эмулятор (23_distribution.md §5).
#ifndef SDDIAG_H
#define SDDIAG_H

#include <stdint.h>

// Проверить карту: инициализация, том, чтение пачкой (CMD18 + DMA) против посекторного
// (CMD17); full — ещё и все 13 пакетов (путь, заголовок, игра).
void sddiag_run(uint8_t full) __banked;
// 1 — с картой что-то не так (пакеты не открылись или чтения разошлись): показать экран.
uint8_t sddiag_bad(void) __banked;
uint8_t sddiag_rows(void) __banked;
void sddiag_title(char *buf) __banked;
// Строка row списка: «название\tзначение»; buf — в Win0/Win1.
void sddiag_text(uint8_t row, char *buf) __banked;

#endif
