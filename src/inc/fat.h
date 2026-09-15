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

extern uint8_t fat_ok;      // том смонтирован
uint8_t fat_mount(void) __banked;
uint8_t fat_open(const char *path, fat_file_t *f) __banked;
uint8_t fat_read(const fat_file_t *f, uint32_t pos, far_t dst, uint32_t len) __banked;
uint8_t fat_write(const fat_file_t *f, uint32_t pos, far_t src, uint32_t len) __banked;

#endif
