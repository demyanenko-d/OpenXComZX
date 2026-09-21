// Графика миссии: тайлсеты наборов, таблица частей для блита и листы спрайтов.
// Вынесено из экрана боя (банк 28 переполнен) в банк логики миссии; всё, что собирается,
// пишется в дальнюю память — экран боя забирает таблицы к себе одним far_read.
//
// Таблица частей: номер части в клетке (1..255) — сквозной по наборам миссии, строка
// таблицы для части t лежит по индексу t-1 (часть 0 в клетках не встречается). Запись
// набора MCDSET — 12 байт (16 §2.3): кадр, P_Level, флаги, bigwall, тип, T_Level,
// TU_Walk, Alt_MCD, Frame[7].
#include <stdint.h>
#include <string.h>
#include "pages.h"
#include "far.h"
#include "res.h"
#include "pathfind.h"
#include "loader.h"

#define MC_SIZE 12                       // байт на часть в таблице набора

// Строка таблицы частей: кадр тайлсета в виде, готовом для блита DMA
//   [0] страница источника, [1..2] смещение, [3] DMALEN (w/2-1), [4] высота,
//   [5] dx, [6] dy (угол кадра в клетке), [7] ширина
static void tile_entry(uint8_t *p, far_t tphys, far_t fdata, uint16_t fr)
{
	uint8_t e[6];
	far_read(tphys + 4 + (uint32_t)fr * 6, e, 6);
	far_t src = fdata + ((uint32_t)((uint16_t)e[4] | ((uint16_t)e[5] << 8)) << 1);
	uint16_t so = FAR_OFFS(src);
	p[0] = FAR_PAGE(src);
	p[1] = (uint8_t)so; p[2] = (uint8_t)(so >> 8);
	p[3] = e[2] ? (uint8_t)(e[2] / 2 - 1) : 0;
	p[4] = e[3];
	p[5] = e[0]; p[6] = e[1];
	p[7] = e[2];
}

uint8_t sheet_load(uint16_t res, uint8_t *page, uint8_t *np, uint8_t n,
		   const uint8_t *want, far_t dst) __banked
{
	res_t rt;
	if (*page == PG_NONE) {
		uint32_t sz = sdres_size(res);
		if (!sz) return 0;
		*np = (uint8_t)((sz + 16383) >> 14);
		*page = pg_alloc(*np, 1);
		if (*page == PG_NONE) return 0;
	}
	if (!sdres_load(res, *page, &rt)) return 0;
	uint16_t nfr = rt.a;
	far_t fdata = rt.phys + 4 + (uint32_t)nfr * 6;
	uint8_t e[TE_SIZE];
	for (uint8_t i = 0; i < n; i++) {
		uint16_t fr = want ? want[i] : i;
		memset(e, 0, TE_SIZE);
		if (fr < nfr) tile_entry(e, rt.phys, fdata, fr);
		far_write(dst + (uint32_t)i * TE_SIZE, e, TE_SIZE);
	}
	return 1;
}

uint16_t tiles_load(const tiles_req_t *q, uint8_t *pages, uint8_t *nps,
		    uint8_t *npages, uint8_t *ndoors) __banked
{
	uint8_t ns = q->ns > TL_SETS ? TL_SETS : q->ns;
	uint8_t nd = 0, npg = 0;
	// Не оставлять кадры прошлой карты: если набор не загрузится, его части не рисуются
	far_fill(FAR(q->page, TL_TILES), 0, 256 * TE_SIZE);
	far_fill(FAR(q->page, TL_YOFS), 0, 256);
	far_fill(FAR(q->page, PF_TU), 0, 768);
	sdres_flush();                       // кэш ресурсов отдаёт страницы: тайлсеты важнее
	uint16_t total = 0;                  // всего частей во всех наборах миссии
	for (uint8_t s = 0; s < ns; s++) {
		res_t rm;
		if (res_find(q->set[s], &rm)) total += rm.a;
	}
	uint16_t vnext = 255;                // номера частей «дверь НЛО открыта» — сверху вниз
	uint16_t part = 0;                   // сквозной номер части миссии
	for (uint8_t s = 0; s < ns && part < 256; s++) {
		res_t rm, rt;
		if (!res_find(q->set[s], &rm)) continue;
		// Тайлсет грузим только у наборов, чьи части реально попали на карту: пустые наборы
		// (те же BLANKS) иначе съедают страницы, которых потом не хватает кораблю и НЛО.
		uint8_t need = 0;
		for (uint16_t i = 0; i < rm.a && part + i < 256; i++) {
			uint16_t g = part + i;
			if (q->used[g >> 3] & (1 << (g & 7))) { need = 1; break; }
		}
		if (!need) { part += rm.a; continue; }
		uint32_t tsz = sdres_size(q->tset[s]);
		uint8_t np = (uint8_t)((tsz + 16383) >> 14);
		uint8_t pg = np ? pg_alloc(np, 1) : PG_NONE;
		if (pg == PG_NONE) break;
		if (!sdres_load(q->tset[s], pg, &rt)) { pg_free(pg, np); break; }
		pages[npg] = pg; nps[npg] = np; npg++;
		uint16_t nfr = rt.a;
		far_t fdata = rt.phys + 4 + (uint32_t)nfr * 6;
		uint16_t base = part;            // номер первой части набора (в нём же номера alt)
		for (uint16_t i = 0; i < rm.a && part < 256; i++, part++) {
			uint8_t mc[MC_SIZE], e[TE_SIZE];
			far_read(rm.phys + (uint32_t)i * MC_SIZE, mc, MC_SIZE);
			uint16_t fr = (uint16_t)mc[0] | ((uint16_t)mc[1] << 8);
			if (!part || fr >= nfr) continue;
			tile_entry(e, rt.phys, fdata, fr);
			far_write(FAR(q->page, TL_TILES) + (uint32_t)(part - 1) * TE_SIZE, e, TE_SIZE);
			far_write(FAR(q->page, TL_YOFS) + (part - 1), &mc[2], 1);
			// Цена прохода (255 — стена) и двери: распашная меняется на часть Alt_MCD,
			// дверь НЛО сдвигается — ей заводится своя часть с открытым кадром Frame[7].
			far_write(FAR(q->page, PF_TU) + part, &mc[7], 1);
			if ((mc[3] & 8) && mc[8] && base + mc[8] < 256) {
				uint8_t v = (uint8_t)(base + mc[8]);
				far_write(FAR(q->page, PF_ALT) + part, &v, 1);
				v = far_byte(rm.phys + (uint32_t)mc[8] * MC_SIZE + 5) & 3;
				far_write(FAR(q->page, PF_ALTSLOT) + part, &v, 1);
				nd++;
			} else if ((mc[3] & 4) && vnext > total) {
				uint16_t f7 = (uint16_t)mc[9] | ((uint16_t)mc[10] << 8);
				if (f7 < nfr) {
					uint16_t vp = vnext--;
					tile_entry(e, rt.phys, fdata, f7);
					far_write(FAR(q->page, TL_TILES) + (uint32_t)(vp - 1) * TE_SIZE, e, TE_SIZE);
					far_write(FAR(q->page, TL_YOFS) + (vp - 1), &mc[2], 1);
					uint8_t v = 0;
					far_write(FAR(q->page, PF_TU) + vp, &v, 1);   // открытая дверь свободна
					v = (uint8_t)vp;
					far_write(FAR(q->page, PF_ALT) + part, &v, 1);
					v = (uint8_t)((mc[5] & 3) | 0x80);
					far_write(FAR(q->page, PF_ALTSLOT) + part, &v, 1);
					nd++;
				}
			}
		}
	}
	*npages = npg;
	*ndoors = nd;
	return part;
}
