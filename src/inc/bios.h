// «BIOS» игры: файловые сервисы (общий код, src/kernel/bios.c).
// Сейчас — служба эмулятора (порты #FD-#FFAF, файлы tmp/saves/<игра>/SLOTnn.SAV
// на хосте); на железе порты не отвечают (статус BIOS_NONE), позже — драйвер SD.
// Память — физическая (far_t), любые страницы; длина — до 64 КБ.
#ifndef BIOS_H
#define BIOS_H

#include <stdint.h>
#include "far.h"

#define BIOS_WRITE  1      // записать файл слота из памяти
#define BIOS_READ   2      // прочитать файл слота в память (не больше len)
#define BIOS_SIZE   3      // размер файла слота
#define BIOS_DELETE 4      // удалить файл слота

#define BIOS_OK     0
#define BIOS_NOFILE 1
#define BIOS_IOERR  2
#define BIOS_NONE   0xFF   // службы нет (реальное железо без драйвера)

// Результат: BIOS_*; *result — байт прочитано / размер файла (может быть 0).
uint8_t bios_file(uint8_t cmd, uint8_t slot, far_t mem, uint16_t len, uint16_t *result);

#endif
