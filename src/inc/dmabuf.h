// Буферы-источники DMA с гарантированно чётными адресами (src/kernel/dmabuf.s).
// DMA игнорирует бит 0 адреса: всё, что читает DMA из Win0/Win1, — только отсюда.
#ifndef DMABUF_H
#define DMABUF_H

#include <stdint.h>

extern uint16_t dma_palette[256];
extern uint16_t dma_fill_word;

#endif
