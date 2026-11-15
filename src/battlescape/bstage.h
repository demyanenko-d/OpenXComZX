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
// drop — прервали в следующую фазу: оставшиеся вне зоны выбывают (goToTimeOut) — пропали без вести
uint8_t st_exit(uint8_t page, uint8_t nsold, uint8_t drop) __banked;

// Окно AbortMissionState (SCR_ABORT; вынесено из банка 31): раскладка — по тому, есть ли зона
// выхода; тексты DYN(0..2) — бойцы в корабле (у входа), снаружи, в зоне выхода
#include "ui.h"
void st_abort_text(uint8_t slot, char *buf, uint8_t page, uint8_t nsold) __banked;
// Зона клетки юнита i (BattleUnit::isInExitArea): 0 — корабль (у карты без корабля — START_POINT),
// 1 — снаружи, 2 — END_POINT
uint8_t st_zone(uint8_t page, uint8_t i) __banked;
// Окно BriefingState (SCR_BRIEFING) развёртывания dep: тексты DYN(0..3) — заголовок, цель корабля
// craft, корабль, брифинг. st_brief_stage(1) — брифинг следующей фазы поверх боя (OK — назад в бой),
// 0 — перед боем (OK — бой)
void st_brief_stage(uint8_t on) __banked;
// Окно «Hidden Movement» поверх места карты (вынесено из банка 31)
void st_hidden_draw(void) __banked;
// Окна SCR_ABORT и SCR_BRIEFING: раскладка (page — рабочая страница боя, dep — развёртывание)
uint8_t st_get(uint8_t id, sdef_t *s, wdef_t *w, uint8_t page, uint16_t dep) __banked;
// Начало миссии с геоскейпа: развёртывание (у НЛО — по его типу), брифинг — перед боем
uint16_t st_mission(uint8_t utype, uint16_t deploy) __banked;
void st_brief_text(uint8_t slot, char *buf, uint16_t dep, uint16_t craft) __banked;
uint8_t st_brief_event(uint8_t ev, uint8_t arg, uint16_t dep) __banked;

#endif
