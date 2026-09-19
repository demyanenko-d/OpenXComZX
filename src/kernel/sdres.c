// Ресурсы с SD (банк 12): пакеты, не вшитые в SPG, — BACK.PAK (фоны окон BACKnn.SCR),
// UFOP.PAK (картинки Уфопедии: TFTD 7 МБ), ITEMS.PAK (BIGOBS, куклы брони) и CUTS.PAK
// (слайды заставок), BATUI.PAK (фон и кнопки боя — экран инвентаря). При
// первом запросе: том FAT, файлы OXZ/<игра>/*.PAK, каталоги пакетов — в страницу
// пула. Ресурс читается целиком в один из слотов пула (4 страницы = 64 КБ, картинка
// 320x200) и остаётся там, пока слот не понадобится другому: вытесняется давно не
// запрошенный (фон окна перерисовывается из ресурса на каждом restore, поэтому
// фоны открытых окон должны оставаться в памяти). res_find (res.c) спрашивает
// сюда, если во вшитых пакетах нет.
#include <stdint.h>
#include <string.h>
#include "pages.h"
#include "far.h"
#include "res.h"
#include "fat.h"
#include "music.h"
#include "dbg.h"

#define SLOT_PAGES 4
#define NSLOTS     6
#define NPACKS     8

static const char *const pack_name[2][NPACKS] = {
	{ "OXZ/UFO/BACK.PAK", "OXZ/UFO/UFOP.PAK", "OXZ/UFO/ITEMS.PAK", "OXZ/UFO/CUTS.PAK", "OXZ/UFO/BATUI.PAK", "OXZ/UFO/BATTLE.PAK", "OXZ/UFO/MAPS.PAK", "OXZ/UFO/TILES.PAK" },
	{ "OXZ/TFTD/BACK.PAK", "OXZ/TFTD/UFOP.PAK", "OXZ/TFTD/ITEMS.PAK", "OXZ/TFTD/CUTS.PAK", "OXZ/TFTD/BATUI.PAK", "OXZ/TFTD/BATTLE.PAK", "OXZ/TFTD/MAPS.PAK", "OXZ/TFTD/TILES.PAK" },
};

// Состояние — в памяти банка: описатели восьми пакетов и шести слотов в Win1 уже не
// помещались (Win1 забит доверху). Память банка не обнуляется — чистим в packs_open.
typedef struct {
	fat_file_t pk[NPACKS];
	uint16_t pk_n[NPACKS];          // ресурсов в каталоге (0 — пакета нет)
	uint16_t idx_off[NPACKS];       // смещение каталога пакета в страницах idx_page, в записях
	uint8_t slot_page[NSLOTS];
	uint16_t slot_id[NSLOTS];       // 0 — слот пуст
	uint16_t slot_use[NSLOTS];      // номер последнего запроса (для вытеснения)
	res_t slot_res[NSLOTS];
	uint16_t use_clock;
	uint8_t idx_page, nslots;
} sdst_t;
static sdst_t __at(0xBB00) S;
#define pk        S.pk
#define pk_n      S.pk_n
#define idx_off   S.idx_off
#define idx_page  S.idx_page
#define nslots    S.nslots
#define slot_page S.slot_page
#define slot_id   S.slot_id
#define slot_use  S.slot_use
#define slot_res  S.slot_res
#define use_clock S.use_clock
uint8_t sd_state;                      // 0 — не открывали, 1 — есть, 2 — нет (карты/файлов)

// Каталоги пакетов лежат подряд в двух страницах пула: смещение каждого — idx_off, размер
// по факту (в MAPS.PAK 282 блока, прежние 127 записей на пакет уже не годились).
#define IDX_PAGES 2
#define IDX_MAX   2000

static uint8_t packs_open(void)
{
	uint8_t any = 0;
	memset(&S, 0, sizeof S);
	idx_page = PG_NONE;
	if (fat_mount()) { dbg_puts("sd: no FAT volume\n"); return 0; }
	idx_page = pg_alloc(IDX_PAGES, 1);
	if (idx_page == PG_NONE) return 0;
	for (nslots = 0; nslots < NSLOTS; nslots++) {
		uint8_t p = pg_alloc(SLOT_PAGES, SLOT_PAGES);
		if (p == PG_NONE) break;
		slot_page[nslots] = p;
	}
	if (!nslots) return 0;
	uint32_t idx_pos = 0;
	for (uint8_t p = 0; p < NPACKS; p++) {
		far_t idx = FAR(idx_page, 0) + idx_pos;
		if (fat_open(pack_name[res_game() == 2][p], &pk[p])) continue;
		if (fat_read(&pk[p], 0, idx, 16)) continue;
		uint16_t n = far_word(idx + 6);
		if (n > IDX_MAX) n = IDX_MAX;
		if ((idx_pos + 16 + (uint32_t)n * 16) > (uint32_t)IDX_PAGES * 16384) { dbg_puts("sd: index overflow\n"); break; }
		if (fat_read(&pk[p], 16, idx + 16, (uint32_t)n * 16)) continue;
		pk_n[p] = n;
		idx_off[p] = (uint16_t)(idx_pos >> 4);   // в записях по 16 байт
		idx_pos += 16 + (uint32_t)n * 16;
		any = 1;
		dbg_puts("sd: ");
		dbg_puts(pack_name[res_game() == 2][p]);
		dbg_puts(" ");
		dbg_dec(n);
		dbg_puts("\n");
	}
	return any;
}

// Ресурс целиком в свои страницы, минуя кэш слотов: наборы тайлов миссии занимают десятки
// страниц и должны лежать до конца боя (16 §2.4). Страницы выделяет вызывающий (pg_alloc),
// page — первая из них; возвращает 1 и заполняет r, как sdres_find.
uint8_t sdres_load(uint16_t id, uint8_t page, res_t *r) __banked
{
	uint8_t e[16];
	if (!sd_state) sd_state = packs_open() ? 1 : 2;
	if (sd_state != 1 || !id) return 0;
	for (uint8_t p = 0; p < NPACKS; p++)
		for (uint16_t i = 0; i < pk_n[p]; i++) {
			far_read(FAR(idx_page, 0) + ((uint32_t)idx_off[p] + 1 + i) * 16, e, 16);
			if ((e[0] | (e[1] << 8)) != id) continue;
			uint32_t off = (uint32_t)(e[4] | (e[5] << 8)) << 9;
			uint32_t size = e[6] | ((uint32_t)e[7] << 8) | ((uint32_t)e[8] << 16) | ((uint32_t)e[9] << 24);
			mus_fill();                  // долгое чтение: кольцо музыки не должно опустеть
			if (fat_read(&pk[p], off, FAR(page, 0), size)) return 0;
			mus_fill();
			r->type = e[2];
			r->phys = FAR(page, 0);
			r->size = size;
			r->a = e[10] | (e[11] << 8);
			r->b = e[12] | (e[13] << 8);
			r->c = e[14] | (e[15] << 8);
			return 1;
		}
	return 0;
}

uint8_t sdres_find(uint16_t id, res_t *r) __banked
{
	uint8_t e[16];
	if (!sd_state) sd_state = packs_open() ? 1 : 2;
	if (sd_state != 1 || !id) return 0;
	use_clock++;
	for (uint8_t s = 0; s < nslots; s++)
		if (slot_id[s] == id) { slot_use[s] = use_clock; *r = slot_res[s]; return 1; }
	for (uint8_t p = 0; p < NPACKS; p++)
		for (uint16_t i = 0; i < pk_n[p]; i++) {
			far_read(FAR(idx_page, 0) + ((uint32_t)idx_off[p] + 1 + i) * 16, e, 16);
			if ((e[0] | (e[1] << 8)) != id) continue;
			uint32_t off = (uint32_t)(e[4] | (e[5] << 8)) << 9;
			uint32_t size = e[6] | ((uint32_t)e[7] << 8) | ((uint32_t)e[8] << 16) | ((uint32_t)e[9] << 24);
			if (size > (uint32_t)SLOT_PAGES * 16384) return 0;
			uint8_t s = 0;                   // слот: пустой или давно не запрошенный
			for (uint8_t k = 1; k < nslots; k++)
				if (!slot_id[s]) break;
				else if (!slot_id[k] || (uint16_t)(use_clock - slot_use[k]) > (uint16_t)(use_clock - slot_use[s])) s = k;
			slot_id[s] = 0;
			mus_fill();                  // долгое чтение: кольцо не должно опустеть (20 §8.2)
			if (fat_read(&pk[p], off, FAR(slot_page[s], 0), size)) return 0;
			mus_fill();
			r->type = e[2];
			r->phys = FAR(slot_page[s], 0);
			r->size = size;
			r->a = e[10] | (e[11] << 8);
			r->b = e[12] | (e[13] << 8);
			r->c = e[14] | (e[15] << 8);
			slot_id[s] = id;
			slot_use[s] = use_clock;
			slot_res[s] = *r;
			return 1;
		}
	return 0;
}
