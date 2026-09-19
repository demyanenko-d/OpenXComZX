// Музыка: главный цикл плеера (банк 12, рядом с fat.c/sdres.c — тоже работа с картой).
// Проект — project_docs/20_music_player.md, данные — 22_music_data.md.
//
// Три уровня: MUSIC.PAK на SD -> страница подкачки (16 КБ, две половины по 8 КБ) -> кольцо
// в Win0 (4 КБ, music_s.s) -> порты чипа. Здесь делается всё, кроме выгрузки в порты:
// чтение с карты, разбор длин записей, зацикливание, конец трека. В прерывании остаётся
// «прочитал байт — выдал пару в порт».
//
// Формат потока (09 §7): u32 loop_offset (#FFFFFFFF — без повтора), u32 кадров, u16 макс.
// записей за кадр, u16 пар ресинка; записи: #00..#7F — n0 пар банка 0, затем байт n1 и n1 пар
// банка 1; #81..#FE — пауза (b - #80) кадров; #FF — конец потока, за ним блок ресинка.
#include <stdint.h>
#include <string.h>
#include "tsconf.h"
#include "memmap.h"
#include "pages.h"
#include "far.h"
#include "res.h"
#include "fat.h"
#include "game.h"
#include "music.h"
#include "res_ids.h"
#include "dbg.h"

#define RING_SIZE   4096
#define RING_END    (RING_SIZE - 1)
#define MUS_AHEAD   96                   // целевой задел, кадров (~2 с)
#define MUS_LIMIT   120                  // предел задела: разность считается байтом
#define MUS_RESERVE 384                  // неприкосновенный запас байт в кольце
#define STAGE_HALF  8192                 // половина страницы подкачки
#define REC_MAX     332                  // самая длинная запись потока

// Кольцо и переменные прерывания — в Win0 (music_s.s)
extern uint8_t mus_ring[RING_SIZE];
extern uint16_t mus_rd;
extern uint8_t mus_pause, mus_played, mus_pushed;
extern uint16_t mus_out;

// Поток под чип выбирается в настройках (11_sound.md): для OPL3 — записи в регистры FM,
// для AY — тот же трек, сведённый конвертером на три канала (MusicAy.cs), для двух YM2203 —
// на шесть FM-каналов (MusicFm.cs; SSG обоих чипов остаётся эффектам).
static const char *const pak_name[2][3] = {
	{ "OXZ/UFO/MUSIC.PAK", "OXZ/UFO/MUSICAY.PAK", "OXZ/UFO/MUSICFM.PAK" },
	{ "OXZ/TFTD/MUSIC.PAK", "OXZ/TFTD/MUSICAY.PAK", "OXZ/TFTD/MUSICFM.PAK" },
};
extern void mus_out_opl3(void);
extern void mus_out_ay(void);
extern void mus_out_ym(void);

// Номер потока по режиму звука: 0 — OPL3, 1 — AY, 2 — FM-части YM2203
static uint8_t mus_stream(void)
{
	return opt.sound == SND_OPL3_AY ? 0 : opt.sound == SND_2YM ? 2 : 1;
}

// Состояние плеера — в памяти банка: Win1 заполнен почти доверху (20 §11).
typedef struct {
	fat_file_t file;
	uint32_t base, size, loop, pos;      // трек в файле и текущее чтение
	uint16_t off, left;                  // где читаем в странице подкачки и сколько осталось
	uint16_t wr;                         // указатель записи в кольцо
	uint16_t id;                         // что играет (0 — ничего)
	uint8_t opened;                      // 0 не пробовали, 1 есть, 2 нет
	uint8_t page;                        // страница подкачки (PG_NONE — не выделена)
	uint8_t eof;                         // поток кончился
	uint8_t rest;                        // остаток длинной паузы, не влезшей в задел
	uint16_t magic;                      // память банка не обнуляется — признак «уже заведено»
	uint16_t seed;                       // жребий треков в группе (свой, не ГСЧ кампании)
} mus_t;
static mus_t __at(0xBF00) M;

// Заглушить чип: снять ноты со всех каналов (иначе после конца трека последняя нота
// тянется вечно). У каждого чипа это делается по-своему: OPL3 — сбросом бита KEY-ON,
// AY — нулевой громкостью, YM2203 — key-off FM-каналов (SSG не трогаем, она под эффекты).
static void mus_silence(void)
{
	uint8_t s = mus_stream();
	if (s == 0) {
		for (uint8_t i = 0; i < 9; i++) {
			OPL3_ADDR0 = 0xB0 + i; OPL3_DATA0 = 0;
		}
		for (uint8_t i = 0; i < 3; i++) {
			OPL3_ADDR1 = 0xB0 + i; OPL3_DATA1 = 0;
		}
		return;
	}
	for (uint8_t chip = 0; chip < 2; chip++) {
		AY_ADDR = s == 2 ? 0xF8 + chip : 0xFF - chip;   // выбор чипа (для FM ещё и «звук включён»)
		if (s == 2) {
			for (uint8_t i = 0; i < 3; i++) { AY_ADDR = 0x28; AY_DATA = i; }
		} else {
			for (uint8_t i = 8; i < 11; i++) { AY_ADDR = i; AY_DATA = 0; }
			break;                                      // музыка AY — только на первом чипе
		}
	}
}

static uint8_t mus_open(void)
{
	if (M.opened) return M.opened == 1;
	M.opened = 2;
	if (fat_mount()) return 0;
	if (fat_open(pak_name[res_game() == 2][mus_stream()], &M.file)) return 0;
	M.opened = 1;
	return 1;
}

// Запись каталога пакета по номеру ресурса: смещение тела и размер (sdres.c грузит ресурс
// целиком в слот 64 КБ, а трек до 124 КБ — поэтому свой поиск).
static uint8_t mus_locate(uint16_t id)
{
	uint8_t hdr[16];
	if (fat_read(&M.file, 0, near_phys(hdr), 16)) return 0;
	uint16_t n = hdr[6] | ((uint16_t)hdr[7] << 8);
	for (uint16_t i = 0; i < n; i++) {
		if (fat_read(&M.file, 16 + (uint32_t)i * 16, near_phys(hdr), 16)) return 0;
		if ((hdr[0] | ((uint16_t)hdr[1] << 8)) != id) continue;
		M.base = (uint32_t)(hdr[4] | ((uint16_t)hdr[5] << 8)) << 9;
		M.size = hdr[6] | ((uint32_t)hdr[7] << 8) | ((uint32_t)hdr[8] << 16);
		return 1;
	}
	return 0;
}

// Прочитать в страницу подкачки половину, в которой лежит M.pos
static uint8_t stage_load(void)
{
	uint32_t left = M.size - M.pos;
	uint16_t want = left > STAGE_HALF ? STAGE_HALF : (uint16_t)left;
	if (!want) return 0;
	if (fat_read(&M.file, M.base + M.pos, FAR(M.page, 0), want)) return 0;
	M.off = 0;
	// Хвост короче самой длинной записи оставляем следующему чтению: так запись никогда
	// не окажется разорванной между половинами (20 §7.2).
	M.left = want > REC_MAX && left > want ? want - REC_MAX : want;
	return M.left != 0;
}

// Таблица MUSGRP (22 §1.3): u8 n_grp, n_grp x {first, count}, u8 n_kind, n_kind x тип,
// затем пул номеров типов. Роль — тема экрана (меню, разбор, концовки); группа — набор
// треков, из которого оригинал тянет жребий (GMGEO, GMINTER).
uint16_t mus_kind(uint8_t role) __banked
{
	res_t r;
	uint8_t b[2];
	if (!res_find(RES_MUSGRP, &r)) return 0;
	far_read(r.phys, b, 1);
	uint16_t off = 1 + (uint16_t)b[0] * 2;       // за окнами групп — число ролей
	far_read(r.phys + off, b, 1);
	if (role >= b[0]) return 0;
	far_read(r.phys + off + 1 + role, b, 1);
	return b[0] == 0xFF ? 0 : MUS_BASE + b[0];
}

// Случайный трек группы — как getRandomMusic в оригинале. Жребий свой, не ГСЧ кампании:
// иначе музыка сдвинула бы всю случайность игры и сломала воспроизводимость тестов (21 §5).
uint16_t mus_pick(uint8_t grp) __banked
{
	res_t r;
	uint8_t b[2];
	if (!res_find(RES_MUSGRP, &r)) return 0;
	far_read(r.phys, b, 1);
	if (grp >= b[0]) return 0;
	uint16_t ngrp = b[0];
	far_read(r.phys + 1 + (uint16_t)grp * 2, b, 2);
	if (!b[1]) return 0;
	uint16_t pool = 1 + ngrp * 2;
	uint8_t nk[1];
	far_read(r.phys + pool, nk, 1);
	pool += 1 + nk[0];
	M.seed = M.seed * 25173 + 13849;
	uint8_t k = (uint8_t)((M.seed >> 8) % b[1]);
	far_read(r.phys + pool + b[0] + k, nk, 1);
	return MUS_BASE + nk[0];
}

void mus_stop(void) __banked
{
	mus_state = 0;
	M.id = 0;
	mus_silence();
}

#define MUS_MAGIC 0x4D55

// Смена режима звука в настройках: чип и пакет потока другие, поэтому сначала глушим
// прежний чип, потом забываем открытый файл и начинаем тот же трек заново.
void mus_set_mode(uint8_t mode) __banked
{
	uint16_t id = M.magic == MUS_MAGIC ? M.id : 0;
	if (mode == opt.sound) return;
	mus_stop();
	opt.sound = mode;
	M.opened = 0;
	M.id = 0;
	if (id) mus_play(id);
}

void mus_play(uint16_t id) __banked
{
	if (M.magic != MUS_MAGIC) { memset(&M, 0, sizeof M); M.page = PG_NONE; M.magic = MUS_MAGIC; }
	if (id == M.id && mus_state) return;   // тот же трек — не перезапускать (21 §2)
	mus_state = 0;
	M.id = 0;
	if (!mus_open()) return;
	if (M.page == PG_NONE) {
		M.page = pg_alloc(1, 1);
		if (M.page == PG_NONE) return;
	}
	if (!mus_locate(id)) return;
	uint8_t h[12];
	if (fat_read(&M.file, M.base, near_phys(h), 12)) return;
	M.loop = h[0] | ((uint32_t)h[1] << 8) | ((uint32_t)h[2] << 16) | ((uint32_t)h[3] << 24);
	M.pos = 12;                            // тело идёт за 12-байтным заголовком
	if (M.loop != 0xFFFFFFFFul) M.loop += 12;
	M.wr = 0;
	mus_rd = (uint16_t)mus_ring;   // ISR читает по абсолютному адресу, а не по смещению
	mus_pause = 0;
	mus_played = 0;
	mus_pushed = 0;
	M.eof = 0;
	M.rest = 0;
	uint8_t s = mus_stream();
	mus_out = s == 0 ? (uint16_t)&mus_out_opl3 : s == 1 ? (uint16_t)&mus_out_ay : (uint16_t)&mus_out_ym;
	if (!stage_load()) return;
	M.id = id;
	mus_state = 1;
	mus_fill();
}

// ISR пишет mus_rd двумя байтами — читаем сторожем, без запрета прерываний
static uint16_t rd_get(void)
{
	uint16_t a, b;
	do { a = mus_rd; b = mus_rd; } while (a != b);
	return a;
}

// Конец потока: либо перемотать на точку повтора, либо признать трек доигранным
static uint8_t track_wrap(void)
{
	if (M.loop == 0xFFFFFFFFul) return 0;
	M.pos = M.loop;
	return stage_load();
}

void mus_fill(void) __banked
{
	if (mus_state != 1) return;
	uint16_t rd = rd_get() - (uint16_t)mus_ring;   // в смещение кольца
	for (;;) {
		uint8_t ahead = (uint8_t)(mus_pushed - mus_played);
		if (ahead >= MUS_AHEAD) break;
		// Задел считается байтом, поэтому он не должен переваливать за половину диапазона:
		// иначе «на 130 вперёд» неотличимо от «на 126 назад» и ISR играет несуществующие кадры.
		// Запись-пауза добавляет до 126 кадров сразу, поэтому длинные паузы режутся на части.
		if (M.rest) {
			uint8_t can = MUS_LIMIT - ahead;
			if (!can) break;
			uint8_t k = M.rest > can ? can : M.rest;
			if (RING_SIZE - M.wr < 2) { mus_ring[M.wr] = 0x80; M.wr = 0; }
			mus_ring[M.wr++] = 0x80 + k;
			M.rest -= k;
			mus_pushed += k;
			continue;
		}
		uint16_t space = (rd > M.wr ? rd - M.wr : RING_SIZE - (M.wr - rd)) - 1;
		if (space < MUS_RESERVE) break;
		if (!M.left) { if (!stage_load()) break; }
		far_t p = FAR(M.page, M.off);
		uint8_t b = far_byte(p), frames;
		uint16_t len;
		if (b == 0xFF) { if (!track_wrap()) { M.eof = 1; } break; }
		if (b >= 0x81) {
			M.rest = b - 0x80;
			M.off++; M.left--; M.pos++;
			continue;
		}
		else {
			uint8_t n1 = far_byte(p + 1 + (uint16_t)b * 2);
			len = 2 + 2 * ((uint16_t)b + n1);
			frames = 1;
		}
		if (len + 1 > space) break;
		if (M.left < len) { M.pos += M.left; M.left = 0; continue; }
		if (RING_SIZE - M.wr < len + 1) {   // запись не влезает до конца кольца
			mus_ring[M.wr] = 0x80;          // метка «читать с начала»
			M.wr = 0;
		}
		far_read(p, mus_ring + M.wr, len);
		M.wr += len;
		M.off += len;
		M.left -= len;
		M.pos += len;
		mus_pushed += frames;                 // только после того, как байты легли
	}
}

void mus_pump(void) __banked
{
	if (mus_state != 1) return;
	mus_fill();
	if (M.eof && (uint8_t)(mus_pushed - mus_played) == 0) mus_stop();
}
