// Графика миссии: тайлсеты наборов, таблица частей для блита и листы спрайтов.
// Вынесено из экрана боя (банк 28 переполнен) в банк логики миссии; всё, что собирается,
// пишется в рабочие страницы боя (pathfind.h, loader.h) — экран боя забирает таблицы
// к себе одним far_read, а свойства частей читает прямо оттуда поиск пути.
//
// Таблица частей: номер части в клетке (1..255) — сквозной по наборам миссии, строка
// таблицы для части t лежит по индексу t-1 (часть 0 в клетках не встречается). Запись
// набора MCDSET — 16 байт (16 §2.3): кадр, P_Level, флаги, bigwall, тип, T_Level,
// TU_Walk, Alt_MCD, Frame[1..7].
#include <stdint.h>
#include <string.h>
#include "pages.h"
#include "far.h"
#include "res.h"
#include "pathfind.h"
#include "loader.h"

extern uint8_t door_an_n;

#define MC_SIZE 16                       // байт на часть в таблице набора
#define UD_MAX  TL_UDMAX                 // дверей НЛО на миссию (частей с флагом UFO_Door)

typedef struct { uint8_t part, e[TE_SIZE], yofs, tlevel, flags, slot, tu; } udoor_t;

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

// Заведённая часть двери НЛО: кадр, смещение и свойства закрытой створки, своя цена прохода
static void ud_part(uint8_t page, uint8_t p, const uint8_t *e, const udoor_t *d, uint8_t tu)
{
	far_write(FAR(page + 1, TL_TILES) + (uint16_t)(p - 1) * TE_SIZE, e, TE_SIZE);
	far_write(FAR(page + 1, TL_YOFS) + (p - 1), &d->yofs, 1);
	far_write(FAR(page, PF_TU) + p, &tu, 1);
	far_write(FAR(page, PF_TLEVEL) + p, &d->tlevel, 1);
	far_write(FAR(page, PF_FLAGS) + p, &d->flags, 1);
}

uint16_t tiles_load(const tiles_req_t *q, uint8_t *pages, uint8_t *nps,
		    uint8_t *npages, uint8_t *ndoors) __banked
{
	uint8_t ns = q->ns > TL_SETS ? TL_SETS : q->ns;
	uint8_t nd = 0, npg = 0;
	// Не оставлять кадры прошлой карты: если набор не загрузится, его части не рисуются
	far_fill(FAR(q->page + 1, TL_TILES), 0, 256 * TE_SIZE);
	far_fill(FAR(q->page + 1, TL_YOFS), 0, 256);
	far_fill(FAR(q->page, PF_TU), 0, PF_PROPS);   // свойства частей — в первой странице
	far_fill(FAR(q->page, PF_UFOOPEN), 0, 1);     // открытых дверей НЛО нет
	far_fill(FAR(q->page + 1, TL_UDIDX), 0, 256);
	door_an_n = 0;                            // створки прошлой миссии больше не едут (doors.c)
	sdres_flush();                       // кэш ресурсов отдаёт страницы: тайлсеты важнее
	// Двери НЛО открываются заменой на свою часть с открытым кадром (Frame[7]), а пока створка едет
	// (Tile::animate, кадры 1..7 по 100 мс) — на часть «в движении», которой бой меняет кадр. Номера
	// им — из тех, что на карте не встречаются (не в клетках и не открытая половина распашной двери):
	// верхних свободных номеров не хватает — в миссии бывает 252 части из 255, и двери НЛО молча
	// оставались закрытыми, боец проходил сквозь них. На «в движении» номера не хватило — створка
	// открывается сразу.
	udoor_t ud[UD_MAX];
	uint8_t nud = 0, taken[32];
	memcpy(taken, q->used, sizeof taken);
	taken[0] |= 1;                       // часть 0 — «пусто»
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
			far_write(FAR(q->page + 1, TL_TILES) + (uint32_t)(part - 1) * TE_SIZE, e, TE_SIZE);
			far_write(FAR(q->page + 1, TL_YOFS) + (part - 1), &mc[2], 1);
			// Цена прохода (255 — стена) и двери: распашная меняется на часть Alt_MCD,
			// дверь НЛО сдвигается — ей заводится своя часть с открытым кадром Frame[7].
			far_write(FAR(q->page, PF_TU) + part, &mc[7], 1);
			far_write(FAR(q->page, PF_TLEVEL) + part, &mc[6], 1);
			far_write(FAR(q->page, PF_FLAGS) + part, &mc[3], 1);
			if ((mc[3] & 8) && mc[8] && base + mc[8] < 256) {
				uint8_t v = (uint8_t)(base + mc[8]);
				far_write(FAR(q->page, PF_ALT) + part, &v, 1);
				taken[v >> 3] |= (uint8_t)(1 << (v & 7));
				v = far_byte(rm.phys + (uint32_t)mc[8] * MC_SIZE + 5) & 3;
				far_write(FAR(q->page, PF_ALTSLOT) + part, &v, 1);
				nd++;
			} else if ((mc[3] & 4) && nud < UD_MAX) {
				if (mc[15] < nfr) {          // части открытой двери заведутся после всех наборов
					udoor_t *d = &ud[nud];
					d->part = (uint8_t)part;
					tile_entry(d->e, rt.phys, fdata, mc[15]);
					d->yofs = mc[2]; d->tlevel = mc[6]; d->flags = mc[3]; d->tu = mc[7];
					d->slot = (uint8_t)((mc[5] & 3) | 0x80);
					for (uint8_t k = 0; k < 7; k++) {   // кадры створки 1..7
						memset(e, 0, TE_SIZE);
						if (mc[9 + k] < nfr) tile_entry(e, rt.phys, fdata, mc[9 + k]);
						far_write(FAR(q->page + 1, TL_UDFR) + (uint16_t)(nud * 7 + k) * TE_SIZE, e, TE_SIZE);
					}
					nud++;
				}
			}
		}
	}
	uint8_t vp = 255;
	for (uint8_t k = 0; k < nud; k++) {
		while (vp && (taken[vp >> 3] & (1 << (vp & 7)))) vp--;
		if (!vp) break;
		udoor_t *d = &ud[k];
		uint8_t op = vp--;                   // открытая створка: проход свободен
		ud_part(q->page, op, d->e, d, 0);
		while (vp && (taken[vp >> 3] & (1 << (vp & 7)))) vp--;
		if (vp) {                            // створка в движении: с кадра 1, на кадре 7 её
			uint8_t an = vp--, v;            // меняют на открытую (Alt)
			far_read(FAR(q->page + 1, TL_UDFR) + (uint16_t)k * 7 * TE_SIZE, d->e, TE_SIZE);
			ud_part(q->page, an, d->e, d, d->tu);
			far_write(FAR(q->page, PF_ALT) + an, &op, 1);
			v = (uint8_t)(d->slot | 0x40);
			far_write(FAR(q->page, PF_ALTSLOT) + an, &v, 1);
			far_write(FAR(q->page + 1, TL_UDIDX) + an, &k, 1);
			op = an;
		}
		far_write(FAR(q->page, PF_ALT) + d->part, &op, 1);
		far_write(FAR(q->page, PF_ALTSLOT) + d->part, &d->slot, 1);
		nd++;
	}
	*npages = npg;
	*ndoors = nd;
	return part;
}
