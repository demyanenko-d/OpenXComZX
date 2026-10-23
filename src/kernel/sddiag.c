// Диагностика SD-карты (банк 14, служебный — рядом с самотестом джампера): на чужой машине
// или эмуляторе отладочный вывод (#F8AF) не виден, а без пакетов с карты игра молча теряет
// заставки, фоны окон и музыку. Здесь — то же, что видно в логе нашего эмулятора, и сверх
// того проверка чтения: монтирование и каталоги идут посекторно (CMD17, байты через INIR),
// а ресурсы — пачкой (CMD18 + DMA SPI->RAM, sd.s). Если пачка расходится с посекторным
// чтением, пакеты «находятся», но внутри пусто — главный подозреваемый на новых эмуляторах.
//
// Только публичный API fat_* (банк 12): всё, что уходит туда по указателю (путь, описатель
// файла, сведения о томе), лежит на стеке (Win0); результаты — в памяти этого банка (D).
#include <stdint.h>
#include <string.h>
#include "pages.h"
#include "far.h"
#include "fat.h"
#include "res.h"
#include "text.h"
#include "sddiag.h"

extern uint8_t sd_state;               // sdres.c: 0 — не открывали, 1 — пакеты есть, 2 — нет
extern uint8_t sd_type, sd_step, sd_r1; // sd.s: тип карты, шаг sd_init, последний R1

#define NPK 13
static const char *const pk_name[NPK] = {
	"BACK", "UFOP", "ITEMS", "CUTS", "BATUI", "BATTLE", "MAPS", "TILES", "UNITS",
	"MUSIC", "MUSICAY", "MUSICFM", "GVIEW",
};
#define PK_GVIEW 12                     // GVIEW.PAK — свой формат (сигнатура GVE2)

// Чтение пачкой против посекторного (dma)
#define RD_OK       0
#define RD_MISMATCH 1                   // данные разошлись
#define RD_ERR18    2                   // ошибка чтения пачкой (CMD18 + DMA)
#define RD_ERR17    3                   // ошибка посекторного чтения (CMD17)
#define RD_NOPAGE   4                   // нет свободной страницы под буфер
#define RD_NOFILE   5                   // нет BACK.PAK — читать нечего
#define RD_NOVOL    6                   // том не смонтирован
#define RD_BADHDR   7                   // чтения совпали, но это не пакет (кластеры не те)
// Пакеты (pk_r): коды FAT_* (fat.h) и свои
#define PK_BADHDR   0x10                // нет сигнатуры
#define PK_GAME     0x11                // пакет другой игры
#define PK_SKIP     0xFF                // не проверяли

#define RD_LEN 1024                     // два сектора: CMD18 на 2 блока + CMD12
#define RD_ODD 4097                     // нечётный приёмник — fat_read идёт посекторно

// Смещения полей проверяют сценарии tests/sdinfo*.oxs (страница #3E, D с #3000):
// +#01 step, +#02 r1, +#03 type, +#04 mount, +#05 rd, +#06 rd_at, +#18..#24 pk_r,
// fi — последним (+#80), чтобы его рост не сдвигал остальное.
typedef struct {
	uint8_t full;
	uint8_t step, r1, type;             // sd_init
	uint8_t mount;                      // код fat_mount; MOUNT_EARLY — смонтирован до нас
	uint8_t rd;
	uint16_t rd_at;                     // смещение первого расхождения
	uint8_t b18[8], b17[8];             // байты двух чтений с этого места
	uint8_t pk_r[NPK];
	uint8_t pk_runs[NPK];
	uint16_t pk_n[NPK];                 // записей в каталоге
	uint32_t pk_size[NPK];
	fat_info_t fi;
} sdd_t;
#define MOUNT_EARLY 0xFE
// В памяти банка (build.ps1: ram банка 14), а не в Win1: Win1 почти полон. Память банка при
// старте не обнуляется — sddiag_run чистит D первым делом; экран зовёт её на EVT_OPEN.
static sdd_t __at(0xB000) D;

static char *put(char *p, const char *s)
{
	while (*s) *p++ = *s++;
	*p = 0;
	return p;
}

static char *put_num(char *p, uint32_t v)
{
	fmt_num(p, (int32_t)v, 0);
	return p + strlen(p);
}

static char *put_hex(char *p, uint8_t v)
{
	static const char hx[] = "0123456789ABCDEF";
	*p++ = hx[v >> 4];
	*p++ = hx[v & 15];
	*p = 0;
	return p;
}

static void path_make(char *p, uint8_t i)
{
	p = put(p, res_game() == 2 ? "OXZ/TFTD/" : "OXZ/UFO/");
	p = put(p, pk_name[i]);
	put(p, ".PAK");
}

// Два сектора BACK.PAK пачкой (CMD18 + DMA, чётный приёмник) и посекторно (CMD17 через
// буфер сектора, нечётный приёмник) в одну страницу; страница заранее залита разными
// узорами, чтобы «DMA ничего не записал» не сошло за совпадение.
static void read_test(uint8_t pg)
{
	fat_file_t f;
	char path[24];
	uint8_t a[8], b[8];
	path_make(path, 0);
	if (fat_open(path, &f)) { D.rd = RD_NOFILE; return; }
	far_fill(FAR(pg, 0), 0x55, RD_LEN);
	far_fill(FAR(pg, RD_ODD), 0xAA, RD_LEN);
	if (fat_read(&f, 0, FAR(pg, 0), RD_LEN)) { D.rd = RD_ERR18; return; }
	if (fat_read(&f, 0, FAR(pg, RD_ODD), RD_LEN)) { D.rd = RD_ERR17; return; }
	for (uint16_t o = 0; o < RD_LEN; o += 8) {
		far_read(FAR(pg, o), a, 8);
		far_read(FAR(pg, RD_ODD + o), b, 8);
		if (!o || memcmp(a, b, 8)) {        // первые байты — всегда, для показа
			memcpy(D.b18, a, 8);
			memcpy(D.b17, b, 8);
			D.rd_at = o;
		}
		if (memcmp(a, b, 8)) { D.rd = RD_MISMATCH; return; }
	}
	D.rd_at = 0;
	if (memcmp(D.b17, "OXZP", 4)) D.rd = RD_BADHDR;
}

static void pack_check(uint8_t i, uint8_t pg)
{
	fat_file_t f;
	char path[24];
	uint8_t h[8];
	path_make(path, i);
	uint8_t r = fat_open(path, &f);
	D.pk_r[i] = r;
	if (r) return;
	D.pk_size[i] = f.size;
	D.pk_runs[i] = f.nruns;
	if (fat_read(&f, 0, FAR(pg, 0), 8)) { D.pk_r[i] = FAT_IOERR; return; }
	far_read(FAR(pg, 0), h, 8);
	if (memcmp(h, i == PK_GVIEW ? "GVE2" : "OXZP", 4)) { D.pk_r[i] = PK_BADHDR; return; }
	if (i == PK_GVIEW) return;
	if (h[5] != res_game()) { D.pk_r[i] = PK_GAME; return; }
	D.pk_n[i] = h[6] | (h[7] << 8);
}

void sddiag_run(uint8_t full) __banked
{
	fat_info_t fi;
	memset(&D, 0, sizeof D);
	memset(D.pk_r, PK_SKIP, NPK);
	D.full = full;
	D.mount = fat_ok ? MOUNT_EARLY : fat_mount();
	D.step = sd_step;
	D.r1 = sd_r1;
	D.type = sd_type;
	fat_info(&fi);
	D.fi = fi;
	if (!fat_ok) { D.rd = RD_NOVOL; return; }
	uint8_t pg = pg_alloc(1, 1);
	if (pg == PG_NONE) { D.rd = RD_NOPAGE; return; }
	uint8_t nd = fat_nodma;             // сравнить именно пачку с посекторным, даже если
	fat_nodma = 0;                      // fat_mount уже перевёл чтение на CMD17
	read_test(pg);
	fat_nodma = nd;
	if (full)
		for (uint8_t i = 0; i < NPK; i++) pack_check(i, pg);
	pg_free(pg, 1);
}

// sd_state == 0 — не беда: sdres открывает пакеты лениво, при первом ресурсе, а главное меню
// спрашивает до рисования своего фона (под сценарием интро нет — пакетов ещё никто не
// просил). Нет пакетов вовсе — это и так поймает read_test: он открывает BACK.PAK сам.
// Пачка не работает, но fat_mount уже перевёл чтение на CMD17 — не беда: игра цела, только
// медленнее; об этом говорит заголовок главного меню, а окно на каждом запуске мешало бы.
uint8_t sddiag_bad(void) __banked
{
	if (sd_state == 2) return 1;
	if (fat_nodma && (D.rd == RD_MISMATCH || D.rd == RD_ERR18)) return 0;
	return D.rd != RD_OK;
}

#define ROW_PK 6                        // первая строка пакетов
uint8_t sddiag_rows(void) __banked
{
	return ROW_PK + NPK;
}

// Заголовок окна: игра и что увидела sdres.c при первом запросе ресурса
void sddiag_title(char *buf) __banked
{
	char *p = put(buf, "SD CARD - ");
	p = put(p, res_game() == 2 ? "TFTD" : "UFO");
	put(p, sd_state == 1 ? ": packs opened" : sd_state == 2 ? ": packs FAILED" : ": packs not opened yet");
}

static const char *const step_name[] = { "", "CMD0", "CMD8", "ACMD41", "CMD58", "CMD16" };
static const char *const fat_err[] = {
	"OK", "NO CARD", "READ ERROR", "NOT FAT16/32", "NOT FOUND", "FRAGMENTED", "SHORT FILE",
};

static char *put_bytes(char *p, const uint8_t *b)
{
	for (uint8_t i = 0; i < 8; i++) { p = put_hex(p, b[i]); *p++ = ' '; }
	*p = 0;
	return p;
}

void sddiag_text(uint8_t row, char *buf) __banked
{
	char *p = buf;
	// Том не смонтирован (а карта ответила): строки 2, 4, 5 — про MBR и сырые байты секторов,
	// по фото сразу видно, что за сектор 0 (MBR от GRUB «EB 63 90», Windows «33 C0»…)
	uint8_t merr = D.mount && D.mount != MOUNT_EARLY && D.mount != FAT_NOCARD;
	*p = 0;
	switch (row) {
	case 0:                             // карта
		p = put(p, "Card\t");
		if (D.step && D.mount != MOUNT_EARLY) {
			p = put(p, "NOT READY at ");
			p = put(p, step_name[D.step < 6 ? D.step : 0]);
			p = put(p, ", R1=");
			put_hex(p, D.r1);
		} else
			put(p, D.type == 8 ? "SDHC/SDXC (block), ready" : D.type ? "SDSC (byte addr), ready" : "?");
		break;
	case 1:                             // том
		p = put(p, "Volume\t");
		if (D.mount && D.mount != MOUNT_EARLY) {
			p = put(p, "MOUNT ERROR: ");
			put(p, fat_err[D.mount < 7 ? D.mount : 0]);
			break;
		}
		p = put(p, D.fi.fat32 ? "FAT32, " : "FAT16, ");
		if (D.fi.part) {
			p = put(p, "MBR type ");
			p = put_hex(p, D.fi.part);
			p = put(p, ", LBA ");
			put_num(p, D.fi.base);
		} else
			put(p, "no MBR");
		break;
	case 2:                             // кластер; том не найден — что в MBR
		if (merr) {
			p = put(p, "MBR\t");
			if (!D.fi.part) { put(p, "no FAT partition / not an MBR"); break; }
			p = put(p, "last tried: type ");
			p = put_hex(p, D.fi.part);
			p = put(p, ", LBA ");
			put_num(p, D.fi.base);
			break;
		}
		p = put(p, "Cluster\t");
		if (!fat_ok) break;
		p = put_num(p, (uint32_t)D.fi.spc * 512);
		p = put(p, " bytes, data at LBA ");
		put_num(p, D.fi.data_lba);
		break;
	case 3:                             // пачка против посекторного
		p = put(p, "Read test\t");
		switch (D.rd) {
		case RD_OK:       p = put(p, "CMD18+DMA = CMD17: OK"); break;
		case RD_MISMATCH: p = put(p, "MISMATCH at byte "); p = put_num(p, D.rd_at); break;
		case RD_ERR18:    p = put(p, "CMD18+DMA: READ ERROR"); break;
		case RD_ERR17:    p = put(p, "CMD17: READ ERROR"); break;
		case RD_NOPAGE:   p = put(p, "no free page"); break;
		case RD_NOFILE:   p = put(p, "no BACK.PAK to read"); break;
		case RD_NOVOL:    p = put(p, "no volume"); break;
		case RD_BADHDR:   p = put(p, "same data, but not a pack"); break;
		}
		if (fat_nodma) put(p, ", using CMD17");   // fat_mount перевёл чтение на посекторное
		break;
	case 4:
	case 5: {                           // байты двух чтений; том не найден — байты секторов
		if (merr) {
			p = put(p, row == 4 ? "  sector 0\t" : "  partition\t");
			if (row == 4 || D.fi.part) put_bytes(p, row == 4 ? D.fi.raw0 : D.fi.raw1);
			break;
		}
		p = put(p, row == 4 ? "  CMD18+DMA\t" : "  CMD17\t");
		if (D.rd != RD_OK && D.rd != RD_MISMATCH && D.rd != RD_BADHDR) break;
		put_bytes(p, row == 4 ? D.b18 : D.b17);
		break;
	}
	default: {                          // пакеты
		uint8_t i = row - ROW_PK;
		if (i >= NPK) break;
		p = put(p, pk_name[i]);
		p = put(p, ".PAK\t");
		uint8_t r = D.pk_r[i];
		if (r == PK_SKIP) put(p, "-");
		else if (r == PK_BADHDR) put(p, "BAD HEADER");
		else if (r == PK_GAME) put(p, "OTHER GAME");
		else if (r) put(p, fat_err[r < 7 ? r : 0]);
		else {
			p = put(p, "OK ");
			if (i != PK_GVIEW) { p = put_num(p, D.pk_n[i]); p = put(p, " res, "); }
			p = put_num(p, D.pk_size[i] >> 10);
			p = put(p, " KB, ");
			p = put_num(p, D.pk_runs[i]);
			put(p, D.pk_runs[i] == 1 ? " run" : " runs");
		}
		break;
	}
	}
}
