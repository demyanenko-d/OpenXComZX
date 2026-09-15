// Тест джампера, банк 2 (сборка: --codeseg BANK2 --constseg BANK2).
#include <stdint.h>
#include "tsconf.h"
#include "dbg.h"
#include "banktest.h"

uint8_t bank_a_page, bank_a_page_after, bank_b_page;

int16_t bank_b_add(int16_t x) __banked
{
	bank_b_page = TS_PAGE2;
	dbg_puts("bank B: hello from BANK2\n");
	return x + 1000;
}
