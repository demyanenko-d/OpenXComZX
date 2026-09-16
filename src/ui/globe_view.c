// Банк 25: предрасчитанные виды глобуса с SD (GVIEW.PAK, конвертер Core/GlobeViews.cs;
// project_docs/globe.md §12.5). На зумах 0–2 вид берётся готовым: на каждую из 200 строк —
// число отрезков и пары (длина в парах − 1, текстура 0..13). Геометрии в кадре нет вовсе:
// остаются тень, вывод отрезков (globe_s.s _gl_rows_pre) и копия на экран.
//
// Сетка видов: поворот i из nLon (угол i · 65536 / nLon), наклон j из nTilt (угол
// (j − (nTilt − 1) / 2) · tstep); наклон ограничен ±27°, зумы 3–5 остаются рёберному рендеру.
// Поток вида читается прямо в страницу рёбер с EP_VIEW, указатели зума — с EP_VIDX; рёберный
// рендер их затирает, поэтому globe.c сбрасывает кэш через gview_reset.
#include <stdint.h>
#include "pages.h"
#include "far.h"
#include "res.h"
#include "fat.h"
#include "globe.h"

#define EP_VIEW   0x1800
#define EP_VIDX   0x3400
#define GV_Z      3

static fat_file_t gvf;
static uint8_t gv_state;                  // 0 — не открывали, 1 — есть, 2 — нет
static uint16_t gv_nlon[GV_Z], gv_ntilt[GV_Z], gv_tstep[GV_Z];
static uint32_t gv_isec[GV_Z], gv_dsec[GV_Z];
static uint8_t gv_izoom = 0xFF;           // зум, чьи указатели лежат в EP_VIDX
static uint16_t gv_cur = 0xFFFF;          // номер вида в EP_VIEW
static uint16_t gv_lon[GV_Z][72];         // углы поворота сетки (i * 65536 / nLon) — деление один раз

static uint32_t rd32(const uint8_t *p)
{
	return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

uint8_t gview_open(uint8_t ep) __banked
{
	uint8_t h[8 + GV_Z * 16];
	if (gv_state) return gv_state == 1;
	gv_state = 2;
	if (fat_mount()) return 0;
	{
		// Литерал лежит в странице банка 25, а fat_open — в банке 12: на время вызова окно
		// банков переключается и указатель становится мусором. Путь — в стек (он всегда виден).
		char path[20];
		const char *s = res_game() == 2 ? "OXZ/TFTD/GVIEW.PAK" : "OXZ/UFO/GVIEW.PAK";
		uint8_t i = 0;
		do { path[i] = s[i]; } while (s[i++]);
		if (fat_open(path, &gvf)) return 0;
	}
	if (fat_read(&gvf, 0, FAR(ep, EP_VIDX), sizeof h)) return 0;
	far_read(FAR(ep, EP_VIDX), h, sizeof h);
	if (h[0] != 'G' || h[1] != 'V' || h[2] != 'W' || h[3] != '1') return 0;
	if (h[4] < GV_Z || h[6] != 0) return 0;
	for (uint8_t z = 0; z < GV_Z; z++) {
		const uint8_t *p = h + 8 + (uint16_t)z * 16;
		gv_nlon[z] = p[0] | ((uint16_t)p[1] << 8);
		gv_ntilt[z] = p[2] | ((uint16_t)p[3] << 8);
		gv_tstep[z] = p[4] | ((uint16_t)p[5] << 8);
		gv_isec[z] = rd32(p + 8);
		gv_dsec[z] = rd32(p + 12);
		if (!gv_nlon[z] || !gv_ntilt[z] || !gv_tstep[z] || gv_nlon[z] > 72) return 0;
		for (uint16_t i = 0; i < gv_nlon[z]; i++)
			gv_lon[z][i] = (uint16_t)(((uint32_t)i * 65536u + gv_nlon[z] / 2) / gv_nlon[z]);
	}
	gv_state = 1;
	return 1;
}

// Ближайший вид сетки: точные углы вида и его номер (0 — предрасчёта для зума нет)
uint8_t gview_pick(uint8_t z, uint16_t *lon, int16_t *lat, uint16_t *iv) __banked
{
	if (z >= GV_Z || gv_state != 1) return 0;
	uint16_t n = gv_nlon[z], ts = gv_tstep[z];
	int16_t k = (int16_t)((gv_ntilt[z] - 1) >> 1);
	uint16_t i = (uint16_t)(((uint32_t)*lon * n + 32768u) >> 16);
	if (i >= n) i -= n;
	// 16 бит хватает: |lat| <= 16384, шаг <= 1638 (деление 32 бит в SDCC — 2 745 тактов)
	int16_t t = *lat, hs = (int16_t)(ts >> 1);
	int16_t j = t >= 0 ? (int16_t)((t + hs) / (int16_t)ts) : -(int16_t)((hs - t) / (int16_t)ts);
	if (j > k) j = k;
	if (j < -k) j = -k;
	*lon = gv_lon[z][i];                      // углы сетки — таблицей (деление было на кадр)
	*lat = (int16_t)(j * (int16_t)ts);
	*iv = (uint16_t)((j + k) * (int16_t)n + (int16_t)i);
	return 1;
}

// Указатели зума и сам вид -> страница рёбер ep
uint8_t gview_load(uint8_t ep, uint8_t z, uint16_t iv) __banked
{
	if (gv_izoom != z) {
		uint16_t nv = gv_nlon[z] * gv_ntilt[z];
		if (fat_read(&gvf, gv_isec[z] << 9, FAR(ep, EP_VIDX), ((uint32_t)nv + 1) * 2)) return 0;
		gv_izoom = z;
		gv_cur = 0xFFFF;
	}
	if (gv_cur == iv) return 1;
	uint16_t s0 = far_word(FAR(ep, EP_VIDX + iv * 2));
	uint16_t s1 = far_word(FAR(ep, EP_VIDX + iv * 2 + 2));
	gv_cur = 0xFFFF;
	if (s1 <= s0) return 0;
	if (fat_read(&gvf, (gv_dsec[z] + s0) << 9, FAR(ep, EP_VIEW), (uint32_t)(s1 - s0) << 9)) return 0;
	gv_cur = iv;
	return 1;
}

// Рёберный рендер затирает поток и указатели
void gview_reset(void) __banked
{
	gv_izoom = 0xFF;
	gv_cur = 0xFFFF;
}

// Привязка вида к сетке предрасчёта (scr_geo.c после поворота и смены зума)
uint8_t globe_snap(uint8_t zoom, uint16_t *lon, int16_t *lat) __banked
{
	uint16_t iv;
	return gview_pick(zoom, lon, lat, &iv);
}
