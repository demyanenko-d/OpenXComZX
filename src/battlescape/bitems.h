// Предметы боя (bitems.c, банк 33): BattleItem оригинала в виде inv_t кампании — у кого
// (юнит или земля), где (секция invs и клетка), что, вставленная обойма и патроны в ней.
#ifndef BITEMS_H
#define BITEMS_H

#include <stdint.h>

#define BI_MAX     256
#define BI_GROUND  0xFE        // owner: лежит на земле, клетка (x, y, z)
#define BI_NONE    0xFF        // owner: запись свободна
typedef struct {
	uint8_t owner, slot, x, y; // юнит (или BI_GROUND / BI_NONE); секция invs и клетка в ней
	uint16_t item, ammo;       // правило items; вставленная обойма (#FFFF — нет)
	uint8_t rounds, z;         // патронов (в оружии — в его обойме); этаж — для лежащего на земле
} bitem_t;

// BattleType (RuleItem.h)
#define BT_NONE     0
#define BT_FIREARM  1
#define BT_AMMO     2
#define BT_MELEE    3
#define BT_GRENADE  4
#define BT_PROXIMITYGRENADE 5
#define BT_MEDIKIT  6

// Снаряжение отряда: sol[i] — запись солдата кампании юнита i (#FF — нет). Предметы бойцов —
// копия их раскладки inv_t (placeItemByLayout); бойцу без раскладки в отладочном бою — винтовка
// с обоймой в правую руку. page — рабочая страница боя. Вернёт число предметов.
uint8_t bi_crew(uint8_t page, const uint8_t *sol, uint8_t n) __banked;
// Предмет в руке юнита (right — правая): 1 — есть, копия в *out
uint8_t bi_hand(uint8_t page, uint8_t unit, uint8_t right, bitem_t *out) __banked;
// Выстрел: патрон из обоймы оружия в руке (BattleItem::spendBullet); вернёт, сколько осталось
uint8_t bi_spend(uint8_t page, uint8_t unit, uint8_t right) __banked;

#endif
