// Тест джампера, банк 1 (сборка: --codeseg BANK1 --constseg BANK1).
#include <stdint.h>
#include "tsconf.h"
#include "dbg.h"
#include "banktest.h"

// Константа банка: указатель можно отдавать общему коду (банк остаётся
// включён на время вызова), но не функциям других банков.
static const char hello[] = "bank A: hello from BANK1\n";

int16_t bank_a_calc(int16_t x) __banked
{
	bank_a_page = TS_PAGE2;
	dbg_puts(hello);
	int16_t r = x * 3 + bank_b_add(x);
	bank_a_page_after = TS_PAGE2;       // трамплин вернул наш банк?
	return r;
}
