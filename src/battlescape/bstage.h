// Фазы миссии (bstage.c, банк 32): многофазные развёртывания TFTD (судно, колония, Т'лет —
// alienDeployments.nextStage; BattlescapeState::finishBattle, BattlescapeGenerator::nextStage) и
// развёртывание по типу НЛО (вынесено из банка 31 — он полон).
#ifndef BSTAGE_H
#define BSTAGE_H

#include <stdint.h>

#define SPECIAL_END_POINT 13       // SpecialTileType END_POINT (Mod/MapData.h): зона выхода в фазу

// Развёртывание НЛО типа utype — то, что называется как сам НЛО (alienDeployments STR_SMALL_SCOUT
// и т. п., BattlescapeGenerator::run: getDeployment(ufo->getRules()->getType())); #FFFF — нет
uint16_t st_ufo_deploy(uint8_t utype) __banked;
// Начало хода стороны (BattleUnit::prepareNewTurn -> recoverTimeUnits): время, энергия, выбор
void st_side_prepare(uint8_t side, uint8_t n) __banked;
// Живых пришельцев (вернёт) и бойцов X-COM (*xc) — конец боя (checkForCasualties)
uint8_t st_alive(uint8_t n, uint8_t *xc) __banked;
// Следующая фаза развёртывания dep (#FFFF — нет)
uint16_t st_next(uint16_t dep) __banked;
// Живых бойцов (из nsold) на полу END_POINT (BattleUnit::isInExitArea(END_POINT)); page — рабочая.
// drop — прервали в следующую фазу: оставшиеся вне зоны выбывают (goToTimeOut; у нас — гибель,
// «пропал без вести» разбор пока не различает)
uint8_t st_exit(uint8_t page, uint8_t nsold, uint8_t drop) __banked;

// Окно AbortMissionState (SCR_ABORT; вынесено из банка 31): раскладка — по тому, есть ли зона
// выхода; тексты DYN(0..2) — бойцы в корабле (у входа), снаружи, в зоне выхода
#include "ui.h"
uint8_t st_abort_get(sdef_t *s, wdef_t *w, uint8_t page, uint16_t dep) __banked;
void st_abort_text(uint8_t slot, char *buf, uint8_t page, uint8_t nsold) __banked;

#endif
