// FAT16/FAT32 на SD, только чтение + запись секторов на месте (src/kernel/fat.c,
// банк 12; секторы — src/kernel/sd.s). Файлы — по пути 8.3 от корня: «OXZ/TFTD/UFOP.PAK».
#ifndef FAT_H
#define FAT_H

#include <stdint.h>
#include "far.h"

#define FAT_OK      0
#define FAT_NOCARD  1       // карта не отвечает (или нет SD)
#define FAT_IOERR   2
#define FAT_NOFS    3       // не FAT16/FAT32 (или сектор не 512 байт)
#define FAT_NOFILE  4
#define FAT_FRAG    5       // файл раздроблен больше чем на FAT_RUNS участков
#define FAT_EOF     6

#define FAT_RUNS    8
typedef struct {
	uint32_t size;
	uint8_t nruns;
	uint32_t run_clus[FAT_RUNS], run_len[FAT_RUNS];
} fat_file_t;

// Сведения о томе (экран диагностики SD, sddiag.c)
typedef struct {
	uint8_t part;           // тип раздела из MBR (0 — том с сектора 0, без MBR)
	uint32_t base;          // LBA тома
	uint8_t fat32;          // 1 — FAT32, 0 — FAT16
	uint8_t spc;            // секторов в кластере
	uint32_t data_lba;      // первый сектор области данных
	uint8_t raw0[8];        // первые байты сектора 0 (MBR или том)
	uint8_t raw1[8];        // первые байты сектора по LBA раздела из MBR (0 — не читали)
} fat_info_t;

extern uint8_t fat_ok;      // том смонтирован
extern uint8_t fat_nodma;   // 1 — чтение пачкой (CMD18 + DMA) не прошло проверку при монтировании,
                            // fat_read читает посекторно (CMD17); 23 §5
uint8_t fat_mount(void) __banked;
void fat_info(fat_info_t *i) __banked;
uint8_t fat_open(const char *path, fat_file_t *f) __banked;
uint8_t fat_read(const fat_file_t *f, uint32_t pos, far_t dst, uint32_t len) __banked;
uint8_t fat_write(const fat_file_t *f, uint32_t pos, far_t src, uint32_t len) __banked;

#endif
