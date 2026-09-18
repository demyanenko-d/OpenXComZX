// Сохранение и загрузка состояния (банк 7). Сохранение — страницы состояния
// как есть: state_t (STATE_PAGE, до 16 КБ, хвост страницы — нули) и солдаты
// (STATE_PAGE + 1, nsoldiers записей). Куда: служба BIOS (эмулятор: файл
// tmp/saves/<игра>/SLOTnn.SAV; позже — файл-слот на SD), без неё — страницы
// пула (живут, пока включено питание). 12_game_state.md §4.
#include <stdint.h>
#include <string.h>
#include "memmap.h"
#include "pages.h"
#include "far.h"
#include "res.h"
#include "bios.h"
#include "state.h"
#include "game.h"

static uint8_t ram_slot[SAVE_SLOTS];     // первая из 3 страниц пула, 0 — пусто

// Сохраняются подряд: страница ядра, вся страница солдат и занятая часть инвентаря
// (страницы смежны в дальней памяти, см. state.h)
static uint16_t blob_size(void)
{
	return 32768u + (uint16_t)(ST->ninv * sizeof(inv_t));
}

// Сумма всего после заголовка: s = s*2 + b (циклически), по дальней памяти.
static uint16_t checksum(uint16_t size)
{
	uint8_t buf[64];
	uint16_t sum = 0x5A5A;
	far_t p = FAR(STATE_PAGE, sizeof(st_header_t));
	for (uint16_t left = size - sizeof(st_header_t); left;) {
		uint16_t n = left > sizeof buf ? sizeof buf : left;
		far_read(p, buf, n);
		for (uint16_t i = 0; i < n; i++)
			sum = (uint16_t)((sum << 1) | (sum >> 15)) + buf[i];
		p += n;
		left -= n;
	}
	return sum;
}

uint8_t st_save(uint8_t slot, const char *name) __banked
{
	if (slot >= SAVE_SLOTS) return SAVE_BADSLOT;
	ST->iron_slot = slot;                        // Ironman сохраняется только сюда
	ST->globe.lon = (int32_t)((uint32_t)ctx.globe_lon << 16);   // центр глобуса (SavedGame globeLon/Lat)
	ST->globe.lat = (int32_t)ctx.globe_lat << 16;
	st_header_t *h = &ST->hdr;
	uint16_t size = blob_size();
	h->magic = ST_MAGIC;
	h->version = ST_VERSION;
	h->game = res_game();
	h->difficulty = ST->difficulty;
	h->ironman = ST->ironman;
	strncpy(h->name, name, SAVE_NAME - 1);
	h->name[SAVE_NAME - 1] = 0;
	h->year = ST->year; h->month = ST->month; h->day = ST->day;
	h->hour = ST->hour; h->minute = ST->minute;
	h->size = size;
	h->sum = checksum(size);
	uint8_t r = bios_file(BIOS_WRITE, slot, FAR(STATE_PAGE, 0), size, 0);
	if (r == BIOS_OK) return SAVE_OK;
	if (r != BIOS_NONE) return SAVE_IOERR;
	if (!ram_slot[slot]) {
		ram_slot[slot] = pg_alloc(3, 1);
		if (ram_slot[slot] == PG_NONE) { ram_slot[slot] = 0; return SAVE_IOERR; }
	}
	far_copy(FAR(ram_slot[slot], 0), FAR(STATE_PAGE, 0), size);
	return SAVE_OK;
}

// Заголовок слота в h (ближняя память). SAVE_OK — слот занят сохранением этой игры.
uint8_t st_peek(uint8_t slot, st_header_t *h) __banked
{
	memset(h, 0, sizeof *h);
	if (slot >= SAVE_SLOTS) return SAVE_BADSLOT;
	uint16_t got = 0;
	uint8_t r = bios_file(BIOS_READ, slot, near_phys(h), sizeof *h, &got);
	if (r == BIOS_NONE) {
		if (!ram_slot[slot]) return SAVE_EMPTY;
		far_read(FAR(ram_slot[slot], 0), h, sizeof *h);
		got = sizeof *h;
	} else if (r == BIOS_NOFILE)
		return SAVE_EMPTY;
	else if (r != BIOS_OK)
		return SAVE_IOERR;
	if (got < sizeof *h || h->magic != ST_MAGIC) return SAVE_BAD;
	if (h->version != ST_VERSION) return SAVE_VERSION;
	if (h->game != res_game()) return SAVE_OTHERGAME;
	return SAVE_OK;
}

uint8_t st_load(uint8_t slot) __banked
{
	st_header_t h;
	uint8_t r = st_peek(slot, &h);
	if (r != SAVE_OK) return r;
	uint16_t got = 0;
	r = bios_file(BIOS_READ, slot, FAR(STATE_PAGE, 0), h.size, &got);
	if (r == BIOS_NONE) {
		far_copy(FAR(STATE_PAGE, 0), FAR(ram_slot[slot], 0), h.size);
		got = h.size;
	} else if (r != BIOS_OK)
		return SAVE_IOERR;
	if (got != h.size || checksum(h.size) != h.sum) {
		st_clear();                        // состояние испорчено — без игры
		return SAVE_BAD;
	}
	df_reset();                            // бои не сохраняются (Craft::_inDogfight тоже)
	return SAVE_OK;
}

// SaveGameState(SAVE_IRONMAN): Ironman — в свой слот под своим именем (до первого
// сохранения имени нет — ничего). SAVE_OK — сохранено или не нужно.
uint8_t st_ironsave(void) __banked
{
	char name[SAVE_NAME];
	if (!ST->ironman || !ST->hdr.name[0]) return SAVE_OK;
	memcpy(name, ST->hdr.name, SAVE_NAME);
	return st_save(ST->iron_slot, name);
}

uint8_t st_delete(uint8_t slot) __banked
{
	if (slot >= SAVE_SLOTS) return SAVE_BADSLOT;
	uint8_t r = bios_file(BIOS_DELETE, slot, 0, 0, 0);
	if (r == BIOS_NONE && ram_slot[slot]) {
		pg_free(ram_slot[slot], 3);
		ram_slot[slot] = 0;
	}
	return SAVE_OK;
}

// Пустое состояние: нули в обеих страницах (хвост страницы ядра тоже — он
// попадает в сохранение и контрольную сумму).
void st_clear(void) __banked
{
	far_fill(FAR(STATE_PAGE, 0), 0, 0x4000);
	far_fill(FAR(SOLDIER_PAGE, 0), 0, 0x4000);
	far_fill(FAR(INV_PAGE, 0), 0, 0x4000);
}
