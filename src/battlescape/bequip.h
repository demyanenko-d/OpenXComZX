// Снаряжение отряда из груза корабля (bequip.c, банк 15): loadWeapons и autoEquip оригинала.
#ifndef BEQUIP_H
#define BEQUIP_H

#include <stdint.h>

// Запрос (на стеке вызывающего): who[u] — запись солдата бойца без раскладки (ему autoEquip),
// #FF — не трогать; nb — занято записей bitem_t; gx, gy, gz — клетка земли корабля
typedef struct {
	uint8_t page, nb, craft, n, gx, gy, gz;
	const uint8_t *who;
} beq_t;

// Груз корабля craft — бойцам без раскладки и на землю; вернёт новое число записей bitem_t
uint8_t be_auto(const beq_t *q) __banked;

// Предметы пришельцев (deployAliens): itemSets записи развёртывания на их уровне, встроенное и
// живое оружие; юниты — TL_AUNITS (номера с first), развёртывание — TL_ADEP, число — TL_ANUM.
// nb — занято записей bitem_t; вернёт новое число
uint8_t be_aliens(uint8_t page, uint8_t nb, uint8_t first) __banked;

#endif
