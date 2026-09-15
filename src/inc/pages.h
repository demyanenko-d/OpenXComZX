// Менеджер страниц (общий код). См. project_docs/08_ui_port_plan.md §4.4.
#ifndef PAGES_H
#define PAGES_H

#include <stdint.h>

#define PG_NONE 0xFF

// Выделить n подряд идущих страниц пула (#A0-#EF), первая кратна align
// (1, 2, 4, 8, 16). Возвращает первую страницу или PG_NONE.
uint8_t pg_alloc(uint8_t n, uint8_t align);
void pg_free(uint8_t first, uint8_t n);
uint8_t pg_free_count(void);

// Подключить страницу в Win3 (#C000). Возвращает прежнюю — для восстановления.
uint8_t pg_map3(uint8_t page);
uint8_t pg_win3(void);

// Дальний адрес: 22 бита = (страница << 14) | смещение — формат адресов DMA.
typedef uint32_t far_t;
#define FAR(page, offs) (((far_t)(page) << 14) | (uint16_t)(offs))

#endif
