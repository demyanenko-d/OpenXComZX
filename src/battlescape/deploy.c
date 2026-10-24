// Расстановка пришельцев на карте миссии (банк 31, вместе с экраном боя): порт
// BattlescapeGenerator::deployAliens / addAlien (REF/OpenXcom/src/Battlescape/BattlescapeGenerator.cpp:1045,
// :1178) и SavedBattleGame::getSpawnNode (REF/OpenXcom/src/Savegame/SavedBattleGame.cpp:1245).
//
// Состав — запись alienDeployments: по рангу alienRank число lowQty..highQty (по сложности) + dQty
// и extraQty случайно, доля снаружи НЛО percentageOutsideUfo; кто это — член расы с этим рангом
// (alienRaces.members), его характеристики — units и броня armors. Место — узел маршрута RMP
// (mapgen.h bnode_t): ранги узлов перебираются по таблице Node::nodeRank, среди подходящих
// берутся узлы с наибольшим priority и из них — случайный. Предметы (itemSets) — со стрельбой.
#include <stdint.h>
#include <string.h>
#include "memmap.h"
#include "res.h"
#include "res_ids.h"
#include "rules.h"
#include "far.h"
#include "pages.h"
#include "game.h"
#include "state.h"
#include "dbg.h"
#include "mapgen.h"
#include "units.h"
#include "doors.h"
#include "loader.h"
#include "deploy.h"

// Node::nodeRank (REF/OpenXcom/src/Savegame/Node.cpp:57): в каком порядке ранг пришельца ищет ранги
// узлов; 0 — разведчик (снаружи НЛО) — запасной у всех
static const uint8_t node_rank[8][7] = {
	{ 4, 3, 5, 8, 7, 2, 0 }, { 4, 3, 5, 8, 7, 2, 0 }, { 5, 4, 3, 2, 7, 8, 0 }, { 7, 6, 2, 8, 3, 4, 0 },
	{ 3, 4, 5, 2, 7, 8, 0 }, { 2, 5, 3, 4, 6, 8, 0 }, { 2, 5, 3, 4, 6, 8, 0 }, { 2, 5, 3, 4, 6, 8, 0 },
};
#define NT_FLYING 0x01                   // Node::TYPE_FLYING: только летающим
#define NT_SMALL  0x02                   // Node::TYPE_SMALL: только размером в клетку

// Рост характеристик пришельцев по сложности (difficulty.rul statGrowthMultipliers — проценты на
// единицу difficultyCoefficient) и множитель меткости и брони (aimAndArmorMultipliers, в сотых):
// REF/OpenXcom/bin/standard/xcom1/difficulty.rul и xcom2/difficulty.rul. В правилах движка их пока
// нет (14_todo) — берутся по игре. Порядок — как в rstats_t.
static const uint8_t growth_ufo[11] = { 4, 4, 0, 0, 6, 6, 0, 2, 4, 4, 4 };
static const uint8_t growth_tftd[11] = { 8, 8, 0, 0, 6, 6, 0, 4, 4, 4, 8 };
static const uint8_t aim_ufo[5] = { 50, 100, 100, 100, 100 };
static const uint8_t aim_tftd[5] = { 40, 70, 100, 110, 120 };

// Узлы, пригодные для появления (priority > 0), — в память банка: перебор по дальней памяти на
// каждого пришельца и каждый ранг стоил бы секунды
typedef struct { uint8_t x, y, z, type, rank, priority; uint16_t idx; } snode_t;
#define SN_MAX 300
static snode_t __at(0xB600) sn[SN_MAX];   // до #BF60
static uint16_t nsn;

static uint8_t cell_free(const unit_t *u, uint8_t n, uint8_t x, uint8_t y, uint8_t z, uint8_t size)
{
	for (uint8_t i = 0; i < n; i++) {
		const unit_t *v = &u[i];
		if (!v->alive || v->z != z) continue;
		uint8_t vs = v->size;
		// клетки пересекаются: квадраты [x, x + size) и [v->x, v->x + vs)
		if (x + size > v->x && v->x + vs > x && y + size > v->y && v->y + vs > y) return 0;
	}
	return 1;
}

// SavedBattleGame::getSpawnNode: узел ранга rank с наибольшим priority, свободный
static int16_t spawn_node(const unit_t *u, uint8_t n, uint8_t rank, uint8_t size, uint8_t fly,
			  uint8_t sx, uint8_t sy)
{
	uint8_t best = 0;
	uint16_t cnt = 0;
	for (uint16_t i = 0; i < nsn; i++) {
		const snode_t *s = &sn[i];
		if (s->rank != rank || ((s->type & NT_SMALL) && size != 1) || ((s->type & NT_FLYING) && !fly)) continue;
		if (s->x + size > sx || s->y + size > sy || !cell_free(u, n, s->x, s->y, s->z, size)) continue;
		if (s->priority > best) { best = s->priority; cnt = 0; }
		if (s->priority == best) cnt++;
	}
	if (!cnt) return -1;
	uint16_t k = rng_next() % cnt;
	for (uint16_t i = 0; i < nsn; i++) {
		const snode_t *s = &sn[i];
		if (s->rank != rank || s->priority != best || ((s->type & NT_SMALL) && size != 1) || ((s->type & NT_FLYING) && !fly)) continue;
		if (s->x + size > sx || s->y + size > sy || !cell_free(u, n, s->x, s->y, s->z, size)) continue;
		if (!k--) return (int16_t)i;
	}
	return -1;
}

// Лист собранных кадров брони: уже загружен — его номер, иначе загрузить (до ASH_MAX)
static uint8_t sheet_of(adep_t *q, uint16_t res, uint8_t size)
{
	for (uint8_t s = 0; s < q->nsh; s++)
		if (q->shres[s] == res) return (uint8_t)(s + 1);
	if (!res || q->nsh >= ASH_MAX) return 0;
	uint8_t s = q->nsh;
	// страницы — через стек: память банка 31 (q) на время вызова банка 30 не видна
	uint8_t pg = PG_NONE, np = 0;
	uint8_t ok = sheet_load(res, &pg, &np, size > 1 ? 32 : ASH_N, 0,
				FAR(q->page + 1, TL_ASH) + (uint16_t)s * ASH_N * TE_SIZE);
	q->shp[s] = pg; q->shn[s] = np;
	if (!ok) { if (pg != PG_NONE) pg_free(pg, np); return 0; }
	q->shres[s] = res;
	q->nsh++;
	return (uint8_t)(s + 1);
}

void aliens_free(adep_t *q)
{
	for (uint8_t s = 0; s < q->nsh; s++)
		if (q->shp[s] != PG_NONE) pg_free(q->shp[s], q->shn[s]);
	q->nsh = 0;
}

// Пришелец: характеристики units + armors, поправка по сложности (BattleUnit::adjustStats).
// Вернёт ресурс собранного листа его брони (0 — нет)
static uint16_t alien_make(unit_t *a, uint16_t type, uint8_t rank, uint8_t diff, uint8_t coef)
{
	rtab_t t;
	r_units_t ur;
	r_armors_t ar;
	memset(a, 0, sizeof *a);
	a->type = type; a->rank = rank; a->faction = FACTION_HOSTILE;
	a->node = 0xFFFF;
	a->alive = 1; a->size = 1;
	uint16_t sheet = 0;
	if (!rtab_open(RES_RULE_UNITS, &t) || type >= t.n) return 0;
	rtab_get(&t, type, &ur);
	uint8_t st[11];
	memcpy(st, &ur.stats, 11);
	if (rtab_open(RES_RULE_ARMORS, &t) && ur.armor < t.n) {
		rtab_get(&t, ur.armor, &ar);
		const uint8_t *as = (const uint8_t *)&ar.stats;   // броня тоже меняет характеристики
		for (uint8_t i = 0; i < 11; i++) st[i] += as[i];
		a->armor[0] = ar.front_armor; a->armor[1] = a->armor[2] = ar.side_armor;
		a->armor[3] = ar.rear_armor; a->armor[4] = ar.under_armor;
		a->size = ar.size ? ar.size : 1;
		a->fly = ar.movement_type == 1;
		sheet = ar.sheet;
	}
	const uint8_t *g = res_game() == 2 ? growth_tftd : growth_ufo;
	const uint8_t *m = res_game() == 2 ? aim_tftd : aim_ufo;
	for (uint8_t i = 0; i < 11; i++) st[i] += (uint8_t)((uint16_t)g[i] * coef * st[i] / 100);
	st[5] = (uint8_t)((uint16_t)st[5] * m[diff] / 100);                  // firing
	for (uint8_t i = 0; i < 5; i++) a->armor[i] = (uint8_t)((uint16_t)a->armor[i] * m[diff] / 100);
	a->tu = a->tu_max = st[0];
	a->en = a->en_max = st[1];
	a->hp = a->hp_max = st[2];
	a->bravery = st[3]; a->reactions = st[4]; a->firing = st[5]; a->throwing = st[6];
	a->strength = st[7]; a->psi_str = st[8]; a->psi_skill = st[9]; a->melee = st[10];
	a->mor = 100;
	return sheet;
}

uint8_t aliens_deploy(unit_t *u, uint8_t n, uint8_t max, adep_t *q, uint8_t sx, uint8_t sy)
{
	rtab_t t;
	r_alienDeployments_t d;
	uint8_t diff = ST->difficulty;
	if (diff > 4) diff = 4;
	uint8_t coef = gv.diff_coef[diff];       // difficultyCoefficient (difficulty.rul)
	if (!rtab_open(RES_RULE_ALIENDEPLOYMENTS, &t) || !t.n) return n;
	uint16_t dep = q->deploy < t.n ? q->deploy : 0;
	rtab_get(&t, dep, &d);
	uint16_t race = d.race != RNONE ? d.race : q->race != 0xFFFF ? q->race : 0;
	int16_t data[6 * 12];
	uint8_t nd = (uint8_t)(d.data.n / 6);
	if (nd > 12) nd = 12;
	if (nd) rtab_tail(&t, d.data.off, data, (uint16_t)nd * 12);
	uint16_t members[8];
	uint8_t nm = 0;
	if (rtab_open(RES_RULE_ALIENRACES, &t) && race < t.n) {
		r_alienRaces_t rr;
		rtab_get(&t, race, &rr);
		nm = rr.members.n > 8 ? 8 : rr.members.n;
		if (nm) rtab_tail(&t, rr.members.off, members, (uint16_t)nm * 2);
	}
	// узлы появления — в память банка
	far_t np;
	uint16_t nn = mapgen_nodes(&np);
	nsn = 0;
	for (uint16_t i = 0; i < nn && nsn < SN_MAX; i++) {
		bnode_t b;
		far_read(np + (uint32_t)i * sizeof b, &b, sizeof b);
		if (b.x == 0xFF || !b.priority) continue;
		snode_t *s = &sn[nsn++];
		s->x = b.x; s->y = b.y; s->z = b.z; s->type = b.type; s->rank = b.rank; s->priority = b.priority;
		s->idx = i;
	}
	dbg_puts("aliens: deploy "); dbg_dec(dep); dbg_puts(" race "); dbg_dec(race);
	dbg_puts(" nodes "); dbg_dec(nn); dbg_puts(" spawn "); dbg_dec(nsn);
	uint8_t n0 = n;
	for (uint8_t e = 0; e < nd; e++) {
		const int16_t *r = data + e * 6;
		uint8_t rank = (uint8_t)r[0] > 7 ? 7 : (uint8_t)r[0];
		if (rank >= nm || members[rank] == RNONE) continue;
		uint8_t low = (uint8_t)r[1], high = (uint8_t)r[2], dq = (uint8_t)r[3], ex = (uint8_t)r[4];
		uint8_t delta = (uint8_t)rng_range(0, dq), qty;
		if (diff < 2) qty = low + delta;                         // DIFF_VETERAN
		else if (diff < 4) qty = low + (high - low) / 2 + delta; // DIFF_SUPERHUMAN
		else qty = high + delta;
		qty += (uint8_t)rng_range(0, ex);
		for (uint8_t k = 0; k < qty && n < max; k++) {
			uint8_t outside = (uint8_t)(rng_next() % 100) < (uint8_t)r[5] && q->ufo;
			unit_t *a = &u[n];
			uint16_t res = alien_make(a, members[rank], rank, diff, coef);
			int16_t s = -1;
			for (uint8_t i = 0; i < 7 && s < 0; i++)
				s = spawn_node(u, n, outside ? 0 : node_rank[rank][i], a->size, a->fly, sx, sy);
			if (s < 0) continue;                 // узла нет — пришельца нет (как в оригинале)
			const snode_t *p = &sn[s];
			a->x = p->x; a->y = p->y; a->z = p->z; a->node = p->idx;
			// лицом к кораблю, если он близко и повезло (20 % на единицу сложности), иначе — куда
			// попало (faceWindow — к окну — пока нет)
			int16_t cx = (int16_t)u[0].x - a->x, cy = (int16_t)u[0].y - a->y;
			if (n0 && cx * cx + cy * cy <= 400 && rng_percent((uint8_t)(20 * coef))) a->dir = dir_to(cx, cy);
			else a->dir = (uint8_t)(rng_next() & 7);
			a->sheet = sheet_of(q, res, a->size);
			a->seen = 0;                         // виден, когда его заметит отряд (vision.c)
			n++;
		}
	}
	dbg_puts(" aliens "); dbg_dec(n - n0); dbg_puts("\n");
	return n;
}
