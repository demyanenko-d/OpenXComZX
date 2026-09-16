// Банк 25: тень глобуса (день и ночь) — OpenXcom Globe::drawShadow (Globe.cpp:176–245,
// 986–1008) без сезонов, ступенчато по блокам: уровень k = ⌊тень/3⌋ (0..10) один на блок
// 8x4 пикселя — по центру блока (project_docs/globe.md §6.4, модель tools/globe_shadow.js
// BLOCK=8x4; оригинал тоже ступенчатый).
//
// Вывод. Строка заднего буфера с тенью заранее заполняется: x 0..255 — добавка суши
// L = ⌊(v_k − шум)/3⌋, x 256..511 — цвет океана O = OCEAN + (v_k − шум) (v_k = 3k + 1, 0 и 31 у
// крайних уровней). Проход строк (globe_s.s) кладёт сушу BLT2 по полубайтам с насыщением
// (узор + L = ровно getLandShadow), океан — копией из x + 256. Отрезок уровня строки блоков —
// одно 2D DMA на 4 строки из образца уровня (4 строки по 512: L и O через 256, шум — сдвиг
// образца по строке блоков); края диска на зумах 0–1 (где строки блока разной ширины) —
// построчно.
//
// Страницы: shp — образцы уровней 0..7 (по 2 КБ), shp + 1 — уровни 8..10 и таблицы кадра
// (её возвращает globe_shadow: флаги строк GLOBE_SH_FLG).
//
// Уровень блока: 2t = −500·(e·s) = TX[столбец] + TY[строка блоков] + TZ[z центра] (таблицы на
// кадр — лестницами без умножений, z центров — globe_sh_tab.h), уровень — таблица SH_LUT по 2t.
// Здесь — подготовка кадра (солнце, узоры, таблицы); проход блоков и строк — globe_sh_s.s.
#include <stdint.h>
#include <string.h>
#include "tsconf.h"
#include "memmap.h"
#include "pages.h"
#include "far.h"
#include "state.h"
#include "globe.h"
#include "globe_sh_tab.h"

int32_t __mulsint2slong(int16_t a, int16_t b);
#define MUL(a, b) __mulsint2slong((int16_t)(a), (int16_t)(b))
#define SHR14(v)  ((int16_t)((uint32_t)((int32_t)(v) << 2) >> 16))
#define sin16(a)  globe_sin((uint16_t)(a))
#define cos16(a)  globe_sin((uint16_t)(a) + 0x4000)

// Страница тени shp + 1 (Win3 на время расчёта; globe_sh_s.s — те же смещения); образец уровня
// k — страница shp + k / 8, смещение (k % 8) · 2048: 4 строки по 512 (L, O через 256)
#define SH_TX     0x2A00                // 2t от столбца блока [32] мл., ст. (+128)
#define SH_TZ     0x2B00                // 2t от z [256] мл., ст. (+256)
#define SH_Z8     0x3800                // z центров блоков зума [25 × 16] (с sh_z8, строки по 16)
#define SH_LUT    0x3D00                // уровень по 2t + 128 [256]
#define NPAT      11

// Пороги уровней в t (ночь уровня k — t ≥ TB_k, как (Sint16) в shade_gradient)
static const int8_t tb[10] = { -34, -12, -8, -5, -2, 2, 5, 8, 14, 39 };

// Пары диска строк (как globe.c rows_init) — таблицей: банк 24 читает через globe_rows_pl
void globe_rows_pl(uint8_t zoom, uint8_t *dst) __banked
{
	const uint8_t *s = sh_row + (uint16_t)zoom * (GLOBE_H * 2);
	for (uint8_t y = 0; y < GLOBE_H; y++, dst += 6, s += 2) { dst[0] = s[0]; dst[1] = s[1]; }
}

static uint8_t shp = PG_NONE, pat_ocean = 0xFF, z8_zoom = 0xFF;

// globe_sh_s.s: проход блоков и строк
void sh_rows(void);
extern uint16_t sh_zi;
extern uint8_t sh_page;
extern int32_t sh_ty, sh_dty;

// ---------------------------------------------------------------- время

uint16_t globe_sunlon(void) __banked
{
	uint8_t h = ST->hour + 18;
	if (h >= 24) h -= 24;
	uint32_t sec = (uint32_t)h * 3600u + (uint16_t)ST->minute * 60u + ST->second;   // 0..86399
	uint16_t day = (uint16_t)((sec * 3107u) >> 12);                               // * 65536 / 86400
	return (uint16_t)(0x4000 - day);
}

// ---------------------------------------------------------------- узоры

// Шум: 256 значений 0..3 (как rand() % 4 OpenXcom)
static uint8_t noise_at(uint16_t i, uint8_t salt)
{
	uint16_t x = i * 0x9E37u + salt * 0x79B9u;
	x ^= x >> 7; x *= 0x2F1Du; x ^= x >> 9;
	return (uint8_t)x;
}

static void patterns(uint8_t ocean)
{
	for (uint8_t k = 0; k < NPAT; k++) {
		pg_map3(shp + (k >> 3));
		uint8_t *p = (uint8_t *)(0xC000 + (uint16_t)(k & 7) * 2048);
		uint8_t v = k == 0 ? 0 : k >= 10 ? 31 : (uint8_t)(3 * k + 1);
		for (uint8_t r = 0; r < 4; r++, p += 512)
			for (uint16_t x = 0; x < 256; x++) {
				uint8_t n = noise_at(x + ((uint16_t)r << 8), 1) & 3, s = v > n ? v - n : 0;
				p[x] = s / 3;
				p[x + 256] = ocean + s;
			}
	}
	pg_map3(shp + 1);                                   // уровень по 2t: ⌊2t / 2⌋ = ⌊t⌋ против порогов
	uint8_t *lut = (uint8_t *)(0xC000 + SH_LUT);
	for (uint16_t i = 0; i < 256; i++) {
		int8_t t = (int8_t)((int16_t)(i - 128) >> 1);
		uint8_t k = 0;
		while (k < 10 && t >= tb[k]) k++;
		lut[i] = k;
	}
	pat_ocean = ocean;
}

// ---------------------------------------------------------------- кадр

// Лестница (globe_sh_s.s sh_ramp): n значений старшего слова v, v += dv (16.16) — младшие байты
// с adr, старшие с adr + hs
extern uint16_t sh_radr, sh_rhs, sh_rn;
extern int32_t sh_rv, sh_rdv;
void sh_ramp(void);
static void ramp(uint16_t off, uint16_t n, uint16_t hs, int32_t v, int32_t dv)
{
	sh_radr = 0xC000 + off; sh_rhs = hs; sh_rn = n; sh_rv = v; sh_rdv = dv;
	sh_ramp();
}

// 2t = −500·(sx·X + sy·Y + sz·Z) / (R·16384) (X, Y — пиксели от центра окна, Z — z8 / 255·R);
// центры блоков: X = 8c − 124, Y = 4b − 98. Лестницы отбрасывают дробь трёх слагаемых —
// в среднем 1.5, её возвращает TY. Множители по зумам — таблицей: деление 32 бит в SDCC
// стоит ~11 400 тактов (4 деления на кадр = 0.16 кадра).
// 1984000/r и 1568000/r (как делило C), шаги — с 16-кратной точностью: 2048000/r и 1024000/r
// (сдвиг 7 вместо 3), иначе усечение множителя уводит лестницу на ~0.04 единицы 2t за таблицу
static const int16_t zk[GLOBE_ZOOMS][4] = {
	{ 22044, 22756, 17422, 11378 }, { 16533, 17067, 13066, 8533 }, { 11022, 11378, 8711, 5689 },
	{ 7085, 7314, 5600, 3657 }, { 4408, 4551, 3484, 2276 }, { 2755, 2844, 2177, 1422 },
};
static void tables(uint8_t zoom, int16_t sx, int16_t sy, int16_t sz)
{
	const int16_t *k = zk[zoom];
	ramp(SH_TX, 32, 128, MUL(k[0], sx) >> 3, -MUL(k[1], sx) >> 7);
	ramp(SH_TZ, 256, 256, 0, -(2000L * sz) / 255);
	sh_ty = (MUL(k[2], sy) >> 3) + 0x18000L;                           // 2t строки блоков 0 · 65536
	sh_dty = -MUL(k[3], sy) >> 7;
}

uint8_t globe_shadow(uint16_t lon, int16_t lat, uint8_t zoom, uint16_t sunlon, uint8_t ocean) __banked
{
	if (shp == PG_NONE) shp = pg_alloc(2, 1);
	if (shp == PG_NONE) return PG_NONE;
	uint8_t old = pg_map3(shp + 1), oc = pat_ocean;
	if (oc != ocean) { patterns(ocean); pg_map3(shp + 1); }
	// солнце в осях вида: s = (sin d, −sinC·cos d, cosC·cos d), d = λs − λ0
	uint16_t dl = sunlon - lon;
	int16_t sd = sin16(dl), cd = cos16(dl), sc = sin16((uint16_t)lat), cc = cos16((uint16_t)lat);
	int16_t sx = sd, sy = -SHR14(MUL(sc, cd)), sz = SHR14(MUL(cc, cd));
	tables(zoom, sx, sy, sz);
	if (z8_zoom != zoom) {
		memcpy((void *)(0xC000 + SH_Z8), sh_z8 + (uint16_t)zoom * 400, 400);
		z8_zoom = zoom;
	}
	sh_zi = (uint16_t)zoom * (GLOBE_H * 2);
	sh_page = shp;
	{ int16_t *dg = (int16_t *)(0xC000 + 0x3FF0); dg[0] = sx; dg[1] = sy; dg[2] = sz; }   // отладка: солнце кадра
	sh_rows();
	pg_map3(old);
	return shp + 1;
}
