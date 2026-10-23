// FAT16/FAT32 только для чтения (банк 12): монтирование (MBR или том с сектора 0),
// поиск файла по пути 8.3 («OXZ/TFTD/UFOP.PAK»), цепочка кластеров файла —
// участками (runs), чтение с любого смещения в дальнюю память. Секторы — sd.s.
// Запись на месте (слоты сохранений) — fat_write_sectors по тем же участкам.
#include <stdint.h>
#include <string.h>
#include "pages.h"
#include "far.h"
#include "fat.h"
#include "dbg.h"

extern uint32_t sd_lba, sd_phys;
extern uint16_t sd_ptr, sd_cnt;
extern uint8_t sd_type;
uint8_t sd_init(void);
uint8_t sd_rd(void);
uint8_t sd_rdm(void);
uint8_t sd_wr(void);

static uint8_t secbuf[512];             // сектор каталога / FAT / краёв чтения (Win1)
static uint32_t sec_cached = 0xFFFFFFFFul;

static uint8_t fat32, spc, spc_shift;   // FAT32?, секторов на кластер (степень 2)
static uint32_t fat_lba, data_lba, root_lba, root_clus;
static uint16_t root_secs;              // FAT16: секторов корневого каталога
static uint32_t vol_base;               // LBA тома (0 — том с сектора 0, без MBR)
static uint8_t vol_part;                // тип раздела из MBR (0 — MBR нет)
static uint8_t vol_raw0[8], vol_raw1[8]; // начало сектора 0 и сектора по LBA раздела (диагностика)
uint8_t fat_ok;
uint8_t fat_nodma;                      // 1 — пачка (CMD18 + DMA) не прошла проверку: всё посекторно

static uint8_t read_sec(uint32_t lba)
{
	if (lba == sec_cached) return 0;
	sd_lba = lba;
	sd_ptr = (uint16_t)secbuf;
	sec_cached = 0xFFFFFFFFul;
	if (sd_rd()) return 1;
	sec_cached = lba;
	return 0;
}

static uint16_t w16(uint16_t o) { return secbuf[o] | (secbuf[o + 1] << 8); }
static uint32_t w32(uint16_t o) { return w16(o) | ((uint32_t)w16(o + 2) << 16); }

// Проверка чтения пачкой при монтировании: два первых сектора тома — CMD18 + DMA SPI->RAM
// в страницу пула и посекторно (CMD17 + INIR в secbuf) — должны совпасть. Иначе (сторонний
// эмулятор без канала DMA SPI или без CMD18: каталоги и том находятся, а ресурсы приходят
// пустыми — без заставки, фонов и музыки, 23 §5) fat_read читает всё посекторно: медленнее,
// но данные настоящие. Страница залита узором — «DMA ничего не записал» не сойдёт за
// совпадение. Нет свободной страницы (бой) — прежний вывод остаётся. Монтируют и sdres,
// и музыка, и глобус, поэтому проверка здесь, а не у кого-то одного из них.
static void dma_check(uint32_t lba)
{
	uint8_t b[16];
	uint8_t bad = 0;
	uint8_t pg = pg_alloc(1, 1);
	if (pg == PG_NONE) return;
	uint8_t old = pg_win3();
	far_fill(FAR(pg, 0), 0x55, 1024);
	sd_lba = lba;
	sd_phys = FAR(pg, 0);
	sd_cnt = 2;
	if (sd_rdm()) bad = 2;
	for (uint8_t s = 0; s < 2 && !bad; s++) {
		sec_cached = 0xFFFFFFFFul;
		if (read_sec(lba + s)) { bad = 3; break; }
		for (uint16_t o = 0; o < 512; o += 16) {
			far_read(FAR(pg, (uint16_t)s * 512 + o), b, 16);
			if (memcmp(b, secbuf + o, 16)) { bad = 1; break; }
		}
	}
	pg_map3(old);
	pg_free(pg, 1);
	if (bad == 3) return;                       // и CMD17 не читает — DMA тут ни при чём
	fat_nodma = bad != 0;
	if (bad) dbg_puts(bad == 1 ? "sd: CMD18+DMA data differ, reading by CMD17\n" : "sd: CMD18+DMA failed, reading by CMD17\n");
}

// В secbuf — загрузочный сектор тома FAT? Подпись 55AA и правдоподобный BPB: 512 байт на
// сектор, секторов в кластере — степень двойки, 1–2 FAT, резерв не ноль. По первому байту
// (переход EB/E9) не отличить: MBR от GRUB и syslinux тоже начинается с EB. Так и вышло на
// настоящем ZX Evolution (2026-10-24): флешка из-под установщика Ubuntu, MBR «EB 63 90»,
// раздел с сектора 2048 — MBR принимался за том, по смещению 11 — 00 00, «не FAT», и ни
// заставки, ни фонов, ни музыки, хотя TS-BIOS и WC с неё грузились (23 §5).
static uint8_t is_vbr(void)
{
	uint8_t c = secbuf[13], n = secbuf[16];
	return w16(510) == 0xAA55 && w16(11) == 512 && c && !(c & (c - 1)) && n && n <= 2 && w16(14);
}

uint8_t fat_mount(void) __banked
{
	uint32_t base = 0;
	fat_ok = 0;
	vol_base = 0;
	vol_part = 0;
	memset(vol_raw0, 0, 8);
	memset(vol_raw1, 0, 8);
	sec_cached = 0xFFFFFFFFul;
	if (sd_init()) return FAT_NOCARD;
	if (read_sec(0)) return FAT_IOERR;
	memcpy(vol_raw0, secbuf, 8);
	if (w16(510) != 0xAA55) return FAT_NOFS;
	// Не том — значит MBR: первый раздел, где лежит том FAT. Тип раздела не смотрим, как и
	// TS-BIOS (REF/tsbios/src/pff/pff.c pf_mount): Windows при форматировании раздела тип в
	// MBR не меняет, и карта из-под exFAT (07) после форматирования в FAT32 остаётся с 07.
	if (!is_vbr()) {
		uint8_t i;
		for (i = 0; i < 4; i++) {
			if (read_sec(0)) return FAT_IOERR;
			uint16_t e = 0x1BE + (uint16_t)i * 16;
			uint8_t t = secbuf[e + 4];
			if (!t) continue;
			base = w32(e + 8);
			vol_part = t;
			vol_base = base;
			if (read_sec(base)) return FAT_IOERR;
			memcpy(vol_raw1, secbuf, 8);
			if (is_vbr()) break;
		}
		if (i == 4) return FAT_NOFS;
	}
	spc = secbuf[13];
	spc_shift = 0;
	while ((1 << spc_shift) < spc) spc_shift++;
	uint16_t rsvd = w16(14);
	uint8_t nfat = secbuf[16];
	uint16_t rootent = w16(17);
	uint32_t fatsz = w16(22);
	fat32 = !fatsz;
	if (fat32) fatsz = w32(36);
	fat_lba = base + rsvd;
	root_secs = (uint16_t)(((uint32_t)rootent * 32 + 511) / 512);
	root_lba = fat_lba + (uint32_t)nfat * fatsz;
	data_lba = root_lba + root_secs;
	root_clus = fat32 ? w32(44) : 0;
	fat_ok = 1;
	dma_check(base);
	return FAT_OK;
}

static uint32_t clus_lba(uint32_t c) { return data_lba + ((c - 2) << spc_shift); }

// Следующий кластер цепочки (>= 0x0FFFFFF8 / 0xFFF8 — конец)
static uint32_t fat_next(uint32_t c)
{
	if (fat32) {
		if (read_sec(fat_lba + (c >> 7))) return 0x0FFFFFFFul;
		return w32((uint16_t)(c & 127) * 4) & 0x0FFFFFFFul;
	}
	if (read_sec(fat_lba + (c >> 8))) return 0xFFFF;
	uint32_t n = w16((uint16_t)(c & 255) * 2);
	return n >= 0xFFF8 ? 0x0FFFFFFFul : n;
}

static uint8_t is_end(uint32_t c) { return c < 2 || c >= 0x0FFFFFF8ul; }

// Имя компонента пути -> 11 байт 8.3 (заглавные, пробелы)
static const char *name83(const char *p, char *n)
{
	memset(n, ' ', 11);
	uint8_t i = 0;
	while (*p && *p != '/' && *p != '.') { if (i < 8) n[i++] = (*p >= 'a' && *p <= 'z') ? *p - 32 : *p; p++; }
	if (*p == '.') {
		p++;
		i = 8;
		while (*p && *p != '/') { if (i < 11) n[i++] = (*p >= 'a' && *p <= 'z') ? *p - 32 : *p; p++; }
	}
	if (*p == '/') p++;
	return p;
}

// Поиск записи в каталоге (clus 0 — корень FAT16). Выход: первый кластер, размер.
static uint8_t dir_find(uint32_t clus, const char *n, uint32_t *fc, uint32_t *size, uint8_t *attr)
{
	uint32_t lba;
	uint16_t left;                              // секторов в текущем куске
	if (!clus && !fat32) { lba = root_lba; left = root_secs; }
	else { if (!clus) clus = root_clus; lba = clus_lba(clus); left = spc; }
	for (;;) {
		if (read_sec(lba)) return FAT_IOERR;
		for (uint16_t o = 0; o < 512; o += 32) {
			uint8_t c0 = secbuf[o];
			if (!c0) return FAT_NOFILE;
			if (c0 == 0xE5 || secbuf[o + 11] == 0x0F || (secbuf[o + 11] & 0x08)) continue;
			if (memcmp(secbuf + o, n, 11)) continue;
			*attr = secbuf[o + 11];
			*fc = w16(o + 26) | ((uint32_t)w16(o + 20) << 16);
			*size = w32(o + 28);
			return FAT_OK;
		}
		lba++;
		if (--left) continue;
		if (!clus) return FAT_NOFILE;               // корень FAT16 кончился
		clus = fat_next(clus);
		if (is_end(clus)) return FAT_NOFILE;
		lba = clus_lba(clus);
		left = spc;
	}
}

// Подряд идущие записи v -> v+1 в уже загруженном секторе FAT (без вызовов
// fat_next: 7 МБ при кластере 512 байт — 14 000 записей). v — кластер, чья запись
// ещё не проверена. Выход: первый непроверенный кластер; *len += пройденные.
static uint32_t chain_skip(uint32_t v, uint32_t *len)
{
	uint16_t n = 0;
	uint16_t lo = (uint16_t)v + 1;                   // ожидаемая запись v + 1 (16-битными словами)
	if (fat32) {
		uint16_t hi = (uint16_t)(v >> 16) + (lo == 0);
		uint8_t k = (uint8_t)v & 127;
		const uint16_t *e = (const uint16_t *)secbuf + k * 2;
		while (k && e[0] == lo && (e[1] & 0x0FFF) == hi) {
			e += 2; n++;
			k = (k + 1) & 127;
			if (!++lo) hi++;
		}
	} else {
		uint8_t k = (uint8_t)v;                      // k++ за 255 -> 0: конец сектора
		const uint16_t *e = (const uint16_t *)secbuf + k;
		while (k && *e == lo) { e++; n++; k++; lo++; }
	}
	*len += n;
	return v + n;
}

// Открыть файл: цепочка кластеров сворачивается в участки подряд идущих кластеров.
uint8_t fat_open(const char *path, fat_file_t *f) __banked
{
	char n[11];
	uint32_t clus = 0, size = 0;
	uint8_t attr = 0x10, r;
	if (!fat_ok) return FAT_NOFS;
	while (*path) {
		if (!(attr & 0x10)) return FAT_NOFILE;
		path = name83(path, n);
		r = dir_find(clus, n, &clus, &size, &attr);
		if (r) return r;
	}
	if (attr & 0x10) return FAT_NOFILE;
	f->size = size;
	f->nruns = 0;
	uint32_t c = clus, start = clus, len = 0;
	while (!is_end(c)) {
		uint32_t nx = fat_next(c);
		len++;
		if (nx == c + 1) { c = chain_skip(nx, &len); continue; }
		if (f->nruns == FAT_RUNS) return FAT_FRAG;   // конец участка
		f->run_clus[f->nruns] = start;
		f->run_len[f->nruns] = len;
		f->nruns++;
		start = nx;
		len = 0;
		c = nx;
	}
	return FAT_OK;
}

// Сектор файла с номером s (от начала) -> LBA; 0 — за концом. *left — секторов
// до конца участка подряд идущих кластеров (с этим сектором).
static uint32_t file_lba(const fat_file_t *f, uint32_t s, uint32_t *left)
{
	uint32_t cl = s >> spc_shift;
	for (uint8_t i = 0; i < f->nruns; i++) {
		if (cl < f->run_len[i]) {
			uint8_t in = (uint8_t)s & (spc - 1);
			*left = ((f->run_len[i] - cl) << spc_shift) - in;
			return clus_lba(f->run_clus[i] + cl) + in;
		}
		cl -= f->run_len[i];
	}
	return 0;
}

// Прочитать len байт с позиции pos в дальнюю память dst. Целые секторы — DMA
// прямо по физическому адресу (sd_rdm, CMD18 на весь участок), края и нечётный
// приёмник — через secbuf.
uint8_t fat_read(const fat_file_t *f, uint32_t pos, far_t dst, uint32_t len) __banked
{
	uint8_t old = pg_win3();
	uint8_t r = FAT_OK;
	// Приёмник 0 — ошибка вызывающего: DMA уйдёт в физическую страницу 0 (ревизия 2026-09-26)
	if (!dst) { dbg_puts("fat_read: dst=0 len "); dbg_dec((uint16_t)len); dbg_puts("\n"); }
	while (len) {
		uint32_t left;
		uint32_t lba = file_lba(f, pos >> 9, &left);
		if (!lba) { r = FAT_EOF; break; }
		uint16_t so = (uint16_t)pos & 511;
		uint16_t n = 512 - so;
		if (n > len) n = (uint16_t)len;
		if (!so && len >= 512 && !((uint8_t)dst & 1) && !fat_nodma) {
			uint32_t k = len >> 9;
			if (k > left) k = left;
			if (k > 0xFFFF) k = 0xFFFF;
			sd_lba = lba;
			sd_phys = dst;
			sd_cnt = (uint16_t)k;
			if (sd_rdm()) { r = FAT_IOERR; break; }
			k <<= 9;
			pos += k;
			dst += k;
			len -= k;
			continue;
		}
		if (read_sec(lba)) { r = FAT_IOERR; break; }
		pg_map3(old);
		far_write(dst, secbuf + so, n);
		pos += n;
		dst += n;
		len -= n;
	}
	pg_map3(old);
	return r;
}

// Записать целые секторы на место (файл уже есть нужного размера): слоты сохранений.
uint8_t fat_write(const fat_file_t *f, uint32_t pos, far_t src, uint32_t len) __banked
{
	uint8_t old = pg_win3();
	uint8_t r = FAT_OK;
	sec_cached = 0xFFFFFFFFul;
	for (; len >= 512; pos += 512, src += 512, len -= 512) {
		uint32_t left;
		uint32_t lba = file_lba(f, pos >> 9, &left);
		if (!lba || (pos & 511)) { r = FAT_EOF; break; }
		far_read(src, secbuf, 512);
		sd_lba = lba;
		sd_ptr = (uint16_t)secbuf;
		if (sd_wr()) { r = FAT_IOERR; break; }
	}
	pg_map3(old);
	return r;
}

// Сведения о томе для экрана диагностики (sddiag.c, другой банк): i — в Win0/Win1.
void fat_info(fat_info_t *i) __banked
{
	i->part = vol_part;
	i->base = vol_base;
	i->fat32 = fat32;
	i->spc = spc;
	i->data_lba = data_lba;
	memcpy(i->raw0, vol_raw0, 8);
	memcpy(i->raw1, vol_raw1, 8);
}
