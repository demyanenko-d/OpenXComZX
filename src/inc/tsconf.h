// Регистры и порты TS-Config для SDCC (Z80).
//
// Источник — эмуляция TS-Config в Unreal:
//   unreal/Unreal/devices/tsconf/tsconf.h  (enum TSREGS, DMADEV, битовые поля)
//   unreal/Unreal/devices/tsconf/tsconf.cpp (DMA), devices/io/io.cpp (порты)
// Сводка — project_docs/02_tsconf_hardware.md.
// НЕ основан на REF/src_sdcc/inc/tsconf.h: там регистры #14-#1E сдвинуты на 1
// и неверны все коды DMA (02 §12).
//
// Порт регистра TS = (reg << 8) | #AF. Читаются только STATUS, PAGE2, PAGE3,
// DMASTATUS; остальные — только запись (держать теневые копии).
#ifndef TSCONF_H
#define TSCONF_H

#include <stdint.h>

#define TS_PORT(reg) ((uint16_t)(((reg) << 8) | 0xAF))

// --- видео ---
__sfr __banked __at(0x00AF) TS_VCONFIG;   // W
__sfr __banked __at(0x00AF) TS_STATUS;    // R
__sfr __banked __at(0x01AF) TS_VPAGE;
__sfr __banked __at(0x02AF) TS_GXOFFSL;
__sfr __banked __at(0x03AF) TS_GXOFFSH;
__sfr __banked __at(0x04AF) TS_GYOFFSL;
__sfr __banked __at(0x05AF) TS_GYOFFSH;
__sfr __banked __at(0x06AF) TS_TSCONFIG;
__sfr __banked __at(0x07AF) TS_PALSEL;
__sfr __banked __at(0x0FAF) TS_BORDER;

// --- память ---
__sfr __banked __at(0x10AF) TS_PAGE0;
__sfr __banked __at(0x11AF) TS_PAGE1;
__sfr __banked __at(0x12AF) TS_PAGE2;     // R/W
__sfr __banked __at(0x13AF) TS_PAGE3;     // R/W
__sfr __banked __at(0x15AF) TS_FMADDR;
__sfr __banked __at(0x21AF) TS_MEMCONFIG;

// --- TSU ---
__sfr __banked __at(0x16AF) TS_TMPAGE;
__sfr __banked __at(0x17AF) TS_T0GPAGE;
__sfr __banked __at(0x18AF) TS_T1GPAGE;
__sfr __banked __at(0x19AF) TS_SGPAGE;
__sfr __banked __at(0x40AF) TS_T0XOFFSL;
__sfr __banked __at(0x41AF) TS_T0XOFFSH;
__sfr __banked __at(0x42AF) TS_T0YOFFSL;
__sfr __banked __at(0x43AF) TS_T0YOFFSH;
__sfr __banked __at(0x44AF) TS_T1XOFFSL;
__sfr __banked __at(0x45AF) TS_T1XOFFSH;
__sfr __banked __at(0x46AF) TS_T1YOFFSL;
__sfr __banked __at(0x47AF) TS_T1YOFFSH;

// --- DMA ---
__sfr __banked __at(0x1AAF) TS_DMASAL;    // A7:1 (бит 0 игнорируется)
__sfr __banked __at(0x1BAF) TS_DMASAH;    // A13:8
__sfr __banked __at(0x1CAF) TS_DMASAX;    // страница A21:14
__sfr __banked __at(0x1DAF) TS_DMADAL;
__sfr __banked __at(0x1EAF) TS_DMADAH;
__sfr __banked __at(0x1FAF) TS_DMADAX;
__sfr __banked __at(0x26AF) TS_DMALEN;    // слов в пачке - 1
__sfr __banked __at(0x27AF) TS_DMACTRL;   // W: запуск
__sfr __banked __at(0x27AF) TS_DMASTATUS; // R: бит 7 = DMA работает
__sfr __banked __at(0x28AF) TS_DMANUM;    // пачек - 1

// --- система и прерывания ---
__sfr __banked __at(0x20AF) TS_SYSCONFIG;
__sfr __banked __at(0x22AF) TS_HSINT;     // 0..223 (такты 3.5 МГц), >223 — выкл
__sfr __banked __at(0x23AF) TS_VSINTL;
__sfr __banked __at(0x24AF) TS_VSINTH;    // строка 0..319, >319 — выкл
__sfr __banked __at(0x29AF) TS_FDDVIRT;
__sfr __banked __at(0x2AAF) TS_INTMASK;
__sfr __banked __at(0x2BAF) TS_CACHECONFIG;

// --- отладочные порты эмулятора OpenXComZX (unreal/Unreal/app/oxz_test.h) ---
// Номера регистров не заняты в TS-Config; на железе запись игнорируется.
__sfr __banked __at(0xF8AF) OXZ_DBG_CHAR;  // символ в stdout эмулятора
__sfr __banked __at(0xF9AF) OXZ_DBG_HEX;   // байт как hex
__sfr __banked __at(0xFAAF) OXZ_DBG_EXIT;  // выход с кодом (тестовый режим)
__sfr __banked __at(0xFBAF) OXZ_DBG_SHOT;  // снимок экрана
__sfr __banked __at(0xFCAF) OXZ_DBG_MARK;  // метка для сценария (waitmark)
// «Служба BIOS» эмулятора — файлы на хосте (src/kernel/bios.c): адрес блока
// запроса (адрес CPU, младший/старший байт), затем команда.
__sfr __banked __at(0xFDAF) OXZ_BIOS_LO;
__sfr __banked __at(0xFEAF) OXZ_BIOS_HI;
__sfr __banked __at(0xFFAF) OXZ_BIOS_CMD;
// Чтение #FDAF: #5A — эмулятор выполняет сценарий (-s): заставка при запуске не играет.
__sfr __banked __at(0xFDAF) OXZ_DBG_SCRIPT;
#define OXZ_SCRIPT_SIG 0x5A

// VConfig
#define VCONF_VM_ZX      0x00
#define VCONF_VM_16C     0x01
#define VCONF_VM_256C    0x02
#define VCONF_VM_TEXT    0x03
#define VCONF_FT_EN      0x04
#define VCONF_GFXOVR     0x08
#define VCONF_NOTSU      0x10
#define VCONF_NOGFX      0x20
#define VCONF_RRES_256x192 0x00
#define VCONF_RRES_320x200 0x40
#define VCONF_RRES_320x240 0x80
#define VCONF_RRES_360x288 0xC0

// TSConfig
#define TSCONF_T0YS_EN   0x01
#define TSCONF_T1YS_EN   0x02
#define TSCONF_T0Z_EN    0x04
#define TSCONF_T1Z_EN    0x08
#define TSCONF_T0_EN     0x20
#define TSCONF_T1_EN     0x40
#define TSCONF_S_EN      0x80

// SysConfig. ВНИМАНИЕ: запись SysConfig перезаписывает CacheConfig
// (#0F при CACHE_EN, иначе #00) — CacheConfig писать после.
#define SYSCONF_ZCLK_3M5 0x00
#define SYSCONF_ZCLK_7M  0x01
#define SYSCONF_ZCLK_14M 0x02
#define SYSCONF_CACHE_EN 0x04

// CacheConfig: кэш по окнам
#define CACHE_WIN0       0x01
#define CACHE_WIN1       0x02
#define CACHE_WIN2       0x04
#define CACHE_WIN3       0x08

// MemConfig
#define MEMCONF_ROM128   0x01
#define MEMCONF_W0_WE    0x02
#define MEMCONF_W0_MAP_N 0x04   // 1 — Page0 как есть
#define MEMCONF_W0_RAM   0x08
#define MEMCONF_LCK512   0x00
#define MEMCONF_LCK128   0x40
#define MEMCONF_LCKAUTO  0x80
#define MEMCONF_LCK1024  0xC0

// FMAddr: окно 4 КБ по адресу A[15:12]: +#000 CRAM, +#200 SFILE, +#400 регистры
#define FMADDR_EN        0x10

// INTMask
#define INT_FRAME        0x01
#define INT_LINE         0x02
#define INT_DMA          0x04

// Векторы IM2 (младший байт адреса слова в таблице I:xx)
#define IM2_VEC_FRAME    0xFF   // слово по I:FF / (I+1):00
#define IM2_VEC_LINE     0xFD
#define IM2_VEC_DMA      0xFB

// DMACtrl: биты 2:0 устройство, 3 ASZ, 4 D_ALGN, 5 S_ALGN, 6 OPT, 7 WNR
#define DMA_ASZ          0x08   // 1 — байтовые пиксели и шаг 512 (256c)
#define DMA_D_ALGN       0x10   // пачка приёмника: начало предыдущей + 256/512
#define DMA_S_ALGN       0x20   // то же для источника (внутри пачки — заворот по строке)
#define DMA_OPT          0x40   // BLT2: насыщение
#define DMA_RAM_RAM      0x01
#define DMA_SPI_RAM      0x02
#define DMA_IDE_RAM      0x03
#define DMA_FILL         0x04   // одно слово источника пишется len*num раз
#define DMA_BLT2         0x06   // dst = src + dst (по байтам/полубайтам)
#define DMA_BLT1         0x81   // копия без нулевых пикселей источника
#define DMA_RAM_SPI      0x82
#define DMA_RAM_IDE      0x83
#define DMA_RAM_CRAM     0x84   // индекс CRAM = (DADDR >> 1) & #FF
#define DMA_RAM_SFILE    0x85
#define DMASTATUS_ACT    0x80

// --- прочие порты ---
__sfr __at(0x57) ZC_DATA;     // SPI SD: обмен байтом (чтение — байт предыдущего обмена)
__sfr __at(0x77) ZC_CFG;      // бит 1 — CS# SD1 (активный 0)

// Мышь Kempston (кнопки активны нулём: бит 0 L, 1 R, 2 M; 7:4 — колесо)
__sfr __banked __at(0xFADF) KMOUSE_BTN;
__sfr __banked __at(0xFBDF) KMOUSE_X;
__sfr __banked __at(0xFFDF) KMOUSE_Y;       // растёт вверх

// Клавиатура: полуряды матрицы ZX (биты 0-4, активны нулём)
__sfr __banked __at(0xFEFE) KBD_CS_V;       // CAPS Z X C V
__sfr __banked __at(0xFDFE) KBD_A_G;
__sfr __banked __at(0xFBFE) KBD_Q_T;
__sfr __banked __at(0xF7FE) KBD_1_5;
__sfr __banked __at(0xEFFE) KBD_0_6;
__sfr __banked __at(0xDFFE) KBD_P_Y;
__sfr __banked __at(0xBFFE) KBD_EN_H;       // ENTER L K J H
__sfr __banked __at(0x7FFE) KBD_SP_B;       // SPACE SYM M N B

// CMOS Gluk: #EFF7 бит 7 — включить, #DFF7 — адрес, #BFF7 — данные
__sfr __banked __at(0xEFF7) CMOS_CTRL;
__sfr __banked __at(0xDFF7) CMOS_ADDR;
__sfr __banked __at(0xBFF7) CMOS_DATA;

// OPL3 (MoonSound): #C4/#C5 — банк 0 (адрес/данные), #C6/#C7 — банк 1.
// Старший байт #FF: с A15 = 0 порт совпадает с декодом #7FFD (02 §11 п.3).
// ПРОВЕРИТЬ НА ЖЕЛЕЗЕ: A0 = 0 у #C4/#C6 совпадает с декодом #FE.
__sfr __banked __at(0xFFC4) OPL3_ADDR0;     // R: статус
__sfr __banked __at(0xFFC5) OPL3_DATA0;
__sfr __banked __at(0xFFC6) OPL3_ADDR1;
__sfr __banked __at(0xFFC7) OPL3_DATA1;

#endif
