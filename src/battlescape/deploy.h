// Расстановка пришельцев (deploy.c, банк 34): отряд берёт из рабочей страницы (loader.h TL_UNITS),
// пришельцев и мирных кладёт в третью (TV_AUNITS) — память банка экрана боя отсюда не видна.
#ifndef DEPLOY_H
#define DEPLOY_H

#include <stdint.h>
#include "units.h"
#include "loader.h"

typedef struct {
	uint16_t deploy;           // alienDeployments (#FFFF — первое: отладочный бой)
	uint16_t race;             // alienRaces миссии (#FFFF — из развёртывания, иначе первая)
	uint8_t ufo;               // на карте есть НЛО (иначе снаружи никого не ставят — все «внутри»)
	uint8_t page;              // рабочая страница боя (листы кадров — во второй, TL_ASH)
	uint8_t nsh;               // загруженные листы брони: ресурс, страницы
	uint16_t shres[ASH_MAX];
	uint8_t shp[ASH_MAX], shn[ASH_MAX];
} adep_t;

// поля — как в adep_t; terrain — террейн карты (его civilianTypes, ресурс CIVTYPES)
typedef struct { uint16_t deploy, race; uint8_t ufo, page; uint16_t terrain; } adreq_t;
// Поставить пришельцев к отряду из nsold бойцов, за ними — мирных жителей (civilians
// развёртывания, FACTION_NEUTRAL); вернёт число тех и других (записи — TV_AUNITS).
// sx, sy — размер карты. Листы брони грузятся в страницы пула — отдать aliens_free.
uint8_t aliens_deploy(const adreq_t *r, uint8_t nsold, uint8_t sx, uint8_t sy) __banked;
void aliens_free(void) __banked;
void aliens_reset(void) __banked;          // вход в бой: листов нет (память банка не обнулена)

#endif
