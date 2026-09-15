// Тест джампера: функции в разных банках кода (src/test/bank_a.c — BANK1,
// src/test/bank_b.c — BANK2) вызывают друг друга и общий код.
#ifndef BANKTEST_H
#define BANKTEST_H

#include <stdint.h>

int16_t bank_a_calc(int16_t x) __banked;   // x*3 + bank_b_add(x)
int16_t bank_b_add(int16_t x) __banked;    // x + 1000

// Страница в Win2, увиденная кодом банка (регистр Page2 читается).
extern uint8_t bank_a_page, bank_a_page_after, bank_b_page;

#endif
