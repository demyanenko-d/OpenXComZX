// Тест джампера: функции в разных банках кода (src/test/bank_a.c — BANK1,
// src/test/bank_b.c — BANK2) вызывают друг друга и общий код.
#ifndef BANKTEST_H
#define BANKTEST_H

#include <stdint.h>

int16_t bank_a_calc(int16_t x) __banked;   // x*3 + bank_b_add(x)
int16_t bank_b_add(int16_t x) __banked;    // x + 1000

// Страница в Win2, увиденная кодом банка (регистр Page2 читается).
extern uint8_t bank_a_page, bank_a_page_after, bank_b_page;

// Самотест 32-битной арифметики (src/test/mathtest.c, банк 14): главное меню зовёт его по
// запросу math_req (число случайных пар, tests/math.oxs) и ставит метку 250 (OXZ_DBG_MARK)
extern uint16_t math_req;
void math_test(uint16_t n) __banked;

#endif
