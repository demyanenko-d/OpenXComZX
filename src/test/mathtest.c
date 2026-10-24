// Самотест 32-битной арифметики ядра (src/kernel/win0/div32.s, mul32.s): деление и остаток
// без знака и со знаком, умножение 32 x 32, 16 x 16 -> 32 сверяются с эталоном на сдвигах,
// сложениях и вычитаниях. Сначала все пары краевых значений (границы путей делителя 8/16/32
// бит, знаки, переносы), затем случайные (xorshift32) с разной длиной операндов.
// Зовёт главное меню по запросу math_req (число случайных пар; tests/math.oxs).
#include <stdint.h>
#include "dbg.h"
#include "banktest.h"

uint16_t math_req;

// Итог — в памяти банка 14 (страница #3E, от #3400): +0 ошибок, +2 пар проверено,
// +4 x, +8 y, +C операция первой ошибки (1 — x*y, 2 — x/y, 3 — x%y, 4, 5 — то же со знаком,
// 6 — u16*u16, 7 — s16*s16)
static uint16_t __at(0xB400) mt_fail;
static uint16_t __at(0xB402) mt_cnt;
static uint32_t __at(0xB404) mt_x;
static uint32_t __at(0xB408) mt_y;
static uint8_t __at(0xB40C) mt_op;

static const uint32_t edge[] = {
	0, 1, 2, 3, 7, 10, 100, 127, 128, 129, 255, 256, 257, 1000, 21600, 32767, 32768, 32769,
	65535, 65536, 65537, 0x00FFFFFFul, 0x01000000ul, 0x12345678ul, 0x7FFFFFFFul, 0x80000000ul,
	0x80000001ul, 0xFFFF0000ul, 0xFFFF8000ul, 0xFFFFFF9Cul, 0xFFFFFFFEul, 0xFFFFFFFFul,
};
#define NEDGE (sizeof edge / sizeof edge[0])

static void ref_divmod(uint32_t x, uint32_t y, uint32_t *q, uint32_t *r)
{
	uint32_t qq = 0, rr = 0;
	for (uint8_t i = 0; i < 32; i++) {
		uint8_t c = (rr & 0x80000000ul) != 0;
		rr <<= 1;
		if (x & 0x80000000ul) rr |= 1;
		x <<= 1;
		qq <<= 1;
		if (c || rr >= y) { rr -= y; qq |= 1; }
	}
	*q = qq;
	*r = rr;
}

static uint32_t ref_mul(uint32_t a, uint32_t b)
{
	uint32_t p = 0;
	while (b) {
		if (b & 1) p += a;
		a <<= 1;
		b >>= 1;
	}
	return p;
}

static void hex32(uint32_t v)
{
	dbg_hex8((uint8_t)(v >> 24));
	dbg_hex8((uint8_t)(v >> 16));
	dbg_hex8((uint8_t)(v >> 8));
	dbg_hex8((uint8_t)v);
}

static void fail(uint8_t op, uint32_t x, uint32_t y)
{
	if (!mt_fail) { mt_x = x; mt_y = y; mt_op = op; }
	mt_fail++;
	if (mt_fail > 20) return;
	dbg_puts("math: FAIL op ");
	dbg_hex8(op);
	dbg_puts(" x ");
	hex32(x);
	dbg_puts(" y ");
	hex32(y);
	dbg_puts("\n");
}

static void test_pair(uint32_t x, uint32_t y)
{
	uint32_t q, r;
	mt_cnt++;
	if (x * y != ref_mul(x, y)) fail(1, x, y);
	uint16_t a = (uint16_t)x, b = (uint16_t)y;
	if ((uint32_t)a * b != ref_mul(a, b)) fail(6, x, y);
	if ((int32_t)(int16_t)a * (int16_t)b != (int32_t)ref_mul((uint32_t)(int32_t)(int16_t)a, (uint32_t)(int32_t)(int16_t)b))
		fail(7, x, y);
	if (!y) return;
	ref_divmod(x, y, &q, &r);
	if (x / y != q) fail(2, x, y);
	if (x % y != r) fail(3, x, y);
	int32_t sx = (int32_t)x, sy = (int32_t)y;
	if (x == 0x80000000ul && y == 0xFFFFFFFFul) return;     // INT32_MIN / -1 — переполнение
	ref_divmod(sx < 0 ? 0 - x : x, sy < 0 ? 0 - y : y, &q, &r);
	if ((sx < 0) != (sy < 0)) q = 0 - q;
	if (sx < 0) r = 0 - r;
	if ((uint32_t)(sx / sy) != q) fail(4, x, y);
	if ((uint32_t)(sx % sy) != r) fail(5, x, y);
}

static uint32_t rnd_s;
static uint32_t rnd(void)
{
	uint32_t s = rnd_s;
	s ^= s << 13;
	s ^= s >> 17;
	s ^= s << 5;
	return rnd_s = s;
}

// Длина операнда — случайная: сдвиг вправо на 0..31
static uint32_t rnd_len(void)
{
	uint32_t v = rnd();
	for (uint8_t k = (uint8_t)(rnd() & 31); k; k--) v >>= 1;
	return v;
}

void math_test(uint16_t n) __banked
{
	mt_fail = 0;
	mt_cnt = 0;
	mt_op = 0;
	for (uint8_t i = 0; i < NEDGE; i++)
		for (uint8_t j = 0; j < NEDGE; j++) test_pair(edge[i], edge[j]);
	rnd_s = 0x2545F491ul;
	while (n--) {
		uint32_t x = rnd_len();
		test_pair(x, rnd_len());
		test_pair(x, rnd() >> 24);                   // делитель — байт
		test_pair(x, rnd() >> 16);                   // делитель — слово
	}
	dbg_puts("math: ");
	dbg_dec(mt_cnt);
	dbg_puts(" pairs, fail ");
	dbg_dec(mt_fail);
	dbg_puts("\n");
}
