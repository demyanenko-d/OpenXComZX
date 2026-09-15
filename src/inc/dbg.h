// Отладочный вывод в консоль эмулятора (порт #F8AF, см. project_docs/07_debug_tools.md).
// На реальном железе запись в порт игнорируется. Общий код.
#ifndef DBG_H
#define DBG_H

#include <stdint.h>

void dbg_puts(const char *s);
void dbg_dec(uint32_t v);
void dbg_hex8(uint8_t v);

#endif
