// Музыка (project_docs/20_music_player.md): поток записей в регистры чипа готовит главный
// цикл (src/kernel/music.c, банк 12), а кадровое прерывание только выгружает готовый кадр из
// кольца (src/kernel/win0/music_s.s). Игровой код видит лишь mus_play/mus_stop/mus_pump.
#ifndef MUSIC_H
#define MUSIC_H

#include <stdint.h>

// Номер ресурса музыки — MUS_BASE + тип (mus_ids.h от конвертера)
#define MUS_BASE 0x0300

// Роли экранов и группы из таблицы MUSGRP (22 §1.3)
#define MK_MENU       0
#define MK_MEMORIAL   1
#define MK_DEBR_GOOD  2
#define MK_DEBR_BAD   3
#define MK_TACTIC     4
#define MK_BRIEF_DEF  5
#define MK_LOSE       6
#define MK_WIN        7
#define MG_GEO        0
#define MG_INTER      1

uint16_t mus_kind(uint8_t role) __banked;   // тема экрана (0 — темы нет, оставить текущее)
uint16_t mus_pick(uint8_t grp) __banked;    // случайный трек группы
void mus_play(uint16_t id) __banked;    // начать трек (тот же id второй раз — не перезапускать)
void mus_stop(void) __banked;           // остановить и заглушить чип
void mus_pump(void) __banked;           // такт подкачки: SD и кольцо; звать раз за оборот цикла
void mus_fill(void) __banked;           // только кольцо, без SD: можно звать из долгих операций
void mus_set_mode(uint8_t mode) __banked; // сменить режим звука (SND_*) и перезапустить трек

extern uint8_t mus_state;               // 0 выкл, 1 играет, 2 пауза (music_s.s)
extern uint8_t mus_under;               // недоборы (диагностика)

#endif
