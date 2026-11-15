// Аптечка (bmedi.c, банк 35): окно MedikitState (SCR_MEDIKIT).
#ifndef BMEDI_H
#define BMEDI_H

#include <stdint.h>
#include "ui.h"

// Действие BA_USE предметом в руке hand бойца actor (время tu): 1 — это аптечка, окно готово
// (цель — юнит в клетке перед бойцом, иначе он сам); n — юнитов
uint8_t md_open(uint8_t page, uint8_t actor, uint8_t hand, uint8_t tu, uint8_t n) __banked;
uint8_t md_get(sdef_t *s, wdef_t *w) __banked;
void md_text(uint8_t slot, char *buf) __banked;
uint8_t md_event(uint8_t ev, uint8_t arg) __banked;

#endif
