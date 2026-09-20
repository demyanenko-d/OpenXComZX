// Ресурсы из пакетов *.PAK (формат — project_docs/09_converter.md §5).
// Пакеты лежат в страницах с начала страницы; таблица страниц — tmp/build/packs.s
// (сейчас пакеты вшиты в SPG; позже — загрузка с SD в те же страницы).
#ifndef RES_H
#define RES_H

#include <stdint.h>
#include "far.h"

typedef struct {
	far_t phys;          // начало данных ресурса
	uint32_t size;
	uint16_t a, b, c;    // параметры (ширина, высота, число кадров …)
	uint8_t type;
} res_t;

#define RT_IMG8   1
#define RT_SPRSET 2
#define RT_PAL    3
#define RT_FONT   4
#define RT_STR    5
#define RT_TABLE  6
#define RT_MUSIC  7
#define RT_BLOB   8

uint8_t res_find(uint16_t id, res_t *r);   // 1 — найден
uint8_t res_game(void);                    // 1 — UFO, 2 — TFTD (заголовок пакета)

// Ресурсы с SD мимо кэша слотов (банк 12, sdres.c): для боя — наборы тайлов миссии,
// которые не влезают в слот и должны лежать до конца боя (16 §2.4).
uint32_t sdres_size(uint16_t id) __banked;
uint8_t sdres_load(uint16_t id, uint8_t page, res_t *r) __banked;

// Таблицы правил (rules.h: rtable_t + записи): открыть, прочитать запись целиком
// в ближнюю память (dst не меньше size) или слово по смещению в записи.
typedef struct { far_t base; uint16_t n, size, tail; } rtab_t;
uint8_t rtab_open(uint16_t id, rtab_t *t);
void rtab_get(const rtab_t *t, uint16_t i, void *dst);
uint16_t rtab_word(const rtab_t *t, uint16_t i, uint8_t off);
// Элементы списка записи (rlist_t: смещение в хвосте + число) -> dst, bytes байт.
void rtab_tail(const rtab_t *t, uint16_t off, void *dst, uint16_t bytes);

#endif
