// Ресурсы с SD (банк 12): пакеты, не вшитые в SPG, — BACK.PAK (фоны окон BACKnn.SCR),
// UFOP.PAK (картинки Уфопедии: TFTD 7 МБ), ITEMS.PAK (BIGOBS, куклы брони) и CUTS.PAK
// (слайды заставок). При
// первом запросе: том FAT, файлы OXZ/<игра>/*.PAK, каталоги пакетов — в страницу
// пула. Ресурс читается целиком в один из слотов пула (4 страницы = 64 КБ, картинка
// 320x200) и остаётся там, пока слот не понадобится другому: вытесняется давно не
// запрошенный (фон окна перерисовывается из ресурса на каждом restore, поэтому
// фоны открытых окон должны оставаться в памяти). res_find (res.c) спрашивает
// сюда, если во вшитых пакетах нет.
#include <stdint.h>
#include "pages.h"
#include "far.h"
#include "res.h"
#include "fat.h"
#include "dbg.h"

#define SLOT_PAGES 4
#define NSLOTS     6
#define NPACKS     4

static const char *const pack_name[2][NPACKS] = {
	{ "OXZ/UFO/BACK.PAK", "OXZ/UFO/UFOP.PAK", "OXZ/UFO/ITEMS.PAK", "OXZ/UFO/CUTS.PAK" },
	{ "OXZ/TFTD/BACK.PAK", "OXZ/TFTD/UFOP.PAK", "OXZ/TFTD/ITEMS.PAK", "OXZ/TFTD/CUTS.PAK" },
};

static fat_file_t pk[NPACKS];
static uint16_t pk_n[NPACKS];           // ресурсов в каталоге (0 — пакета нет)
static uint8_t idx_page = PG_NONE;
static uint8_t nslots;
static uint8_t slot_page[NSLOTS];
static uint16_t slot_id[NSLOTS];        // 0 — слот пуст
static uint16_t slot_use[NSLOTS];       // номер последнего запроса (для вытеснения)
static res_t slot_res[NSLOTS];
static uint16_t use_clock;
uint8_t sd_state;                      // 0 — не открывали, 1 — есть, 2 — нет (карты/файлов)

// Каталоги пакетов — подряд в странице пула: пакет p с смещения p * 4 КБ (до 255 ресурсов)
#define IDX_STEP 4096
#define IDX_MAX  255

static uint8_t packs_open(void)
{
	uint8_t any = 0;
	if (fat_mount()) { dbg_puts("sd: no FAT volume\n"); return 0; }
	idx_page = pg_alloc(1, 1);
	if (idx_page == PG_NONE) return 0;
	for (nslots = 0; nslots < NSLOTS; nslots++) {
		uint8_t p = pg_alloc(SLOT_PAGES, SLOT_PAGES);
		if (p == PG_NONE) break;
		slot_page[nslots] = p;
	}
	if (!nslots) return 0;
	for (uint8_t p = 0; p < NPACKS; p++) {
		far_t idx = FAR(idx_page, (uint16_t)p * IDX_STEP);
		if (fat_open(pack_name[res_game() == 2][p], &pk[p])) continue;
		if (fat_read(&pk[p], 0, idx, 16)) continue;
		uint16_t n = far_word(idx + 6);
		if (n > IDX_MAX) n = IDX_MAX;
		if (fat_read(&pk[p], 16, idx + 16, (uint32_t)n * 16)) continue;
		pk_n[p] = n;
		any = 1;
		dbg_puts("sd: ");
		dbg_puts(pack_name[res_game() == 2][p]);
		dbg_puts(" ");
		dbg_dec(n);
		dbg_puts("\n");
	}
	return any;
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
			far_read(FAR(idx_page, (uint16_t)p * IDX_STEP + 16) + (uint32_t)i * 16, e, 16);
			if ((e[0] | (e[1] << 8)) != id) continue;
			uint32_t off = (uint32_t)(e[4] | (e[5] << 8)) << 9;
			uint32_t size = e[6] | ((uint32_t)e[7] << 8) | ((uint32_t)e[8] << 16) | ((uint32_t)e[9] << 24);
			if (size > (uint32_t)SLOT_PAGES * 16384) return 0;
			uint8_t s = 0;                   // слот: пустой или давно не запрошенный
			for (uint8_t k = 1; k < nslots; k++)
				if (!slot_id[s]) break;
				else if (!slot_id[k] || (uint16_t)(use_clock - slot_use[k]) > (uint16_t)(use_clock - slot_use[s])) s = k;
			slot_id[s] = 0;
			if (fat_read(&pk[p], off, FAR(slot_page[s], 0), size)) return 0;
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
