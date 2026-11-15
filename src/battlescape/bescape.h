// Бегство мирного жителя от пришельцев (bescape.c, банк 35).
#ifndef BESCAPE_H
#define BESCAPE_H

#include <stdint.h>
#include "bai.h"

// Юнит a (мирный): если его видит пришелец — путь прочь от них (до max шагов, буфер — на стеке
// вызывающего: память банка ИИ отсюда не видна); 0 — не видят или некуда
uint8_t ai_escape(const aictx_t *c, uint8_t a, uint8_t *path, uint8_t max) __banked;

#endif
