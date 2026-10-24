// Расстановка пришельцев (deploy.c, банк 31 — вызывается из экрана боя напрямую, тот же банк).
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

// Поставить пришельцев после n юнитов массива u (до max); вернёт новое число юнитов.
// sx, sy — размер карты. Листы брони грузятся в страницы пула — отдать aliens_free.
uint8_t aliens_deploy(unit_t *u, uint8_t n, uint8_t max, adep_t *q, uint8_t sx, uint8_t sy);
void aliens_free(adep_t *q);

#endif
