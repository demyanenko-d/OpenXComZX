;; Банк 24: глобус — кадр (перевод globe.c на ассемблер; project_docs/globe.md).
;;
;; Узор суши в OpenXcom привязан к экрану (texturedPolygon(…, 0, 0)): пиксель (x, y) берёт
;; TEXTURE[y & 31][x & 31]. Строка r узора t развёрнута в блок 256 байт (повтор 8 раз), и отрезок
;; [xs, xe) строки y — копия байтов блока со смещения xs. Блоки — в 8 страницах пула: страница
;; r / 4, блок (r & 3) · 14 + t, t = 13 — океан (globe.md §6.5).
;;
;; Кадр (render): вид сетки (все зумы, наклон до ±27°) — куски границ с карты (globe_view.s) -> тень ->
;; проход кусков (globe_s.s _gl_rows_edg); та же геометрия и новое солнце — только тень и проход
;; строк; иначе (вид вне сетки — центрирование на объекте) рёберный путь: таблицы произведений на 8 констант вида -> центры ячеек 30° -> отсев
;; (_gl_cull) -> по видимой ячейке: вершины (DMA в рабочую страницу, _gl_project) и рёбра
;; (_gl_edges) -> полосы строк без рёбер (сетка 5°) -> тень -> строки (_gl_rows). Затем копия
;; заднего буфера (строки экрана 280..479) на экран по кадровому прерыванию.
;;
;; Рабочая страница пула: #0000 таблицы произведений, #2000 проекции вершин ячейки, #2390 блок
;; ячейки из ресурса, #2AA0 записи рёбер ячейки, #3080 корзины строк, #3300 покрытие, #3800
;; пределы окна ячеек, #3900 видимые ячейки. Страница рёбер — globe_s.s.
;;
;; Соглашения. Банковые вызовы — аргументы в стеке (кладутся справа налево, байт — push af /
;; inc sp), чистит вызывающий, результат-байт A, 16 бит DE. Ядро — sdcccall(1): far_t в HL:DE (HL —
;; старшее слово), байт в A, 16 бит в HL; остальное — стек, чистит вызываемая. Внутренние
;; процедуры берут параметры из переменных. IX сохраняется. _DATA обнуляется при старте:
;; «нет значения» #FF ставит globe_draw при первом вызове (g_init).

	.module globe
	.optsdcc -mz80 sdcccall(1)

	.globl	_globe_draw, _globe_invalidate, b_globe_draw, b_globe_invalidate, _globe_prepare, b_globe_prepare
	.globl	render, r_sunonly, r_edges, rc_loop, rc_done, r_rows, r_bg, r_shadow, blit, dma_wait, dma_go, dma_blk
	.globl	far_dma, build_strips, rows_meta, bg_restore, tables, cell_of, dot3, bands, bd_loop, bd_fill, cm_fill, fill
	.globl	_gl_dbg, _gv_off
	.globl	_gl_eb, _gl_ec, _gl_wpg, _gl_epg, _gl_limb, _gl_gtex, _gl_res, _gl_eptr, _gl_sptr, _gl_nedge
	.globl	_gl_crp, _gl_cpp, _gl_cmp, _gl_vsp, _gl_nvis, _gl_cn, _gl_nb, _gl_bl
	.globl	_gl_pv, _gl_pd, _gl_pn, _gl_noz, _gl_k, _gl_kpg
	.globl	_gl_edges, _gl_rows, _gl_rows_pre, _gl_rows_edg, _gl_rows_cap, _gl_capok, _gl_capwant, _gl_bands, _gl_project, _gl_ktab, _gl_cull
	.globl	_sqz_q14, _sin5_q14, _cos5k_q14, _sin5k_q14
	.globl	_res_find, _res_game, _far_read, _far_copy, _far_fill, _far_byte, _pg_map3, _pg_alloc, _pg_win3
	.globl	_dbg_puts, _dbg_dec, ___mulsint2slong, ___muluint2ulong, _frames, _dma_fill_word, _st_, _ctx
	.globl	___sdcc_bcall_ehl
	.globl	_globe_sin, b_globe_sin, _globe_shadow, b_globe_shadow, _globe_rows_pl, b_globe_rows_pl
	.globl	_globe_sunlon, b_globe_sunlon, _gview_open, b_gview_open, _gview_pick, b_gview_pick
	.globl	_gview_load, b_gview_load, _gview_reset, b_gview_reset
	.globl	_globe_det, b_globe_det, detail

b_globe_draw		= 24
b_globe_prepare		= 24
b_globe_invalidate	= 24

RES_OFF		= 0x2000		; рабочая страница: проекции вершин ячейки
VSRC		= 0x2390		;   шапка блока ячейки: подблоки (DMA из ресурса)
VDAT		= 0x2400		;   данные подблока: вершины, рёбра (DMA из ресурса)
SUBC		= 0x3950		;   центры подблоков ячейки [4 x 6]
SUBP		= 0x3970		;   их проекции [4 x 6]
SUBV		= 0x3990		;   видимые подблоки [4]
STG		= 0x2AA0		;   записи рёбер ячейки
WB_BUCKET	= 0x3080		;   корзины строк
WB_COV		= 0x3300		;   покрытие строк рёбрами
WB_CM		= 0x3800		;   пределы окна ячеек [72] u16
WB_VIS		= 0x3900		;   видимые ячейки [72]
EP_BT		= 0x0000		; страница рёбер: текстура полосы строк без рёбер
EP_ROW		= 0x0200		;   записи строк (6 байт)
EP_EVB		= 0x0700		;   события края
EP_EVKB		= 0x0900
EP_EVKA		= 0x0A00
EP_BUCKET	= 0x1500		;   корзины (копия из рабочей)
EP_SHF		= 0x1700		;   флаги строк с тенью
EP_POOL		= 0x1800		;   записи рёбер
MAXCV		= 150			; вершин / рёбер в ячейке
NCELL		= 72
BACK_Y		= 280			; задний буфер: строки экрана 280..479
BACK_DAH	= (BACK_Y & 31) << 1
BACK_DAX	= SCREEN_PAGE + (BACK_Y >> 5)
NTEX		= 13
NBLK		= 14			; блоков узора на строку: 13 текстур + океан
GLOBE_H		= 200
GLOBE_SH_FLG	= 0x3000
PG_NONE		= 0xFF
SCREEN_PAGE	= 0x10
DATA_PAGE	= 0x05
DMA_RAM_RAM	= 0x01
DMA_FILL	= 0x04
DMA_ASZ		= 0x08
DMA_D_ALGN	= 0x10
DMA_S_ALGN	= 0x20
RES_GEOBORD_SCR	= 0x0120
RES_TEXTURE_DAT	= 0x0142
RES_GLOBE	= 0x0145
RES_GLOBEDET	= 0x0032
DT_CV		= 0x2000		; рабочая страница после кадра: центры групп деталей (6 байт)
DT_CP		= 0x2240		;   их проекции
DT_M		= 0x2480		;   пределы окна зума (u16)
DT_VIS		= 0x2540		;   число взятых групп, их номера (globe_det.c)
DT_REC		= 0x2600		;   записи групп (16 байт)
DT_V		= 0x2C00		;   вершины взятых групп -> их проекции x, y, z на месте (до #3800)
DT_VMAX		= 512
DT_GMAX		= 96
ST_ZOOM		= 72			; state_t.zoom
CTX_LON		= 6			; ctx.globe_lon, globe_lat
CTX_LAT		= 8
ARR		= NCELL * 16 + NCELL * 12	; кадр рёберного пути на стеке: записи ячеек, центры, проекции

	.area	_DATA

_gv_off::	.ds	1		; 1 — не брать предрасчитанные виды (сравнение путей)
_gl_dbg::	.ds	1		; 1 — печатать строку «globe:»
_gl_eb::	.ds	2		; проходы globe_s.s (см. там)
_gl_ec::	.ds	1
_gl_wpg::	.ds	1
_gl_epg::	.ds	1
_gl_limb::	.ds	1
_gl_gtex::	.ds	1
_gl_res::	.ds	2
_gl_eptr::	.ds	2
_gl_sptr::	.ds	2
_gl_nedge::	.ds	2
_gl_crp::	.ds	2
_gl_cpp::	.ds	2
_gl_cmp::	.ds	2
_gl_vsp::	.ds	2
_gl_nvis::	.ds	1
_gl_cn::	.ds	1
_gl_nb::	.ds	1
_gl_bl::	.ds	32
_gl_pv::	.ds	2
_gl_pd::	.ds	2
_gl_pn::	.ds	1
_gl_noz::	.ds	1
_gl_k::		.ds	4
_gl_kpg::	.ds	1

g_init:		.ds	1		; 1 — «нет значения» расставлены
work:		.ds	1		; страницы: рабочая, рёбер, узоров (0 — не выделены)
epage:		.ds	1
strip:		.ds	1
strip_set:	.ds	1		; набор узоров в страницах (#FF — нет)
valid:		.ds	1
pre_only:	.ds	1		; 1 — globe_prepare: кадр только в задний буфер (без копии на экран)
v_zoom:		.ds	1		; вид в заднем буфере
v_lon:		.ds	2
v_lat:		.ds	2
bg_zoom:	.ds	1		; зум, чей фон вне диска лежит в заднем буфере (#FF — затёрт)
row_zoom:	.ds	1		; зум пар диска в записях строк
row_meta:	.ds	1		; постоянные поля записей строк заполнены
cm_zoom:	.ds	1		; зум пределов окна ячеек
ocean:		.ds	1
v_sun:		.ds	1		; эпоха солнца (λs >> 7) кадра
geom:		.ds	1		; страница рёбер держит геометрию вида v_*
kc_ok:		.ds	1
g_r:		.ds	2
r_glon:		.ds	2
r_glat:		.ds	2
r_giv:		.ds	2
rg:		.ds	15		; res_t: GLOBE, TEXTURE.DAT, GEOBORD.SCR
rt:		.ds	15
rb:		.ds	15

	.area	_BANK24

;; рабочие переменные банка 24 (только его код; окно 2 кэшируется — быстрее окна 1)
kc:		.ds	32		; K построенных таблиц произведений
kr:		.ds	2		; 2^21 / R
ev:		.ds	18		; оси экрана в мировых координатах (Q14)
; render
r_lon:		.ds	2
r_lat:		.ds	2
r_z:		.ds	1
r_sun:		.ds	2
r_t0:		.ds	2
r_set:		.ds	1
r_bz:		.ds	1
r_rz:		.ds	1
r_ncell:	.ds	1
; детали (detail): вид последней проекции
dt_res:		.ds	15		; res_t GLOBEDET
dt_hdr:		.ds	24
dt_ng:		.ds	1
dt_nv:		.ds	2
dt_nk:		.ds	1
dt_g:		.ds	1
dt_vi:		.ds	2
dt_vo:		.ds	2
dt_voff:	.ds	2
dt_ok:		.ds	1
dt_z:		.ds	1
dt_lon:		.ds	2
dt_lat:		.ds	2
dt_t0:		.ds	2
dt_fr:		.ds	1
r_ct:		.ds	4		; far_t: записи ячеек, блоки ячеек, сетка 5°
r_bt:		.ds	4
r_gt:		.ds	4
r_h:		.ds	6		; nCell, nVert, nEdge
r_btpg:		.ds	1
r_btof:		.ds	2
r_cr:		.ds	2		; массивы кадра на стеке
r_cv:		.ds	2
r_cp:		.ds	2
r_v:		.ds	1
r_nvc:		.ds	1		; видимых ячеек (_gl_nvis портит отсев подблоков)
r_ns:		.ds	1		; подблоков в ячейке, видимых, текущий
r_nsv:		.ds	1
r_sv:		.ds	1
r_boff:		.ds	2		; смещение блока ячейки в странице блоков
r_whole:	.ds	1
r_dp:		.ds	2		; данные текущего подблока в рабочей странице		; 1 — данные всех подблоков ячейки уже в VDAT (зумы 0–2)
r_hdr:		.ds	2		; размер шапки блока ячейки
r_cv8:		.ds	1
r_vn:		.ds	2
r_en:		.ds	2
r_e0:		.ds	2
r_sp:		.ds	1		; страница флагов тени
; DMA
dg_sp:		.ds	1
dg_so:		.ds	2
dg_dp:		.ds	1
dg_do:		.ds	2
dg_len:		.ds	1
dg_num:		.ds	1
dg_ctrl:	.ds	1
fd_sp:		.ds	1
fd_so:		.ds	2
fd_dp:		.ds	1
fd_do:		.ds	2
fd_n:		.ds	2
fd_b:		.ds	2
bs_tp:		.ds	1		; узоры: страница и смещение набора, счётчики
bs_to:		.ds	2
bs_set:		.ds	1
bs_p:		.ds	1
bs_rr:		.ds	1
bs_t:		.ds	1
bs_sp:		.ds	1
bs_o:		.ds	2
tb_i:		.ds	1		; таблицы вида
tb_cl:		.ds	2
tb_sl:		.ds	2
tb_cc:		.ds	2
tb_sc:		.ds	2
bd_n:		.ds	1		; полосы
bd_y0:		.ds	1
bd_y1:		.ds	1
bd_ym:		.ds	1
bd_t:		.ds	1
bd_k:		.ds	1
bd_p:		.ds	3
co_px:		.ds	2		; клетка сетки
co_py:		.ds	2
co_pz:		.ds	2
co_q0:		.ds	2
co_q1:		.ds	2
co_q2:		.ds	2
co_ax:		.ds	2
co_ay:		.ds	2
co_lo:		.ds	1
co_hi:		.ds	1
co_m:		.ds	1
co_rr:		.ds	4
co_s:		.ds	4
co_t:		.ds	4
co_i:		.ds	1
d_se:		.ds	1		; globe_draw: эпоха солнца
co_x:		.ds	1
co_y:		.ds	1
co_f:		.ds	1
co_g:		.ds	2


	.area	_BANK24

zoom_r:	.dw	90, 120, 180, 280, 450, 720	; Globe::setupRadii
zoom_kr:
	.dw	23301, 17476, 11650, 7489, 4660, 2912	; 2^21 / R (деление 32 бит — таблицей)

s_nopg:	.asciz	"globe: no pages\n"
s_bad:	.asciz	"globe: bad GLOBE\n"
s_big:	.asciz	"globe: cell too big\n"
s_full:	.asciz	"globe: edge pool full\n"
s_zoom:	.asciz	"globe: zoom "
s_c0e:	.asciz	", cells 0, edges "
s_cells: .asciz	", cells "
s_edges: .asciz	", edges "
s_fr:	.asciz	", frames "
s_view:	.asciz	", view "
s_sp:	.asciz	" "
s_sun:	.asciz	", sun "
s_pre:	.asciz	", pre\n"
s_shp:	.asciz	", shp "
s_nl:	.asciz	"\n"
s_sonly: .asciz	"globe: sun only, zoom "
s_det:	.asciz	"globe: detail, zoom "
s_fresh: .asciz	", fresh "

;; ================================================================ мелочи

;; Ждать конца DMA; C = #AF (портит A, B)
dma_wait:
	ld	bc, #0x27AF
1$:	in	a, (c)
	jp	m, 1$
	ret

;; DMA регистрами после конца прошлого: dg_sp:dg_so -> dg_dp:dg_do, dg_len слов − 1, dg_num пачек
;; − 1, dg_ctrl. dma_blk — без DMALEN / DMANUM (их пишут один раз на серию пачек)
dma_go:
	call	dma_wait
	ld	a, (dg_len)
	ld	b, #0x26
	out	(c), a
	ld	a, (dg_num)
	ld	b, #0x28
	out	(c), a
	jr	dg_addr
dma_blk:
	call	dma_wait
dg_addr:
	ld	hl, (dg_so)
	ld	b, #0x1A
	out	(c), l
	inc	b
	out	(c), h
	inc	b
	ld	a, (dg_sp)
	out	(c), a
	inc	b
	ld	hl, (dg_do)
	out	(c), l
	inc	b
	out	(c), h
	inc	b
	ld	a, (dg_dp)
	out	(c), a
	ld	a, (dg_ctrl)
	ld	b, #0x27
	out	(c), a
	ret

;; Копия fd_n байт (чётно) со страницы fd_sp, смещение fd_so (может уходить за 16 КБ) в страницу
;; fd_dp со смещения fd_do пачками по 512 байт
far_dma:
	ld	a, (fd_so + 1)
	rlca
	rlca
	and	a, #3
	ld	hl, #fd_sp
	add	a, (hl)
	ld	(hl), a
	ld	a, (fd_so + 1)
	and	a, #0x3F
	ld	(fd_so + 1), a
fd_loop:
	ld	hl, (fd_n)
	ld	a, h
	or	a, l
	ret	z
	ld	de, #512
	or	a, a
	sbc	hl, de
	jr	nc, 1$
	add	hl, de
	ex	de, hl
1$:	ld	(fd_b), de
	ld	a, (fd_sp)
	ld	(dg_sp), a
	ld	hl, (fd_so)
	ld	(dg_so), hl
	ld	a, (fd_dp)
	ld	(dg_dp), a
	ld	hl, (fd_do)
	ld	(dg_do), hl
	srl	d
	rr	e
	dec	e
	ld	a, e
	ld	(dg_len), a
	xor	a, a
	ld	(dg_num), a
	ld	a, #DMA_RAM_RAM
	ld	(dg_ctrl), a
	call	dma_go
	ld	de, (fd_b)
	ld	hl, (fd_so)
	add	hl, de
	ld	(fd_so), hl
	ld	hl, (fd_do)
	add	hl, de
	ld	(fd_do), hl
	ld	hl, (fd_n)
	or	a, a
	sbc	hl, de
	ld	(fd_n), hl
	ld	a, (fd_so + 1)
	cp	a, #0x40
	jr	c, fd_loop
	sub	a, #0x40
	ld	(fd_so + 1), a
	ld	hl, #fd_sp
	inc	(hl)
	jr	fd_loop

;; HL — угол -> DE = globe_sin(HL) (Q14, банк 2)
sin16:
	push	hl
	ld	e, #b_globe_sin
	ld	hl, #_globe_sin
	call	___sdcc_bcall_ehl
	pop	hl
	ret

;; HL:DE (int32) -> HL = SHR14 (старшее слово HL:DE << 2)
shr14:
	sla	d
	rl	l
	rl	h
	sla	d
	rl	l
	rl	h
	ret

;; HL = −HL
neg16:
	ex	de, hl
	ld	hl, #0
	or	a, a
	sbc	hl, de
	ret

;; Печать числа: dec16 — HL, dec8 — A (dbg_dec берёт uint32 в HL:DE)
dec8:
	ld	l, a
	ld	h, #0
dec16:
	ex	de, hl
	ld	hl, #0
	jp	_dbg_dec

;; far_t (HL) + DE -> (HL) (32 бита на месте)
far_add:
	ld	a, (hl)
	add	a, e
	ld	(hl), a
	inc	hl
	ld	a, (hl)
	adc	a, d
	ld	(hl), a
	inc	hl
	ld	a, (hl)
	adc	a, #0
	ld	(hl), a
	inc	hl
	ld	a, (hl)
	adc	a, #0
	ld	(hl), a
	ret

;; far_t из (HL) -> HL (старшее слово), DE (младшее)
far_load:
	ld	e, (hl)
	inc	hl
	ld	d, (hl)
	inc	hl
	ld	a, (hl)
	inc	hl
	ld	h, (hl)
	ld	l, a
	ret

;; FAR(A, DE) -> HL (старшее), DE (младшее); DE < #4000
far_mk:
	ld	l, a
	and	a, #3
	rrca
	rrca
	or	a, d
	ld	d, a
	ld	a, l
	srl	a
	srl	a
	ld	l, a
	ld	h, #0
	ret

;; ================================================================ подготовка

;; Узоры набора bs_set (0 — зумы 4–5, 1 — 2–3, 2 — 0–1) из TEXTURE.DAT (bs_tp:bs_to): в каждом блоке
;; — 32 байта строки узора (океан — заливка словом), затем три удвоения сразу по всем 56 блокам
;; страницы (2D DMA, пачка на блок, шаг 256): [0, 32) -> [32, 64), [0, 64) -> [64, 128), [0, 128) ->
;; [128, 256)
build_strips:
	ld	a, (bs_set)
	ld	hl, (bs_to)
	or	a, a
	jr	z, 2$
	ld	b, a
	ld	de, #NTEX * 1024
1$:	add	hl, de
	djnz	1$
2$:	ld	(bs_to), hl
	call	dma_wait
	ld	a, (ocean)
	ld	(_dma_fill_word), a
	ld	(_dma_fill_word + 1), a
	xor	a, a
	ld	(bs_p), a
bs_page:
	ld	a, (bs_p)
	ld	hl, #strip
	add	a, (hl)
	ld	(bs_sp), a
	xor	a, a
	ld	(bs_rr), a
bs_row:
	ld	a, (bs_p)		; o = to + ((p << 2) | rr) · 32
	add	a, a
	add	a, a
	ld	hl, #bs_rr
	or	a, (hl)
	ld	l, a
	ld	h, #0
	add	hl, hl
	add	hl, hl
	add	hl, hl
	add	hl, hl
	add	hl, hl
	ld	de, (bs_to)
	add	hl, de
	ld	(bs_o), hl
	call	dma_wait		; длина и число пачек — только после конца передачи
	ld	a, #15
	ld	b, #0x26
	out	(c), a
	xor	a, a
	ld	b, #0x28
	out	(c), a
	ld	(bs_t), a
	ld	a, #DMA_RAM_RAM
	ld	(dg_ctrl), a
	ld	a, (bs_sp)
	ld	(dg_dp), a
bs_tex:
	ld	hl, (bs_o)		; источник: tp + (o >> 14), o & #3FFF
	ld	a, h
	rlca
	rlca
	and	a, #3
	ld	b, a
	ld	a, (bs_tp)
	add	a, b
	ld	(dg_sp), a
	ld	a, h
	and	a, #0x3F
	ld	h, a
	ld	(dg_so), hl
	call	bs_blk			; A = rr · 14
	ld	hl, #bs_t
	add	a, (hl)
	ld	h, a
	ld	l, #0
	ld	(dg_do), hl
	call	dma_blk
	ld	hl, (bs_o)
	ld	de, #1024
	add	hl, de
	ld	(bs_o), hl
	ld	a, (bs_t)
	inc	a
	ld	(bs_t), a
	cp	a, #NTEX
	jr	c, bs_tex
	ld	a, #DATA_PAGE		; океан — заливка словом dma_fill_word
	ld	(dg_sp), a
	ld	hl, #_dma_fill_word
	ld	a, h
	and	a, #0x3F
	ld	h, a
	ld	(dg_so), hl
	call	bs_blk
	add	a, #NTEX
	ld	h, a
	ld	l, #0
	ld	(dg_do), hl
	ld	a, #DMA_FILL
	ld	(dg_ctrl), a
	call	dma_blk
	ld	a, (bs_rr)
	inc	a
	ld	(bs_rr), a
	cp	a, #4
	jp	c, bs_row
	ld	a, (bs_sp)		; три удвоения по 56 блокам страницы
	ld	(dg_sp), a
	ld	(dg_dp), a
	ld	hl, #0
	ld	(dg_so), hl
	ld	a, #4 * NBLK - 1
	ld	(dg_num), a
	ld	a, #DMA_RAM_RAM | DMA_S_ALGN | DMA_D_ALGN
	ld	(dg_ctrl), a
	ld	hl, #0x0020
	ld	(dg_do), hl
	ld	a, #15
	ld	(dg_len), a
	call	dma_go
	ld	hl, #0x0040
	ld	(dg_do), hl
	ld	a, #31
	ld	(dg_len), a
	call	dma_go
	ld	hl, #0x0080
	ld	(dg_do), hl
	ld	a, #63
	ld	(dg_len), a
	call	dma_go
	ld	a, (bs_p)
	inc	a
	ld	(bs_p), a
	cp	a, #8
	jp	c, bs_page
	ld	a, (bs_set)		; конца ждать не надо: следующий пользователь DMA ждёт сам
	ld	(strip_set), a
	ret

;; A = bs_rr · 14
bs_blk:
	ld	a, (bs_rr)
	add	a, a
	ld	b, a
	add	a, a
	add	a, a
	add	a, a
	sub	a, b
	ret

;; Записи строк страницы рёбер (Win3): адрес строки заднего буфера, блок и страница узоров — от
;; зума не зависят, заполняются один раз; пары диска pl, pr — globe_rows_pl
rows_meta:
	ld	hl, #0xC000 + EP_ROW
	ld	d, #BACK_DAH
	ld	e, #BACK_DAX
	ld	c, #0
1$:	inc	hl
	inc	hl
	ld	(hl), d
	inc	hl
	ld	(hl), e
	inc	hl
	ld	a, c			; (y & 3) · 14
	and	a, #3
	add	a, a
	ld	b, a
	add	a, a
	add	a, a
	add	a, a
	sub	a, b
	ld	(hl), a
	inc	hl
	ld	a, c			; strip + (y & 31) >> 2
	and	a, #31
	srl	a
	srl	a
	ld	b, a
	ld	a, (strip)
	add	a, b
	ld	(hl), a
	inc	hl
	inc	d
	inc	d
	ld	a, d
	cp	a, #64
	jr	nz, 2$
	ld	d, #0
	inc	e
2$:	inc	c
	ld	a, c
	cp	a, #GLOBE_H
	jr	c, 1$
	ld	a, #1
	ld	(row_meta), a
	ret

;; Фон окна вне диска (зумы 0–1): левые 256 столбцов GEOBORD в задний буфер
bg_restore:
	ld	hl, #RES_GEOBORD_SCR
	ld	de, #rb
	call	_res_find
	or	a, a
	ret	z
	ld	a, (rb + 1)		; страница и смещение: phys >> 14, phys & #3FFF
	rlca
	rlca
	and	a, #3
	ld	b, a
	ld	a, (rb + 2)
	add	a, a
	add	a, a
	or	a, b
	ld	(dg_sp), a
	ld	hl, (rb)
	ld	a, h
	and	a, #0x3F
	ld	h, a
	ld	(dg_so), hl
	ld	a, #127
	ld	(dg_len), a
	xor	a, a
	ld	(dg_num), a
	ld	(dg_do), a
	ld	a, #DMA_RAM_RAM
	ld	(dg_ctrl), a
	ld	a, #BACK_DAH
	ld	(dg_do + 1), a
	ld	a, #BACK_DAX
	ld	(dg_dp), a
	ld	c, #GLOBE_H
1$:	push	bc
	call	dma_go
	pop	bc
	ld	hl, (dg_so)		; so += ширина; перенос в страницу
	ld	de, (rb + 8)
	add	hl, de
	ld	(dg_so), hl
	ld	a, h
	cp	a, #0x40
	jr	c, 2$
	sub	a, #0x40
	ld	(dg_so + 1), a
	ld	hl, #dg_sp
	inc	(hl)
2$:	ld	hl, #dg_do + 1
	inc	(hl)
	inc	(hl)
	ld	a, (hl)
	cp	a, #64
	jr	nz, 3$
	ld	(hl), #0
	ld	hl, #dg_dp
	inc	(hl)
3$:	dec	c
	jr	nz, 1$
	jp	dma_wait

;; Таблицы произведений на 8 констант вида (r_lon, r_lat, R); Win3 = рабочая. Строятся только
;; таблицы, у которых K изменилось (при повороте по долготе те же K4 и sinC·Z)
tables:
	ld	hl, (r_lon)
	call	sin16
	ld	(tb_sl), de
	ld	de, #0x4000
	add	hl, de
	call	sin16
	ld	(tb_cl), de
	ld	hl, (r_lat)
	call	sin16
	ld	(tb_sc), de
	ld	de, #0x4000
	add	hl, de
	call	sin16
	ld	(tb_cc), de
	ld	hl, (tb_sl)		; k0 = R · −sinλ0
	call	neg16
	ld	de, (g_r)
	ld	ix, #kn
	call	kmul
	ld	hl, (tb_cl)		; k1 = R · cosλ0
	ld	de, (g_r)
	call	kmul
	ld	hl, (tb_sc)		; k2 = R · −SHR14(sinC · cosλ0)
	ld	de, (tb_cl)
	call	___mulsint2slong
	call	shr14
	call	neg16
	ld	de, (g_r)
	call	kmul
	ld	hl, (tb_sc)		; k3 = R · −SHR14(sinC · sinλ0)
	ld	de, (tb_sl)
	call	___mulsint2slong
	call	shr14
	call	neg16
	ld	de, (g_r)
	call	kmul
	ld	hl, (tb_cc)		; k4 = R · cosC
	ld	de, (g_r)
	call	kmul
	ld	hl, (tb_cc)		; k5 = SHR14(cosC · cosλ0) << 10
	ld	de, (tb_cl)
	call	___mulsint2slong
	call	shr14
	call	kshl10
	ld	hl, (tb_cc)		; k6 = SHR14(cosC · sinλ0) << 10
	ld	de, (tb_sl)
	call	___mulsint2slong
	call	shr14
	call	kshl10
	ld	hl, (tb_sc)		; k7 = sinC << 10
	call	kshl10
	xor	a, a
	ld	(tb_i), a
	ld	ix, #kn
tb_loop:
	ld	a, (kc_ok)		; K то же — таблица годна
	or	a, a
	jr	z, 2$
	ld	a, (tb_i)
	add	a, a
	add	a, a
	ld	e, a
	ld	d, #0
	ld	hl, #kc
	add	hl, de
	push	ix
	pop	de
	ld	b, #4
1$:	ld	a, (de)
	cp	a, (hl)
	jr	nz, 2$
	inc	hl
	inc	de
	djnz	1$
	jr	3$
2$:	push	ix			; gl_k = k[i], gl_kpg = #C0 + i·4
	pop	hl
	ld	de, #_gl_k
	ld	bc, #4
	ldir
	ld	a, (tb_i)
	add	a, a
	add	a, a
	add	a, #0xC0
	ld	(_gl_kpg), a
	push	ix
	call	_gl_ktab
	pop	ix
	ld	a, (tb_i)		; kc[i] = k[i]
	add	a, a
	add	a, a
	ld	e, a
	ld	d, #0
	ld	hl, #kc
	add	hl, de
	ex	de, hl
	push	ix
	pop	hl
	ld	bc, #4
	ldir
3$:	ld	bc, #4
	add	ix, bc
	ld	a, (tb_i)
	inc	a
	ld	(tb_i), a
	cp	a, #8
	jr	c, tb_loop
	ld	a, #1
	ld	(kc_ok), a
	ret

;; K таблицы кадра: HL · DE (16 x 16) -> (IX), IX += 4
kmul:
	call	___mulsint2slong
kput:
	ld	0 (ix), e
	ld	1 (ix), d
	ld	2 (ix), l
	ld	3 (ix), h
	ld	bc, #4
	add	ix, bc
	ret

;; (int32)HL << 10 -> (IX), IX += 4
kshl10:
	ld	a, h			; знаковое расширение
	rla
	sbc	a, a
	ld	e, l
	ld	d, h
	ld	l, a
	ld	h, a			; HL:DE = (int32)HL
	ld	b, #10
1$:	sla	e
	rl	d
	rl	l
	rl	h
	djnz	1$
	jr	kput

kn:	.ds	32			; K кадра

;; ================================================================ сетка 5°

;; Пиксель окна (A — x, C — y) -> HL = номер клетки сетки 5° (gy · 72 + gx; #FFFF — вне диска):
;; z — таблицей sqrt(1 − r²), мировой вектор — умножениями на оси вида ev, широта и долгота —
;; двоичным поиском по границам клеток (sin 5k°, tan 5k° перекрёстным умножением)
cell_of:
	ld	(co_x), a
	ld	a, c
	ld	(co_y), a
	ld	a, (co_x)		; px = (int16)((2x + 1 − 256) · kr >> 8)
	ld	l, a
	ld	h, #0
	add	hl, hl
	inc	hl
	dec	h
	ld	de, (kr)
	call	___mulsint2slong
	ld	h, l
	ld	l, d
	ld	(co_px), hl
	ld	a, (co_y)		; py = (int16)((2y + 1 − 200) · kr >> 8)
	ld	l, a
	ld	h, #0
	add	hl, hl
	inc	hl
	ld	de, #-200
	add	hl, de
	ld	de, (kr)
	call	___mulsint2slong
	ld	h, l
	ld	l, d
	ld	(co_py), hl
	ld	hl, (co_px)		; rr = px² + py²
	ld	de, (co_px)
	call	___mulsint2slong
	ld	(co_rr), de
	ld	(co_rr + 2), hl
	ld	hl, (co_py)
	ld	de, (co_py)
	call	___mulsint2slong
	push	hl
	ld	hl, (co_rr)
	add	hl, de
	ld	(co_rr), hl
	pop	de
	ld	hl, (co_rr + 2)
	adc	hl, de
	ld	(co_rr + 2), hl
	ld	a, h			; rr >= #10000000 — вне диска
	cp	a, #0x10
	jr	c, 1$
	ld	hl, #0xFFFF
	ret
1$:	ld	a, h			; i = rr >> 20, f = (u8)(rr >> 12)
	add	a, a
	add	a, a
	add	a, a
	add	a, a
	ld	c, a
	ld	a, l
	srl	a
	srl	a
	srl	a
	srl	a
	or	a, c
	ld	(co_i), a
	ld	a, l
	and	a, #0x0F
	add	a, a
	add	a, a
	add	a, a
	add	a, a
	ld	c, a
	ld	a, (co_rr + 1)
	srl	a
	srl	a
	srl	a
	srl	a
	or	a, c
	ld	(co_f), a
	ld	a, (co_i)		; pz = sqz[i] − (int16)((sqz[i] − sqz[i + 1]) · f >> 8)
	ld	l, a
	ld	h, #0
	add	hl, hl
	ld	de, #_sqz_q14
	add	hl, de
	ld	e, (hl)
	inc	hl
	ld	d, (hl)
	inc	hl
	ld	c, (hl)
	inc	hl
	ld	b, (hl)
	ld	(co_pz), de
	ex	de, hl
	or	a, a
	sbc	hl, bc
	ld	a, (co_f)
	ld	e, a
	ld	d, #0
	call	___mulsint2slong
	ld	h, l
	ld	l, d
	ex	de, hl
	ld	hl, (co_pz)
	or	a, a
	sbc	hl, de
	ld	(co_pz), hl
	ld	ix, #ev			; q0, q1, q2 — мировой вектор (Q14)
	call	dot3
	ld	(co_q0), hl
	ld	ix, #ev + 2
	call	dot3
	ld	(co_q1), hl
	ld	ix, #ev + 4
	call	dot3
	ld	(co_q2), hl
	xor	a, a			; широта: число границ sin(5k − 90°) <= q2
	ld	(co_lo), a
	ld	a, #36
	ld	(co_hi), a
2$:	ld	a, (co_lo)
	ld	b, a
	ld	a, (co_hi)
	sub	a, b
	cp	a, #2
	jr	c, 4$
	ld	a, (co_hi)
	add	a, b
	srl	a
	ld	(co_m), a
	ld	l, a
	ld	h, #0
	add	hl, hl
	ld	de, #_sin5_q14
	add	hl, de
	ld	e, (hl)
	inc	hl
	ld	d, (hl)
	ld	hl, (co_q2)
	call	cmp16s			; q2 < sin5[m] — hi = m
	ld	a, (co_m)
	jr	c, 3$
	ld	(co_lo), a
	jr	2$
3$:	ld	(co_hi), a
	jr	2$
4$:	ld	a, (co_lo)		; g = lo · 72
	ld	l, a
	ld	h, #0
	ld	de, #72
	call	___muluint2ulong
	ld	(co_g), de
	ld	hl, (co_q0)		; |q0|, |q1|
	bit	7, h
	call	nz, neg16
	ld	(co_ax), hl
	ld	hl, (co_q1)
	bit	7, h
	call	nz, neg16
	ld	(co_ay), hl
	xor	a, a			; угол в четверти: число k с 5k° <= atan(ay / ax)
	ld	(co_lo), a
	ld	a, #18
	ld	(co_hi), a
5$:	ld	a, (co_lo)
	ld	b, a
	ld	a, (co_hi)
	sub	a, b
	cp	a, #2
	jr	c, 7$
	ld	a, (co_hi)
	add	a, b
	srl	a
	ld	(co_m), a
	add	a, a
	ld	e, a
	ld	d, #0
	ld	hl, #_cos5k_q14
	add	hl, de
	ld	a, (hl)
	inc	hl
	ld	h, (hl)
	ld	l, a
	ex	de, hl
	ld	hl, (co_ay)
	call	___mulsint2slong	; ay · cos5k[m]
	ld	(co_t), de
	ld	(co_t + 2), hl
	ld	a, (co_m)
	add	a, a
	ld	e, a
	ld	d, #0
	ld	hl, #_sin5k_q14
	add	hl, de
	ld	a, (hl)
	inc	hl
	ld	h, (hl)
	ld	l, a
	ex	de, hl
	ld	hl, (co_ax)
	call	___mulsint2slong	; ax · sin5k[m]
	push	hl			; t − s >= 0 — lo = m (значения < 2^29 — без переполнения)
	ld	hl, (co_t)
	or	a, a
	sbc	hl, de
	pop	de
	ld	hl, (co_t + 2)
	sbc	hl, de
	ld	a, (co_m)
	jp	m, 6$
	ld	(co_lo), a
	jr	5$
6$:	ld	(co_hi), a
	jr	5$
7$:	ld	a, (co_lo)		; четверть по знакам q0, q1
	ld	b, a
	ld	a, (co_q0 + 1)
	bit	7, a
	jr	z, 9$
	ld	a, (co_q1 + 1)
	bit	7, a
	jr	nz, 8$
	ld	a, #35
	sub	a, b
	jr	10$
8$:	ld	a, #36
	add	a, b
	jr	10$
9$:	ld	a, (co_q1 + 1)
	bit	7, a
	ld	a, b
	jr	z, 10$
	ld	a, #71
	sub	a, b
10$:	ld	e, a
	ld	d, #0
	ld	hl, (co_g)
	add	hl, de
	ret

;; IX — &ev[a]: HL = SHR14(px · ev[a] + py · ev[a + 3] + pz · ev[a + 6])
dot3:
	ld	hl, (co_px)
	ld	e, 0 (ix)
	ld	d, 1 (ix)
	call	___mulsint2slong
	ld	(co_s), de
	ld	(co_s + 2), hl
	ld	hl, (co_py)
	ld	e, 6 (ix)
	ld	d, 7 (ix)
	call	___mulsint2slong
	call	co_add
	ld	hl, (co_pz)
	ld	e, 12 (ix)
	ld	d, 13 (ix)
	call	___mulsint2slong
	call	co_add
	ld	de, (co_s)
	ld	hl, (co_s + 2)
	jp	shr14

;; co_s += HL:DE
co_add:
	push	hl
	ld	hl, (co_s)
	add	hl, de
	ld	(co_s), hl
	pop	de
	ld	hl, (co_s + 2)
	adc	hl, de
	ld	(co_s + 2), hl
	ret

;; CY = 1, если HL < DE (со знаком); портит A, HL
cmp16s:
	ld	a, h
	xor	a, d
	jp	m, 1$
	or	a, a
	sbc	hl, de
	ret
1$:	ld	a, h
	rla
	ret

;; Полосы строк без рёбер (_gl_bands: #FD в EP_BT, список _gl_bl) -> текстура точки посередине
;; (до 3 точек, пока клетка не однородна) или #FE. Win3 = страница рёбер.
bands:
	call	_gl_bands
	ld	a, (_gl_nb)
	or	a, a
	ret	z
	ld	hl, (r_lon)
	call	sin16
	ld	(tb_sl), de
	ld	de, #0x4000
	add	hl, de
	call	sin16
	ld	(tb_cl), de
	ld	hl, (r_lat)
	call	sin16
	ld	(tb_sc), de
	ld	de, #0x4000
	add	hl, de
	call	sin16
	ld	(tb_cc), de
	ld	hl, (tb_sl)		; ev = (−sl, cl, 0; −sC·cl, −sC·sl, cC; cC·cl, cC·sl, sC)
	call	neg16
	ld	(ev), hl
	ld	hl, (tb_cl)
	ld	(ev + 2), hl
	ld	hl, #0
	ld	(ev + 4), hl
	ld	hl, (tb_sc)
	ld	de, (tb_cl)
	call	___mulsint2slong
	call	shr14
	call	neg16
	ld	(ev + 6), hl
	ld	hl, (tb_sc)
	ld	de, (tb_sl)
	call	___mulsint2slong
	call	shr14
	call	neg16
	ld	(ev + 8), hl
	ld	hl, (tb_cc)
	ld	(ev + 10), hl
	ld	hl, (tb_cc)
	ld	de, (tb_cl)
	call	___mulsint2slong
	call	shr14
	ld	(ev + 12), hl
	ld	hl, (tb_cc)
	ld	de, (tb_sl)
	call	___mulsint2slong
	call	shr14
	ld	(ev + 14), hl
	ld	hl, (tb_sc)
	ld	(ev + 16), hl
	ld	a, (r_z)		; kr = 2^21 / R — таблицей
	add	a, a
	ld	e, a
	ld	d, #0
	ld	hl, #zoom_kr
	add	hl, de
	ld	a, (hl)
	inc	hl
	ld	h, (hl)
	ld	l, a
	ld	(kr), hl
	xor	a, a
	ld	(bd_n), a
bd_loop:
	ld	a, (bd_n)		; полоса [y0, y1], средняя строка ym
	add	a, a
	ld	e, a
	ld	d, #0
	ld	hl, #_gl_bl
	add	hl, de
	ld	a, (hl)
	ld	(bd_y0), a
	ld	b, a
	inc	hl
	ld	a, (hl)
	ld	(bd_y1), a
	add	a, b
	rra
	ld	(bd_ym), a
	ld	l, a			; запись строки ym: pl (a), pr (b)
	ld	h, #0
	ld	e, l
	ld	d, h
	add	hl, hl
	add	hl, de
	add	hl, hl
	ld	de, #0xC000 + EP_ROW
	add	hl, de
	ld	c, (hl)
	inc	hl
	ld	b, (hl)
	ld	a, b			; p0 = (a + b) >> 1
	add	a, c
	rra
	ld	(bd_p), a
	ld	a, b			; d = (b − a) >> 2
	sub	a, c
	srl	a
	srl	a
	ld	d, a
	ld	a, c			; p1 = a + d
	add	a, d
	ld	(bd_p + 1), a
	ld	a, b			; p2 = b − d
	sub	a, d
	ld	(bd_p + 2), a
	ld	a, #0xFE
	ld	(bd_t), a
	xor	a, a
	ld	(bd_k), a
bd_pt:
	ld	a, (bd_t)
	cp	a, #0xFE
	jr	nz, bd_fill
	ld	a, (bd_k)
	cp	a, #3
	jr	nc, bd_fill
	ld	e, a
	ld	d, #0
	ld	hl, #bd_p
	add	hl, de
	ld	a, (bd_ym)
	ld	c, a
	ld	a, (hl)
	add	a, a
	call	cell_of
	ld	a, h
	and	a, l
	inc	a
	jr	z, bd_next		; вне диска
	ex	de, hl			; t = far_byte(gt + c)
	ld	hl, (r_gt)
	add	hl, de
	ex	de, hl
	ld	hl, (r_gt + 2)
	jr	nc, 1$
	inc	hl
1$:	call	_far_byte
	ld	(bd_t), a
bd_next:
	ld	hl, #bd_k
	inc	(hl)
	jr	bd_pt
bd_fill:
	ld	a, (bd_y0)		; EP_BT[y0 .. y1] = t
	ld	e, a
	ld	d, #0xC0 + (EP_BT >> 8)
	ld	a, (bd_y1)
	sub	a, e
	inc	a
	ld	b, a
	ld	a, (bd_t)
2$:	ld	(de), a
	inc	de
	djnz	2$
	ld	a, (bd_n)
	inc	a
	ld	(bd_n), a
	ld	hl, #_gl_nb
	cp	a, (hl)
	jp	c, bd_loop
	ret

;; ================================================================ кадр

;; Кадр вида r_lon, r_lat, r_z, солнце r_sun
render:
	ld	hl, #RES_GLOBE
	ld	de, #rg
	call	_res_find
	or	a, a
	ret	z
	ld	hl, #RES_TEXTURE_DAT
	ld	de, #rt
	call	_res_find
	or	a, a
	ret	z
	ld	hl, #6			; far_read(rg.phys, h, 6): nCell, nVert, nEdge
	push	hl
	ld	hl, #r_h
	push	hl
	ld	hl, #rg
	call	far_load
	call	_far_read
	ld	hl, #rg			; ct = phys + 6, bt = ct + h0 · 16, gt = bt + (h1 + h2) · 6
	ld	de, #r_ct
	ld	bc, #4
	ldir
	ld	hl, #r_ct
	ld	de, #6
	call	far_add
	ld	hl, #r_ct
	ld	de, #r_bt
	ld	bc, #4
	ldir
	ld	hl, (r_h)
	add	hl, hl
	add	hl, hl
	add	hl, hl
	add	hl, hl
	ex	de, hl
	ld	hl, #r_bt
	call	far_add
	ld	hl, #r_bt
	ld	de, #r_gt
	ld	bc, #4
	ldir
	ld	hl, (r_h + 2)
	ld	de, (r_h + 4)
	add	hl, de
	ld	e, l
	ld	d, h
	add	hl, hl
	add	hl, de
	add	hl, hl
	ex	de, hl
	ld	hl, #r_gt
	call	far_add
	ld	hl, (r_h)		; nCell > 72 — ресурс не тот
	ld	de, #NCELL + 1
	or	a, a
	sbc	hl, de
	jr	c, 1$
	ld	hl, #s_bad
	jp	_dbg_puts
1$:	ld	hl, (_frames)
	ld	(r_t0), hl
	ld	a, (r_z)		; набор узоров 2 − (z >> 1)
	srl	a
	ld	b, a
	ld	a, #2
	sub	a, b
	ld	(r_set), a
	ld	a, (bg_zoom)
	ld	(r_bz), a
	ld	a, (row_zoom)
	ld	(r_rz), a
	ld	a, (r_z)		; R = zoom_r[z]
	add	a, a
	ld	e, a
	ld	d, #0
	ld	hl, #zoom_r
	add	hl, de
	ld	a, (hl)
	inc	hl
	ld	h, (hl)
	ld	l, a
	ld	(g_r), hl
	call	_res_game		; globe.rul oceanPalette: TFTD 1, UFO 12
	cp	a, #2
	ld	a, #16
	jr	z, 2$
	ld	a, #192
2$:	ld	(ocean), a
	ld	a, (strip_set)
	ld	b, a
	ld	a, (r_set)
	cp	a, b
	jr	z, 3$
	ld	(bs_set), a
	ld	a, (rt + 1)		; TEXTURE.DAT: страница и смещение
	rlca
	rlca
	and	a, #3
	ld	b, a
	ld	a, (rt + 2)
	add	a, a
	add	a, a
	or	a, b
	ld	(bs_tp), a
	ld	hl, (rt)
	ld	a, h
	and	a, #0x3F
	ld	h, a
	ld	(bs_to), hl
	call	build_strips
3$:	; ---- предрасчитанный вид (вид сетки): геометрии нет — тень и проход кусков
	ld	a, (_gv_off)
	or	a, a
	jp	nz, r_sunonly
	ld	hl, (r_lon)
	ld	(r_glon), hl
	ld	hl, (r_lat)
	ld	(r_glat), hl
	ld	hl, #r_giv
	push	hl
	ld	hl, #r_glat
	push	hl
	ld	hl, #r_glon
	push	hl
	ld	a, (r_z)
	push	af
	inc	sp
	ld	e, #b_gview_pick
	ld	hl, #_gview_pick
	call	___sdcc_bcall_ehl
	ld	hl, #7
	add	hl, sp
	ld	sp, hl
	or	a, a
	jp	z, r_sunonly
	ld	hl, (r_glon)
	ld	de, (r_lon)
	or	a, a
	sbc	hl, de
	jp	nz, r_sunonly
	ld	hl, (r_glat)
	ld	de, (r_lat)
	or	a, a
	sbc	hl, de
	jp	nz, r_sunonly
	ld	hl, (r_giv)
	push	hl
	ld	a, (r_z)
	push	af
	inc	sp
	ld	a, (epage)
	push	af
	inc	sp
	ld	e, #b_gview_load
	ld	hl, #_gview_load
	call	___sdcc_bcall_ehl
	pop	hl
	pop	hl
	or	a, a
	jp	z, r_sunonly
	ld	a, (epage)
	call	_pg_map3
	call	r_rows
	ld	a, (work)
	call	_pg_map3
	call	r_bg
	ld	a, (epage)
	call	_pg_map3
	call	r_shadow
	call	_gl_rows_edg			; вид рёбрами (GVE2)
	ld	a, (work)
	call	_pg_map3
	call	dma_wait
	call	r_setv
	ld	a, #1
	ld	(valid), a
	xor	a, a
	ld	(geom), a			; страница рёбер занята видом
	ld	a, (_gl_dbg)
	or	a, a
	ret	z
	ld	hl, #s_zoom			; формат как у рёберного пути (globebench.ps1)
	call	_dbg_puts
	ld	a, (r_z)
	call	dec8
	ld	hl, #s_c0e
	call	_dbg_puts
	ld	hl, (r_giv)
	call	dec16
	call	r_dbgtail
	ld	hl, #s_pre
	jp	_dbg_puts

r_sunonly:	; ---- тот же вид, другое солнце: геометрия годна — тень и проход строк
	ld	a, (geom)
	or	a, a
	jp	z, r_edges
	ld	a, (v_zoom)
	ld	b, a
	ld	a, (r_z)
	cp	a, b
	jp	nz, r_edges
	ld	hl, (v_lon)
	ld	de, (r_lon)
	or	a, a
	sbc	hl, de
	jp	nz, r_edges
	ld	hl, (v_lat)
	ld	de, (r_lat)
	or	a, a
	sbc	hl, de
	jp	nz, r_edges
	ld	a, (epage)
	call	_pg_map3
	call	r_shadow
	ld	a, (_gl_capok)			; отрезки прошлого прохода записаны — без AEL
	or	a, a
	jr	z, 1$
	call	_gl_rows_cap
	jr	2$
1$:	ld	a, #1				; полный проход — с записью: следующая смена солнца дешёвая
	ld	(_gl_capwant), a
	call	_gl_rows
2$:	ld	a, (work)
	call	_pg_map3
	call	dma_wait
	ld	a, (_gl_dbg)
	or	a, a
	ret	z
	ld	hl, #s_sonly
	call	_dbg_puts
	ld	a, (r_z)
	call	dec8
	ld	hl, #s_fr
	call	_dbg_puts
	ld	hl, (_frames)
	ld	de, (r_t0)
	or	a, a
	sbc	hl, de
	call	dec16
	ld	hl, #s_sun
	call	_dbg_puts
	ld	hl, (r_sun)
	call	dec16
	ld	hl, #s_nl
	jp	_dbg_puts

r_edges:	; ---- рёберный путь
	ld	a, (epage)
	call	_pg_map3
	ld	e, #b_gview_reset		; затирает указатели видов и поток
	ld	hl, #_gview_reset
	call	___sdcc_bcall_ehl
	call	r_rows
	ld	hl, #0xC000 + EP_EVB
	ld	bc, #0x200
	ld	a, #0xFE
	call	fill
	ld	hl, #0xC000 + EP_EVKB
	ld	bc, #0x100
	xor	a, a
	call	fill
	ld	hl, #0xC000 + EP_EVKA
	ld	bc, #0x100
	ld	a, #0xFF
	call	fill
	ld	hl, #0xC000 + EP_POOL
	ld	(_gl_eptr), hl
	ld	hl, #0
	ld	(_gl_nedge), hl
	ld	a, (work)
	call	_pg_map3
	ld	hl, #0xC000 + WB_BUCKET
	ld	bc, #GLOBE_H * 2
	xor	a, a
	call	fill
	ld	hl, #0xC000 + WB_COV
	ld	bc, #256
	xor	a, a
	call	fill
	call	r_bg
	call	r_setv
	call	tables
	ld	hl, #-ARR			; записи ячеек, центры, проекции центров — на стеке
	add	hl, sp
	ld	sp, hl
	ld	(r_cr), hl
	ld	de, #NCELL * 16
	add	hl, de
	ld	(r_cv), hl
	ld	de, #NCELL * 6
	add	hl, de
	ld	(r_cp), hl
	ld	hl, (r_h)			; far_read(ct, cr, h0 · 16)
	add	hl, hl
	add	hl, hl
	add	hl, hl
	add	hl, hl
	push	hl
	ld	hl, (r_cr)
	push	hl
	ld	hl, #r_ct
	call	far_load
	call	_far_read
	ld	a, (r_h)			; центры ячеек (6 байт с +8 записи)
	or	a, a
	jr	z, 2$
	ld	b, a
	ld	hl, (r_cr)
	ld	de, #8
	add	hl, de
	ld	de, (r_cv)
1$:	push	bc
	ld	bc, #6
	ldir
	ld	bc, #10
	add	hl, bc
	pop	bc
	djnz	1$
2$:	ld	hl, (r_cv)
	ld	(_gl_pv), hl
	ld	hl, (r_cp)
	ld	(_gl_pd), hl
	ld	a, (r_h)
	ld	(_gl_pn), a
	xor	a, a
	ld	(_gl_noz), a
	call	_gl_project
	ld	a, (r_z)			; край диска — зумы 0–1
	cp	a, #2
	ld	a, #0
	adc	a, #0
	ld	(_gl_limb), a
	ld	a, (work)
	ld	(_gl_wpg), a
	ld	a, (epage)
	ld	(_gl_epg), a
	ld	hl, #0xC000 + RES_OFF
	ld	(_gl_res), hl
	xor	a, a
	ld	(r_ncell), a
	ld	a, (r_bt + 1)			; страница и смещение блоков ячеек
	rlca
	rlca
	and	a, #3
	ld	b, a
	ld	a, (r_bt + 2)
	add	a, a
	add	a, a
	or	a, b
	ld	(r_btpg), a
	ld	hl, (r_bt)
	ld	a, h
	and	a, #0x3F
	ld	h, a
	ld	(r_btof), hl
	call	cm_fill
	ld	hl, (r_cr)
	ld	(_gl_crp), hl
	ld	hl, (r_cp)
	ld	(_gl_cpp), hl
	ld	a, (r_h)
	ld	(_gl_cn), a
	ld	hl, #0xC000 + WB_CM
	ld	(_gl_cmp), hl
	ld	hl, #0xC000 + WB_VIS
	ld	(_gl_vsp), hl
	call	_gl_cull
	ld	a, (_gl_nvis)
	ld	(r_nvc), a
	xor	a, a
	ld	(r_v), a
rc_loop:				; видимые ячейки
	ld	a, (r_nvc)
	ld	b, a
	ld	a, (r_v)
	cp	a, b
	jp	nc, rc_done
	ld	e, a
	ld	d, #0
	ld	hl, #0xC000 + WB_VIS
	add	hl, de
	ld	a, (hl)
	ld	(r_cv8), a
	and	a, #0x7F
	ld	l, a
	ld	h, #0
	add	hl, hl
	add	hl, hl
	add	hl, hl
	add	hl, hl
	ld	de, (r_cr)
	add	hl, de
	push	hl
	pop	ix				; IX — запись ячейки
	ld	l, 4 (ix)			; рёбер в ячейке всего
	ld	h, 5 (ix)
	ld	(r_en), hl
2$:	ld	hl, (r_en)			; пул рёбер: gl_eptr > #10000 − 10 · en
	add	hl, hl
	ld	e, l
	ld	d, h
	add	hl, hl
	add	hl, hl
	add	hl, de
	call	neg16
	ex	de, hl
	ld	hl, (_gl_eptr)
	or	a, a
	sbc	hl, de
	jr	z, 3$
	jr	c, 3$
	ld	hl, #s_full
	call	_dbg_puts
	jp	rc_done
3$:	ld	hl, #r_ncell
	inc	(hl)
	ld	a, (r_btpg)			; шапка блока ячейки -> рабочая страница
	ld	(fd_sp), a
	ld	l, 0 (ix)
	ld	h, 1 (ix)
	ld	de, (r_btof)
	add	hl, de
	ld	(r_boff), hl
	ld	(fd_so), hl
	ld	a, (work)
	ld	(fd_dp), a
	ld	hl, #VSRC
	ld	(fd_do), hl
	ld	l, 2 (ix)
	ld	h, 3 (ix)
	ld	(fd_n), hl
	ld	(r_hdr), hl
	call	far_dma
	call	dma_wait
	xor	a, a
	ld	(r_whole), a
	ld	a, (0xC000 + VSRC)
	ld	(r_ns), a
	ld	a, (r_z)			; зумы 0–2: все подблоки, передняя сторона — как у ячейки
	cp	a, #3
	jr	nc, 5$
	ld	a, (r_cv8)
	and	a, #0x80
	ld	c, a
	ld	a, (r_ns)
	ld	b, a
	ld	hl, #0xC000 + SUBV
	xor	a, a
4$:	ld	e, a
	or	a, c
	ld	(hl), a
	inc	hl
	ld	a, e
	inc	a
	djnz	4$
	ld	a, (r_ns)			; данные всех подблоков одним DMA, если влезают до STG
	ld	b, a
	ld	ix, #0xC000 + VSRC + 2
	ld	hl, #0
10$:	ld	e, 2 (ix)			; (vn + en) · 6
	ld	d, 3 (ix)
	add	hl, de
	ld	e, 4 (ix)
	ld	d, 5 (ix)
	add	hl, de
	ld	de, #16
	add	ix, de
	djnz	10$
	ld	e, l
	ld	d, h
	add	hl, hl
	add	hl, de
	add	hl, hl
	ld	(fd_n), hl
	ld	de, #STG - VDAT + 1
	or	a, a
	sbc	hl, de
	jr	nc, 11$
	ld	a, (r_btpg)			; far_dma нормирует адрес и портит fd_*
	ld	(fd_sp), a
	ld	a, (work)
	ld	(fd_dp), a
	ld	hl, (r_boff)
	ld	de, (r_hdr)
	add	hl, de
	ld	(fd_so), hl
	ld	hl, #VDAT
	ld	(fd_do), hl
	call	far_dma
	call	dma_wait
	ld	a, #1
	ld	(r_whole), a
11$:	ld	a, (r_ns)
	jp	6$
5$:	ld	a, (r_ns)			; зумы 3–5: центры подблоков, проекция, отсев как у ячеек
	ld	b, a
	ld	hl, #0xC000 + VSRC + 2 + 8
	ld	de, #0xC000 + SUBC
7$:	push	bc
	ld	bc, #6
	ldir
	ld	bc, #10
	add	hl, bc
	pop	bc
	djnz	7$
	ld	hl, #0xC000 + SUBC
	ld	(_gl_pv), hl
	ld	hl, #0xC000 + SUBP
	ld	(_gl_pd), hl
	ld	a, (r_ns)
	ld	(_gl_pn), a
	xor	a, a
	ld	(_gl_noz), a
	call	_gl_project
	ld	hl, #0xC000 + VSRC + 2
	ld	(_gl_crp), hl
	ld	hl, #0xC000 + SUBP
	ld	(_gl_cpp), hl
	ld	a, (r_ns)
	ld	(_gl_cn), a
	ld	l, a				; m зума: шапка + 2 + 16·ns + 2·ns·(z − 3)
	ld	h, #0
	add	hl, hl
	ld	e, l
	ld	d, h
	add	hl, hl
	add	hl, hl
	add	hl, hl
	ld	a, (r_z)
	sub	a, #3
	jr	z, 8$
	ld	b, a
9$:	add	hl, de
	djnz	9$
8$:	ld	de, #0xC000 + VSRC + 2
	add	hl, de
	ld	(_gl_cmp), hl
	ld	hl, #0xC000 + SUBV
	ld	(_gl_vsp), hl
	call	_gl_cull
	ld	a, (_gl_nvis)
6$:	ld	(r_nsv), a
	xor	a, a
	ld	(r_sv), a
rs_loop:				; видимые подблоки ячейки
	ld	a, (r_nsv)
	ld	b, a
	ld	a, (r_sv)
	cp	a, b
	jp	nc, rc_next
	ld	e, a
	ld	d, #0
	ld	hl, #0xC000 + SUBV
	add	hl, de
	ld	a, (hl)
	ld	c, a
	rlca					; бит 7 — весь спереди, z вершин не нужен
	and	a, #1
	ld	(_gl_noz), a
	ld	a, c
	and	a, #0x7F
	ld	l, a
	ld	h, #0
	add	hl, hl
	add	hl, hl
	add	hl, hl
	add	hl, hl
	ld	de, #0xC000 + VSRC + 2
	add	hl, de
	push	hl
	pop	ix				; IX — запись подблока
	ld	l, 2 (ix)
	ld	h, 3 (ix)
	ld	(r_vn), hl
	ld	l, 4 (ix)
	ld	h, 5 (ix)
	ld	(r_en), hl
	ld	a, (r_whole)			; данные уже в VDAT: смещение подблока − шапка
	or	a, a
	jr	z, 12$
	ld	l, 0 (ix)
	ld	h, 1 (ix)
	ld	de, (r_hdr)
	or	a, a
	sbc	hl, de
	ld	de, #0xC000 + VDAT
	add	hl, de
	jr	13$
12$:	ld	a, (r_btpg)			; данные подблока (вершины, рёбра) -> рабочая страница
	ld	(fd_sp), a
	ld	l, 0 (ix)
	ld	h, 1 (ix)
	ld	de, (r_boff)
	add	hl, de
	ld	(fd_so), hl
	ld	a, (work)
	ld	(fd_dp), a
	ld	hl, #VDAT
	ld	(fd_do), hl
	ld	hl, (r_vn)
	ld	de, (r_en)
	add	hl, de
	ld	e, l
	ld	d, h
	add	hl, hl
	add	hl, de
	add	hl, hl
	ld	(fd_n), hl
	call	far_dma
	call	dma_wait
	ld	hl, #0xC000 + VDAT
13$:	ld	(r_dp), hl
	ld	(_gl_pv), hl
	ld	hl, #0xC000 + RES_OFF
	ld	(_gl_pd), hl
	ld	a, (r_vn)
	ld	(_gl_pn), a
	call	_gl_project
	ld	hl, (r_vn)			; рёбра — после вершин
	ld	e, l
	ld	d, h
	add	hl, hl
	add	hl, de
	add	hl, hl
	ld	de, (r_dp)
	add	hl, de
	ld	(_gl_eb), hl
	ld	a, (r_en)
	ld	(_gl_ec), a
	ld	hl, (_gl_eptr)
	ld	(r_e0), hl
	ld	hl, #0xC000 + STG
	ld	(_gl_sptr), hl
	call	_gl_edges
	ld	hl, (_gl_eptr)			; записи подблока -> страница рёбер
	ld	de, (r_e0)
	or	a, a
	sbc	hl, de
	jr	z, rs_next
	ld	(fd_n), hl
	ld	a, (work)
	ld	(fd_sp), a
	ld	hl, #STG
	ld	(fd_so), hl
	ld	a, (epage)
	ld	(fd_dp), a
	ld	hl, (r_e0)
	ld	a, h
	sub	a, #0xC0
	ld	h, a
	ld	(fd_do), hl
	call	far_dma
rs_next:
	ld	hl, #r_sv
	inc	(hl)
	jp	rs_loop
rc_next:
	ld	hl, #r_v
	inc	(hl)
	jp	rc_loop
rc_done:				; сетка 5° в центре вида — если в окне нет рёбер
	ld	hl, (r_lon)			; gx = (u32)lon · 72 >> 16
	ld	de, #72
	call	___muluint2ulong
	ld	a, l
	ld	(co_x), a
	ld	hl, (r_lat)			; gy = (u32)(lat + 16384) · 36 >> 15, не больше 35
	ld	de, #16384
	add	hl, de
	ld	de, #36
	call	___muluint2ulong
	sla	d
	rl	l
	rl	h
	ld	a, h
	or	a, a
	ld	a, l
	jr	nz, 4$
	cp	a, #36
	jr	c, 5$
4$:	ld	a, #35
5$:	ld	l, a
	ld	h, #0
	ld	de, #72
	call	___muluint2ulong
	ld	a, (co_x)
	ld	l, a
	ld	h, #0
	add	hl, de
	ex	de, hl
	ld	hl, (r_gt)
	add	hl, de
	ex	de, hl
	ld	hl, (r_gt + 2)
	jr	nc, 6$
	inc	hl
6$:	call	_far_byte
	cp	a, #0xFE
	jr	nz, 7$
	ld	a, #NTEX
7$:	ld	(_gl_gtex), a
	ld	hl, #0				; корзины и покрытие -> страница рёбер
	push	hl
	ld	hl, #GLOBE_H * 2
	push	hl
	ld	a, (work)
	ld	de, #WB_BUCKET
	call	far_mk
	push	hl
	push	de
	ld	a, (epage)
	ld	de, #EP_BUCKET
	call	far_mk
	call	_far_copy
	ld	hl, #0
	push	hl
	ld	hl, #256
	push	hl
	ld	a, (work)
	ld	de, #WB_COV
	call	far_mk
	push	hl
	push	de
	ld	a, (epage)
	ld	de, #EP_BT
	call	far_mk
	call	_far_copy
	ld	a, (epage)
	call	_pg_map3
	call	bands
	call	r_shadow
	ld	a, #1				; запись отрезков — сразу: рёберный путь идёт только на виде
	ld	(_gl_capwant), a		; вне сетки, смена солнца на нём — без AEL
	call	_gl_rows
	ld	a, (work)
	call	_pg_map3
	call	dma_wait
	ld	a, #1
	ld	(valid), a
	ld	(geom), a
	ld	hl, #ARR
	add	hl, sp
	ld	sp, hl
	ld	a, (_gl_dbg)
	or	a, a
	ret	z
	ld	hl, #s_zoom
	call	_dbg_puts
	ld	a, (r_z)
	call	dec8
	ld	hl, #s_cells
	call	_dbg_puts
	ld	a, (r_ncell)
	call	dec8
	ld	hl, #s_edges
	call	_dbg_puts
	ld	hl, (_gl_nedge)
	call	dec16
	call	r_dbgtail
	ld	hl, #s_shp
	call	_dbg_puts
	ld	a, (r_sp)
	call	dec8
	ld	hl, #s_nl
	jp	_dbg_puts

;; Записи строк: постоянные поля — один раз, пары диска — при смене зума (Win3 = страница рёбер)
r_rows:
	ld	a, (row_meta)
	or	a, a
	call	z, rows_meta
	ld	a, (r_rz)
	ld	b, a
	ld	a, (r_z)
	cp	a, b
	ret	z
	ld	hl, #0xC000 + EP_ROW
	push	hl
	push	af
	inc	sp
	ld	e, #b_globe_rows_pl
	ld	hl, #_globe_rows_pl
	call	___sdcc_bcall_ehl
	inc	sp
	pop	hl
	ld	a, (r_z)
	ld	(row_zoom), a
	ret

;; Фон вне диска: зумы 2+ — диск закрывает окно (фон затёрт); иначе восстановить, если диск был
;; больше (при увеличении новый диск накрывает старый, фон вне него уже лежит)
r_bg:
	ld	a, (r_z)
	cp	a, #2
	jr	c, 1$
	ld	a, #0xFF
	ld	(bg_zoom), a
	ret
1$:	ld	b, a
	ld	a, (r_bz)
	cp	a, b
	jr	z, 2$
	jr	c, 2$
	push	bc
	call	bg_restore
	pop	bc
2$:	ld	a, b
	ld	(bg_zoom), a
	ret

;; Тень (банк 25) -> флаги строк в EP_SHF страницы рёбер (нет страницы тени — нули)
r_shadow:
	ld	a, (ocean)
	push	af
	inc	sp
	ld	hl, (r_sun)
	push	hl
	ld	a, (r_z)
	push	af
	inc	sp
	ld	hl, (r_lat)
	push	hl
	ld	hl, (r_lon)
	push	hl
	ld	e, #b_globe_shadow
	ld	hl, #_globe_shadow
	call	___sdcc_bcall_ehl
	ld	hl, #8
	add	hl, sp
	ld	sp, hl
	ld	(r_sp), a
	cp	a, #PG_NONE
	jr	z, 1$
	ld	hl, #0				; far_copy(FAR(epage, EP_SHF), FAR(sp, GLOBE_SH_FLG), 200)
	push	hl
	ld	hl, #GLOBE_H
	push	hl
	ld	a, (r_sp)
	ld	de, #GLOBE_SH_FLG
	call	far_mk
	push	hl
	push	de
	ld	a, (epage)
	ld	de, #EP_SHF
	call	far_mk
	call	_far_copy
	ret
1$:	ld	hl, #GLOBE_H			; far_fill(FAR(epage, EP_SHF), 0, 200)
	push	hl
	xor	a, a
	push	af
	inc	sp
	ld	a, (epage)
	ld	de, #EP_SHF
	call	far_mk
	call	_far_fill
	ret

;; v_lon, v_lat, v_zoom = вид кадра
r_setv:
	ld	hl, (r_lon)
	ld	(v_lon), hl
	ld	hl, (r_lat)
	ld	(v_lat), hl
	ld	a, (r_z)
	ld	(v_zoom), a
	ret

;; ", frames F, view LON LAT, sun S"
r_dbgtail:
	ld	hl, #s_fr
	call	_dbg_puts
	ld	hl, (_frames)
	ld	de, (r_t0)
	or	a, a
	sbc	hl, de
	call	dec16
	ld	hl, #s_view
	call	_dbg_puts
	ld	hl, (r_lon)
	call	dec16
	ld	hl, #s_sp
	call	_dbg_puts
	ld	hl, (r_lat)
	call	dec16
	ld	hl, #s_sun
	call	_dbg_puts
	ld	hl, (r_sun)
	jp	dec16

;; Пределы окна ячеек от зума (Win3 = рабочая): проекция ячейки не длиннее хорды, m = R·sinρ +
;; m/8 + 2; sinρ >= 16384 — ячейка не отсекается (_gl_cull её не смотрит)
cm_fill:
	ld	a, (cm_zoom)
	ld	b, a
	ld	a, (r_z)
	cp	a, b
	ret	z
	ld	(cm_zoom), a
	ld	a, (r_h)
	or	a, a
	ret	z
	ld	b, a
	ld	hl, (r_cr)
	ld	de, #14
	add	hl, de
	ld	de, #0xC000 + WB_CM
1$:	push	bc
	push	hl
	push	de
	ld	e, (hl)
	inc	hl
	ld	d, (hl)				; DE = sinρ
	ld	hl, #0
	bit	7, d
	jr	nz, 2$
	ld	a, d
	cp	a, #0x40
	jr	nc, 3$
2$:	ld	hl, (g_r)				; v = SHR14(R · sinρ)
	call	___mulsint2slong
	call	shr14
3$:	ld	e, l				; m = v + (v >> 3) + 2
	ld	d, h
	sra	d
	rr	e
	sra	d
	rr	e
	sra	d
	rr	e
	add	hl, de
	inc	hl
	inc	hl
	ex	de, hl
	pop	hl
	ld	(hl), e
	inc	hl
	ld	(hl), d
	inc	hl
	ex	de, hl
	pop	hl
	ld	bc, #16
	add	hl, bc
	pop	bc
	djnz	1$
	ret

;; HL — куда, BC — сколько (>= 1), A — байт
fill:
	ld	(hl), a
	dec	bc
	ld	a, b
	or	a, c
	ret	z
	ld	e, l
	ld	d, h
	inc	de
	ldir
	ret

;; Задний буфер -> окно глобуса на экране (2D DMA, строки по 512). Старт — сразу после кадрового
;; прерывания: копия идёт ~1.35 строки на строку луча и остаётся впереди него (иначе виден разрыв)
blit:
	call	dma_wait
	ld	a, (_frames)
	ld	b, a
1$:	ld	a, (_frames)
	cp	a, b
	jr	z, 1$
	ld	bc, #0x1AAF
	xor	a, a
	out	(c), a				; SAL
	inc	b
	ld	a, #BACK_DAH
	out	(c), a				; SAH
	inc	b
	ld	a, #BACK_DAX
	out	(c), a				; SAX
	inc	b
	xor	a, a
	out	(c), a				; DAL
	inc	b
	out	(c), a				; DAH
	inc	b
	ld	a, #SCREEN_PAGE
	out	(c), a				; DAX
	ld	b, #0x26
	ld	a, #127
	out	(c), a				; DMALen
	ld	b, #0x28
	ld	a, #GLOBE_H - 1
	out	(c), a				; DMANum
	ld	b, #0x27
	ld	a, #DMA_RAM_RAM | DMA_S_ALGN | DMA_D_ALGN | DMA_ASZ
	out	(c), a
	jp	dma_wait

;; ================================================================ детали

;; Детали глобуса (зумы 1–5, globe_det.c в банке 26) — после кадра, в задний буфер. Вид сменился —
;; группы GLOBEDET (записи и пределы окна зума) копией DMA в рабочую страницу, центры групп —
;; проекция таблицами вида и отсев _gl_cull (как ячейки карты); вершины видимых групп нужного зума
;; (линии — с 1, подписи стран — с 2, города — с 3) копией DMA подряд в DT_V и проекция на месте.
;; Банк 26 забирает список групп и проекции и рисует. Тот же вид (другое солнце) — рисует по прошлым.
detail:
	ld	a, (r_z)
	or	a, a
	ret	z
	ld	hl, (_frames)
	ld	(dt_t0), hl
	ld	a, (dt_ok)
	or	a, a
	jr	z, 1$
	ld	a, (dt_z)
	ld	b, a
	ld	a, (r_z)
	cp	a, b
	jr	nz, 1$
	ld	hl, (dt_lon)
	ld	de, (r_lon)
	or	a, a
	sbc	hl, de
	jr	nz, 1$
	ld	hl, (dt_lat)
	ld	de, (r_lat)
	or	a, a
	sbc	hl, de
	ld	a, #0
	jp	z, dt_draw
1$:	xor	a, a
	ld	(dt_ok), a
	ld	hl, #RES_GLOBEDET
	ld	de, #dt_res
	call	_res_find
	or	a, a
	ret	z
	ld	hl, #24				; far_read(phys, dt_hdr, 24)
	push	hl
	ld	hl, #dt_hdr
	push	hl
	ld	hl, #dt_res
	call	far_load
	call	_far_read
	ld	hl, (dt_hdr + 14)		; групп: 1..DT_GMAX
	ld	a, h
	or	a, a
	ret	nz
	ld	a, l
	or	a, a
	ret	z
	cp	a, #DT_GMAX + 1
	ret	nc
	ld	(dt_ng), a
	ld	e, a				; смещения: записи 24, пределы зума 24 + 16·ng + 2·ng·z,
	ld	d, #0				; вершины 24 + 28·ng + 2·nVert
	ld	hl, #0
	ld	b, #28
2$:	add	hl, de
	djnz	2$
	ld	bc, #24
	add	hl, bc
	ld	bc, (dt_hdr + 16)
	add	hl, bc
	add	hl, bc
	ld	(dt_voff), hl
	ld	a, (work)
	call	_pg_map3
	call	tables
	ld	hl, #24				; записи -> DT_REC
	call	dt_src
	ld	hl, #DT_REC
	ld	(fd_do), hl
	ld	a, (dt_ng)
	ld	l, a
	ld	h, #0
	add	hl, hl
	add	hl, hl
	add	hl, hl
	add	hl, hl
	ld	(fd_n), hl
	push	hl
	call	far_dma
	pop	hl				; пределы зума -> DT_M
	ld	a, (dt_ng)
	ld	e, a
	ld	d, #0
	ld	a, (r_z)
	or	a, a
	jr	z, 4$
	ld	b, a
3$:	add	hl, de
	add	hl, de
	djnz	3$
4$:	ld	bc, #24
	add	hl, bc
	call	dt_src
	ld	hl, #DT_M
	ld	(fd_do), hl
	ld	a, (dt_ng)
	ld	l, a
	ld	h, #0
	add	hl, hl
	ld	(fd_n), hl
	call	far_dma
	call	dma_wait
	ld	a, (work)
	call	_pg_map3
	ld	hl, #0xC000 + DT_REC + 8	; центры (6 байт с +8 записи) -> DT_CV
	ld	de, #0xC000 + DT_CV
	ld	a, (dt_ng)
	ld	b, a
5$:	push	bc
	ld	bc, #6
	ldir
	ld	bc, #10
	add	hl, bc
	pop	bc
	djnz	5$
	ld	hl, #0xC000 + DT_CV
	ld	(_gl_pv), hl
	ld	hl, #0xC000 + DT_CP
	ld	(_gl_pd), hl
	ld	a, (dt_ng)
	ld	(_gl_pn), a
	xor	a, a
	ld	(_gl_noz), a
	call	_gl_project
	ld	hl, #0xC000 + DT_REC
	ld	(_gl_crp), hl
	ld	hl, #0xC000 + DT_CP
	ld	(_gl_cpp), hl
	ld	hl, #0xC000 + DT_M
	ld	(_gl_cmp), hl
	ld	hl, #0xC000 + DT_VIS + 1
	ld	(_gl_vsp), hl
	ld	a, (dt_ng)
	ld	(_gl_cn), a
	call	_gl_cull
	;; вершины видимых групп нужного зума подряд в DT_V; список DT_VIS — только взятые группы
	ld	hl, #0
	ld	(dt_nv), hl
	ld	hl, #0xC000 + DT_VIS + 1
	ld	(dt_vi), hl
	ld	(dt_vo), hl
	xor	a, a
	ld	(dt_nk), a
	ld	a, (_gl_nvis)
	or	a, a
	jp	z, dt_proj
	ld	b, a
dt_grp:
	push	bc
	ld	a, (work)
	call	_pg_map3
	ld	hl, (dt_vi)
	ld	a, (hl)
	inc	hl
	ld	(dt_vi), hl
	and	a, #0x7F
	ld	(dt_g), a
	ld	l, a				; запись группы
	ld	h, #0
	add	hl, hl
	add	hl, hl
	add	hl, hl
	add	hl, hl
	ld	de, #0xC000 + DT_REC
	add	hl, de
	ld	e, (hl)				; DE — первая вершина
	inc	hl
	ld	d, (hl)
	inc	hl
	ld	c, (hl)				; C — вершин
	inc	hl
	ld	a, (r_z)			; вид группы (0 — линия, 1 — страны, 2 — города) <= зум − 1
	dec	a
	cp	a, (hl)
	jr	c, dt_gnext
	ld	hl, (dt_nv)			; места хватает
	ld	b, #0
	add	hl, bc
	push	hl
	push	de
	ld	de, #DT_VMAX + 1
	or	a, a
	sbc	hl, de
	pop	de
	pop	hl
	jr	nc, dt_gnext
	push	bc
	ld	c, l				; dst = DT_V + nv · 6, nv += n
	ld	b, h
	ld	hl, (dt_nv)
	ld	(dt_nv), bc
	ld	c, l
	ld	b, h
	add	hl, hl
	add	hl, bc
	add	hl, hl
	ld	bc, #DT_V
	add	hl, bc
	push	hl
	ex	de, hl				; src = voff + first · 6
	ld	e, l
	ld	d, h
	add	hl, hl
	add	hl, de
	add	hl, hl
	ld	de, (dt_voff)
	add	hl, de
	call	dt_src
	pop	hl
	ld	(fd_do), hl
	pop	bc
	ld	l, c
	ld	h, #0
	ld	e, l
	ld	d, h
	add	hl, hl
	add	hl, de
	add	hl, hl
	ld	(fd_n), hl
	call	far_dma
	ld	a, (work)
	call	_pg_map3
	ld	hl, (dt_vo)			; группа — в список
	ld	a, (dt_g)
	ld	(hl), a
	inc	hl
	ld	(dt_vo), hl
	ld	hl, #dt_nk
	inc	(hl)
dt_gnext:
	pop	bc
	dec	b
	jp	nz, dt_grp
dt_proj:
	call	dma_wait
	ld	a, (work)
	call	_pg_map3
	ld	a, (dt_nk)
	ld	(0xC000 + DT_VIS), a
	ld	hl, (dt_nv)
	call	proj_v
	ld	a, (r_z)
	ld	(dt_z), a
	ld	hl, (r_lon)
	ld	(dt_lon), hl
	ld	hl, (r_lat)
	ld	(dt_lat), hl
	ld	a, #1
	ld	(dt_ok), a
dt_draw:					; globe_det(fresh = A, z, work)
	ld	(dt_fr), a
	ld	c, a
	ld	a, (work)
	push	af
	inc	sp
	ld	a, (r_z)
	push	af
	inc	sp
	ld	a, c
	push	af
	inc	sp
	ld	e, #b_globe_det
	ld	hl, #_globe_det
	call	___sdcc_bcall_ehl
	ld	hl, #3
	add	hl, sp
	ld	sp, hl
	ld	a, (_gl_dbg)			; "globe: detail, zoom Z, frames F, fresh X"
	or	a, a
	ret	z
	ld	hl, #s_det
	call	_dbg_puts
	ld	a, (r_z)
	call	dec8
	ld	hl, #s_fr
	call	_dbg_puts
	ld	hl, (_frames)
	ld	de, (dt_t0)
	or	a, a
	sbc	hl, de
	call	dec16
	ld	hl, #s_fresh
	call	_dbg_puts
	ld	a, (dt_fr)
	call	dec8
	ld	hl, #s_nl
	jp	_dbg_puts

;; HL вершин с DT_V рабочей страницы (Win3 = рабочая, таблицы вида готовы) -> проекции x, y, z на месте,
;; пачками до 200 (_gl_pn — байт)
proj_v:
	push	hl
	ld	hl, #0xC000 + DT_V
	ld	(_gl_pv), hl
	ld	(_gl_pd), hl
	xor	a, a
	ld	(_gl_noz), a
	pop	hl
1$:	ld	a, h
	or	a, a
	jr	nz, 2$
	ld	a, l
	or	a, a
	ret	z
	cp	a, #200
	jr	c, 3$
2$:	ld	a, #200
3$:	ld	(_gl_pn), a
	ld	e, a
	ld	d, #0
	or	a, a
	sbc	hl, de
	push	hl
	ld	l, e				; DE = пачка · 6
	ld	h, d
	add	hl, hl
	add	hl, de
	add	hl, hl
	push	hl
	call	_gl_project
	pop	de
	ld	hl, (_gl_pv)
	add	hl, de
	ld	(_gl_pv), hl
	ld	(_gl_pd), hl
	pop	hl
	jr	1$

;; HL — смещение в ресурсе GLOBEDET -> fd_sp, fd_so (источник far_dma), fd_dp = рабочая
dt_src:
	push	hl
	ld	hl, #dt_res
	call	far_load			; HL:DE — страница << 14 | смещение
	ld	a, d
	rlca
	rlca
	and	a, #3
	ld	b, a
	ld	a, l
	add	a, a
	add	a, a
	or	a, b
	ld	(fd_sp), a
	ld	a, d
	and	a, #0x3F
	ld	d, a
	pop	hl
	add	hl, de
	ld	(fd_so), hl
	ld	a, (work)
	ld	(fd_dp), a
	ret

;; ================================================================ входы

;; void globe_invalidate(void) __banked — задний буфер и кэши недействительны
_globe_invalidate::
	xor	a, a
	ld	(dt_ok), a
	ld	(valid), a
	ld	(geom), a
	ld	(kc_ok), a
	ld	(row_meta), a
	ld	a, #0xFF
	ld	(cm_zoom), a
	ld	(bg_zoom), a
	ld	(v_zoom), a
	ld	(row_zoom), a
	ld	(v_sun), a
	ret

;; void globe_draw(void) __banked — окно глобуса (перерисовка при смене вида или эпохи солнца,
;; иначе копия заднего буфера). ST — в Win3.
_globe_prepare::
	ld	a, #1				; только рендер в задний буфер, без копии на экран
	ld	(pre_only), a
	jr	gd_entry

_globe_draw::
	xor	a, a
	ld	(pre_only), a
gd_entry:
	push	ix
	ld	a, (g_init)
	or	a, a
	jr	nz, 1$
	call	_globe_invalidate
	ld	a, #0xFF
	ld	(strip_set), a
	ld	a, #1
	ld	(g_init), a
1$:	ld	a, (_st_ + ST_ZOOM)
	cp	a, #6
	jr	c, 2$
	ld	a, #5
2$:	ld	(r_z), a
	ld	hl, (_ctx + CTX_LON)
	ld	(r_lon), hl
	ld	hl, (_ctx + CTX_LAT)
	ld	(r_lat), hl
	ld	a, (work)			; страницы (0 — не выделена; неудача — повтор в следующий раз)
	or	a, a
	jr	nz, 3$
	ld	l, #1
	ld	a, #1
	call	_pg_alloc
	cp	a, #PG_NONE
	jr	z, 3$
	ld	(work), a
3$:	ld	a, (strip)
	or	a, a
	jr	nz, 4$
	ld	l, #1
	ld	a, #8
	call	_pg_alloc
	cp	a, #PG_NONE
	jr	z, 4$
	ld	(strip), a
4$:	ld	a, (epage)
	or	a, a
	jr	nz, 5$
	ld	l, #1
	ld	a, #1
	call	_pg_alloc
	cp	a, #PG_NONE
	jr	z, 5$
	ld	(epage), a
5$:	ld	a, (work)
	or	a, a
	jr	z, 6$
	ld	a, (strip)
	or	a, a
	jr	z, 6$
	ld	a, (epage)
	or	a, a
	jr	nz, 7$
6$:	ld	hl, #s_nopg
	call	_dbg_puts
	pop	ix
	ret
7$:	ld	a, (epage)			; предрасчитанные виды с карты (все зумы)
	push	af
	inc	sp
	ld	e, #b_gview_open
	ld	hl, #_gview_open
	call	___sdcc_bcall_ehl
	inc	sp
	ld	e, #b_globe_sunlon		; ST ещё в Win3
	ld	hl, #_globe_sunlon
	call	___sdcc_bcall_ehl
	ld	(r_sun), de
	call	_pg_win3
	push	af
	ld	hl, (r_sun)			; эпоха солнца (u8)(sun >> 7): 0.7°
	add	hl, hl
	ld	a, h
	ld	(d_se), a
	ld	a, (valid)
	or	a, a
	jr	z, 8$
	ld	a, (v_zoom)
	ld	b, a
	ld	a, (r_z)
	cp	a, b
	jr	nz, 8$
	ld	hl, (v_lon)
	ld	de, (r_lon)
	or	a, a
	sbc	hl, de
	jr	nz, 8$
	ld	hl, (v_lat)
	ld	de, (r_lat)
	or	a, a
	sbc	hl, de
	jr	nz, 8$
	ld	a, (v_sun)
	ld	b, a
	ld	a, (d_se)
	cp	a, b
	jr	z, 9$
8$:	call	render
	call	detail
	ld	a, (d_se)
	ld	(v_sun), a
9$:	ld	a, (pre_only)
	or	a, a
	call	z, blit
	pop	af
	call	_pg_map3
	pop	ix
	ret
