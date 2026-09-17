// Раскладка памяти. См. project_docs/08_ui_port_plan.md §4.
//
// Окна CPU:
//   Win0 #0000-#3FFF  стр. KERNEL_PAGE: RST, общий код (#0100-#37FF),
//                     стек (вниз от #3E00), векторы IM2 #3EFB-#3F00
//   Win1 #4000-#7FFF  стр. DATA_PAGE: все переменные C и ядра;
//                     #7FE0-#7FF0 — заглушка входа SPG
//   Win2 #8000-#BFFF  банки кода (__banked), страница = bank_page[банк]
//   Win3 #C000-#FFFF  дальние данные (без кэша: сюда пишет DMA)
//
// Значения должны совпадать с tools/build.ps1 и src/kernel/win0/crt0.s.
#ifndef MEMMAP_H
#define MEMMAP_H

#define KERNEL_PAGE   0x04
#define DATA_PAGE     0x05

#define IM2_I         0x3E      // таблица векторов: #3EFB, #3EFD, #3EFF
#define STACK_TOP     0x3E00

// Карта физических страниц (256 шт.). #F0-#FF не трогать.
#define STATE_PAGE    0x06      // состояние кампании (src/inc/state.h): #06-#09, 64 КБ
#define STATE_PAGES   4
#define SCRATCH_PAGE  0x0F      // под окном FMAddr (input.c)
#define SCREEN_PAGE   0x10      // экран 256c 512x512: 16 стр., выравнивание 16
#define TSU_PAGE      0x20      // листы TSU: 16 стр., выравнивание 8
#define BANK_PAGE     0x30      // банки кода: резидентные #30-#3F, оверлей #40-#4F
#define RES_PAGE      0x50      // ресурсы (пакеты *.PAK) #50-#AF
#define RES_LAST      0xAF
#define POOL_FIRST    0xB0      // динамический пул (менеджер страниц)
#define POOL_LAST     0xEF
#define RESERVED_PAGE 0xF0      // #F0-#FF — загрузчики, TS-BIOS, vDOS

#define SCREEN_W      320
#define SCREEN_H      200

#endif
