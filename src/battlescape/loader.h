// Графика миссии: тайлсеты наборов, таблица частей и листы спрайтов (loader.c, банк 30).
// Банк экрана боя (28) забит отрисовкой, поэтому загрузка живёт отдельно, а результат
// кладётся в дальнюю память — оттуда экран боя забирает его к себе одним far_read.
#ifndef BAT_LOADER_H
#define BAT_LOADER_H

#include <stdint.h>
#include "far.h"

#define TE_SIZE   8            // байт на кадр в таблице «готово для блита» (17 §2)
#define TL_SETS   16           // наборов MCD на миссию (как MAX_SETS у генератора)

// Таблицы графики миссии лежат во ВТОРОЙ рабочей странице (первая занята поиском пути,
// раскладка обеих — pathfind.h). Экран боя переносит их оттуда к себе в банк.
#define TL_TILES  0            // 256 строк по TE_SIZE — вид частей для блита
#define TL_YOFS   2048         // 256 байт — вертикальное смещение части (MCD.P_Level)
#define TL_SHEET  2304         // до 232 кадров листа спрайтов (1856 байт, до #1040): лист бойца
                               // с ходьбой; кадры ходьбы (40..) бой читает отсюда при сборке вида
#define TL_ARROW  4160         // стрелка над выбранным бойцом: 9 строк по 10 байт (источник DMA)
#define TL_ARROWE 4256         //   её запись для блита (TE_SIZE)
#define TL_CURSOR 4264         // 9 кадров курсора клетки: рамка и прицел (TE_SIZE каждый, до #10F0)
#define TL_UDIDX  4608         // 256 байт: часть «створка в движении» -> номер типа двери НЛО
#define TL_UDFR   4864         // кадры 1..7 створок по типам дверей НЛО: TL_UDMAX x 7 x TE_SIZE
#define TL_UDMAX  24
#define TL_UNITS  6208         // отряд в виде unit_t (units.h), MAX_UNITS записей по 48 байт (до #1A80)
#define TL_RUNITS 6848         // юниты для вида: runit_t (bview.h), MAX_BUNITS по 8 байт (до #1C00)
// Собранные кадры юнитов по броне (UNITS.PAK ASHEET_*, конвертер собирает их как UnitSprite):
// лист s (1..ASH_MAX) — ASH_N записей с (s - 1) * ASH_N. Броня 1x1: 0..7 стоя по направлению,
// 8 + d*8 + фаза — ходьба, 72 + d — на колене; 2x2: d*4 + четверть (стоя; ходьбы пока нет).
#define TL_ASH    7168
#define ASH_N     80
#define ASH_MAX   6            // до #2B00
#define TL_BIGW   11264        // 256 байт: MCD.Big_Wall части по номеру (Pathfinding::bigWallTypes)
#define TL_BITEMS 11520        // предметы боя: bitem_t (bitems.h), BI_MAX по 10 байт (до #3700)
#define TL_AUNITS 14080        // пришельцы от расстановки (deploy.c): unit_t, до 28 записей (до #3C40)
// Оружие в руках бойцов X-COM (UnitSprite::drawRoutine0): кадры листа 232..263 — руки, держащие
// предмет; у бойца i — два байта слотов кадров предмета (правая, левая): 0 — пусто, иначе
// номер слота + 1, бит 7 — двуручное; кадры предметов HANDOB по слотам — по 8 направлений,
// пиксели — в своей странице (номер — TL_HANDPG), её заводит bload.c, заполняет bitems.c
#define TL_HARMS  15424        // 32 кадра рук с оружием (TE_SIZE)
#define TL_UHS    15680        // MAX_UNITS x 2 байта: слоты предметов в руках
#define TL_HANDPG 15704        // страница пикселей кадров предметов (PG_NONE — нет)
#define TL_HAND   15712        // кадры предметов: слот s, направление d — запись s * 8 + d (до #4000)
#define HAND_MAX  10

// Третья рабочая страница — части миссии для выстрелов и разрушений (по номеру части):
#define TV_LOFT   0            // 12 байт: MCD.LOFT — маска LOFTEMPS на каждые 2 вокселя высоты
#define TV_PROPS  3072         // 12 байт: armor, heBlock, dieMcd (часть миссии, 0 — нет), flammable,
#define TV_PSIZE  12           //   footstep, heType, heStrength, smokeBlockage, fuel, lightSource,
                               //   targetType, tuFly (до #1800)
// Выстрел на экране (bfx.c): спрайты поверх вида — число (байт) и записи с TV_FX + 2: мировые x, y
// и запись для блита (12 байт); кадры частиц пуль по типам (ключ — первый кадр BULLETS, 35 частиц
// и 35 теней) и вспышек попадания (ключ — hitAnimation, 10 кадров SMOKE.PCK); пиксели — в своей
// странице (номер — TV_FXPG, её заводит bload.c)
#define TV_FX     6144
#define FX_MAX    72
#define TV_FXB    7040         // FXB_MAX x (2 + 70 x TE_SIZE)
#define FXB_MAX   4
#define TV_FXH    9344         // FXH_MAX x (2 + 10 x TE_SIZE)
#define FXH_MAX   4
#define TV_FXPG   9728

typedef struct {
	uint16_t set[TL_SETS];     // ресурсы MCDSET наборов миссии
	uint16_t tset[TL_SETS];    // ресурсы TILESET тех же наборов
	uint8_t ns;
	uint8_t used[32];          // какие части реально попали на карту (mapgen_used)
	uint8_t page;              // рабочая страница: туда пишутся таблицы
} tiles_req_t;

// Загрузить тайлсеты и собрать таблицы частей миссии: вид для блита (TL_TILES),
// смещения (TL_YOFS), цену прохода и двери (pathfind.h). Возвращает число частей;
// выданные страницы пула — в pages/nps (их освобождает вызывающий), дверей — в ndoors.
uint16_t tiles_load(const tiles_req_t *q, uint8_t *pages, uint8_t *nps,
		    uint8_t *npages, uint8_t *ndoors) __banked;

// Лист спрайтов (SPRSET) в свою страницу пула, таблица кадров — по адресу dst.
// want == 0 — кадры 0..n-1 подряд, иначе n номеров из want (буфер вызывающего: он
// должен лежать в стеке, страница банка вызывающего при вызове отключена).
uint8_t sheet_load(uint16_t res, uint8_t *page, uint8_t *np, uint8_t n,
		   const uint8_t *want, far_t dst) __banked;
// Ещё n кадров (first..) листа, уже загруженного sheet_load в page, — таблицей по адресу dst
void sheet_more(uint8_t page, uint16_t first, uint8_t n, far_t dst) __banked;

#endif
