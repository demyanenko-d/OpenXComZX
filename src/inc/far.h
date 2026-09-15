// Дальняя память: 22-битный физический адрес (страница << 14 | смещение) —
// тот же формат, что у адресов DMA. Доступ CPU — через окно Win3 (#C000).
#ifndef FAR_H
#define FAR_H

#include <stdint.h>

typedef uint32_t far_t;
#define FAR(page, offs) (((far_t)(page) << 14) | (uint16_t)(offs))
#define FAR_PAGE(f)     ((uint8_t)((f) >> 14))
#define FAR_OFFS(f)     ((uint16_t)(f) & 0x3FFF)

// Скопировать len байт из дальней памяти в src ближнюю (Win0/Win1). Win3 восстанавливается.
void far_read(far_t src, void *dst, uint16_t len);
// Обратно: из ближней памяти в дальнюю; заполнить дальнюю байтом.
void far_write(far_t dst, const void *src, uint16_t len);
void far_fill(far_t dst, uint8_t v, uint16_t len);
uint8_t far_byte(far_t src);
uint16_t far_word(far_t src);

// DMA RAM->RAM (линейно, len — чётное, до 128 КБ). Ждёт окончания.
void far_copy(far_t dst, far_t src, uint32_t len);

// Физический адрес объекта в Win0/Win1 (общий код и данные).
far_t near_phys(const void *p);

#endif
