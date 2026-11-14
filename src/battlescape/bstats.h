// Окно характеристик юнита (bstats.c, банк 38): UnitInfoState.
#ifndef BSTATS_H
#define BSTATS_H

#include <stdint.h>

typedef struct {
	uint8_t sol;               // запись солдата (звание и имя), #FF — нет
	uint16_t type;             // правило units (имя пришельца), #FFFF — нет
	uint8_t tu, tu_max, en, en_max, hp, hp_max, stun, mor;
	uint8_t bravery, reactions, firing, throwing, melee, strength, psi_str, psi_skill;
	uint8_t armor[5];
} ustat_t;

void stats_draw(const ustat_t *s) __banked;
// Боевые показатели отряда (units[0..nsold), units.h UNITS_FAR): солдат кампании или отладочные,
// рост и броня типа солдата, броня с прибавками (BattleUnit(Soldier*)). Вынесено из банка 31
void stats_crew(uint8_t nsold) __banked;   // весь экран 320x200

#endif
