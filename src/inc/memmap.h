// Раскладка памяти: номера страниц и окна — src/map.h (карта всех страниц с описанием),
// здесь — то, что к карте страниц не относится (стек, векторы, размер экрана).
// Значения должны совпадать с tools/build.ps1 и src/kernel/win0/crt0.s.
#ifndef MEMMAP_H
#define MEMMAP_H

#include "../map.h"

#define IM2_I         0x3E      // таблица векторов: #3EFB, #3EFD, #3EFF
#define STACK_TOP     0x3E00

#define SCREEN_W      320
#define SCREEN_H      200

#endif
