// Очередь команд DMA, продвигаемая прерыванием DMA (src/kernel/dmaq.s).
// При работающей очереди регистры DMA напрямую не трогать.
#ifndef DMAQ_H
#define DMAQ_H

#include <stdint.h>

// Порядок полей — под OUTI в isr_dma, не менять.
typedef struct {
	uint8_t dax, dah, dal;   // приёмник: страница, A13:8, A7:0
	uint8_t sax, sah, sal;   // источник
	uint8_t len;             // слов в пачке - 1
	uint8_t num;             // пачек - 1
	uint8_t ctrl;            // DMACtrl (запуск)
} dma_cmd_t;

void dmaq_push(const dma_cmd_t *cmd);  // ждёт, если кольцо (255 команд) полно
uint8_t dmaq_busy(void);
void dmaq_wait(void);

#endif
