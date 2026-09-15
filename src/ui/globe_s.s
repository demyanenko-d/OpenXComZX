;; Банк 24: горячие циклы рендера глобуса (src/ui/globe.c; project_docs/globe.md §5.4, М4в).
;;
;; Конвертер (Core/Globe.cs) заранее строит плоскую карту: рёбра — только границы, где
;; текстура меняется, у каждого текстуры слева и справа, рёбра пересекаются лишь в концах.
;; Проходы кадра (вызывает globe.c render):
;;   _gl_project — вершины ячейки (векторы) -> x, y (Q2: четверти пикселя, окно 0..1023 x
;;                 0..799) и z8 (Q6) таблицами произведений;
;;   _gl_edges   — рёбра ячейки: горизонт (часть за ним отрезается), окно по x, события
;;                 левого края (строки без рёбер берут текстуру края), шаг по строкам ->
;;                 запись в страницу рёбер, корзина по первой строке;
;;   _gl_rows    — строки 0..199: список активных рёбер по x, починка порядка в кучках почти
;;                 совпавших рёбер, вывод строки отрезками одной текстуры (DMA).
;;
;; Выборка: строка r — центр y = r + 0.5 (Q2: 4r + 2); ребро [ya, yb) задевает строки
;; r0 = (ya + 1) >> 2 .. r1 = ((yb + 1) >> 2) - 1. Граница в парах пикселей (DMA пишет
;; словами): u = x / 2 + 0.75 (8.8), пара floor(u) и правее — справа от ребра.
;;
;; Страница рёбер (Win3 на время _gl_rows и записи рёбер):
;;   EP_BT      [200] — текстура полосы строк без рёбер по сетке 5° (#FE — нет; globe.c bands)
;;   EP_INS     [127] — новые слоты строки (зумы 0–1: место по текстурам — ai_redo)
;;   EP_ROW     [200] x 6: pl, pr (пары диска; pl > pr — строки нет), DAH, DAX (строка
;;              280 + y заднего буфера), блок узора строки без текстуры ((y & 3) * 14),
;;              страница узоров
;;   EP_EVB/EVA [200] — текстура левого края ниже / выше события в строке (#FE — нет),
;;   EP_EVKB/KA [200] — ключ события (положение в строке * 4 + порядок)
;;   EP_SUL..STR [127] — поля активных рёбер по слоту (u, шаг, последняя строка, текстуры)
;;   EP_ORD, EP_OUH [127] — слоты по x и старшие байты u (граница в парах); EP_FREE — слоты
;;   EP_BUCKET  u16 [200] — первое ребро корзины строки (0 — нет); корзины и записи рёбер
;;              пишутся в рабочую страницу с тех же смещений и копируются DMA (globe.c)
;;   EP_POOL..  записи рёбер по 10 байт: +0 u (8.8), +2 шаг u (8.8 со знаком), +4 последняя
;;              строка, +5 текстура слева, +6 справа (13 — океан), +8 следующее в корзине
;;
;; Второй набор регистров (EXX, AF') портится; прерывание кадра (crt0.s) его не трогает.

	.module globe_s
	.optsdcc -mz80 sdcccall(1)

	.globl	_gl_edges, edge1, edge1s, e_rows, e_fast, hclip, lerpz, xclip, lerpx, div32, eslope, bord_ev, e_store
	.globl	_gl_bands, _gl_nb, _gl_bl, gb_end, ai_put, ai_redo, rw_emit_s, em_run_s
	.globl	_gl_rows, ael_ins, ai_fix2, f2_near, f2_score, ai_cmp, ael_fix, fx_pair, rw_emit, rw_band, rw_pfill, rw_flush, ael_step, ael_sort
	.globl	_gl_eb
	.globl	_gl_ec
	.globl	_gl_res
	.globl	_gl_wpg
	.globl	_gl_epg
	.globl	_gl_eptr
	.globl	_gl_nedge
	.globl	_gl_limb
	.globl	_gl_sptr
	.globl	_gl_gtex
	.globl	_recip_tab
	.globl	_gl_project, kt_put16, kt_lo16, mul8e, mulu16
	.globl	_gl_noz
	.globl	_gl_pv
	.globl	_gl_pd
	.globl	_gl_pn
	.globl	_gl_ktab
	.globl	_gl_k
	.globl	_gl_kpg
	.globl	_gl_sq

EP_BT		= 0xC000 + 0x0000	; текстура полосы строк без рёбер по сетке (globe.c)
EP_INS		= 0xC000 + 0x0100	; новые слоты строки (ai_redo)
EP_ROW		= 0xC000 + 0x0200
EP_EVB		= 0xC000 + 0x0700
EP_EVA		= 0xC000 + 0x0800
EP_EVKB		= 0xC000 + 0x0900
EP_EVKA		= 0xC000 + 0x0A00
EP_SUL		= 0xC000 + 0x0B00	; слоты: u (младший), шаг (младший) — страница выше,
EP_SSL		= 0xC000 + 0x0C00
EP_SUH		= 0xC000 + 0x0D00	;   u (старший), шаг (старший),
EP_SSH		= 0xC000 + 0x0E00
EP_SLAST	= 0xC000 + 0x0F00	;   последняя строка, текстуры слева / справа
EP_STL		= 0xC000 + 0x1000
EP_STR		= 0xC000 + 0x1100
EP_ORD		= 0xC000 + 0x1200	; порядок по x: слоты, старшие байты u (страница выше)
EP_OUH		= 0xC000 + 0x1300
EP_FREE		= 0xC000 + 0x1400	; свободные слоты
EP_BUCKET	= 0xC000 + 0x1500	; корзины (копия из рабочей страницы после рёбер)
EP_SHF		= 0xC000 + 0x1700	; флаги строк с тенью (globe.c: globe_shadow)
EP_POOL		= 0xC000 + 0x1800	; записи рёбер (копии буфера рабочей страницы по ячейкам)
WB_BUCKET	= 0xC000 + 0x3080	; рабочая страница: корзины на время рёбер
WB_COV		= 0xC000 + 0x3300	;   покрытие строк рёбрами (разности, globe.c)
EP_END		= 0xC000 + 0x4000 - 10
AEL_MAX		= 127
PAGE3_PORT	= 0x13AF
GH		= 200
XR		= 1024			; правый край окна в Q2

	.area	_DATA

e_ptr:	.ds	2			; текущая запись ребра (рабочая страница)
e_cnt:	.ds	1
e_tL:	.ds	1			; текстуры слева / справа от va -> vb
e_tR:	.ds	1
e_tl:	.ds	1			; слева / справа на экране (ребро сверху вниз)
e_tr:	.ds	1
e_hz:	.ds	1			; 1 — горизонтальное
e_hb:	.ds	1			; у горизонтального: текстура снизу / сверху
e_ha:	.ds	1
e_xa:	.ds	2			; концы (Q2)
e_ya:	.ds	2
e_xb:	.ds	2
e_yb:	.ds	2
e_za:	.ds	2			; z концов (Q12)
e_zb:	.ds	2
e_lba:	.ds	1			; 1 — конец на левом краю (окна или диска)
e_lbb:	.ds	1
e_r0:	.ds	2			; первая / последняя строка
e_r1:	.ds	2
e_s:	.ds	2			; шаг u (8.8)
e_u:	.ds	2			; u первой строки
e_neg:	.ds	1
e_dy:	.ds	2			; быстрый путь: dy, |dx| (младший байт)
e_dxl:	.ds	1
lz_a:	.ds	2			; горизонт: z переднего конца, разность z
lz_d:	.ds	2
lx_s:	.ds	1
mu_a:	.ds	2
mu_b:	.ds	2
mu_r0:	.ds	2
mu_c:	.ds	1
pj_dst:	.ds	2			; проекция: запись результата
pj_n:	.ds	1
kt_sh:	.ds	2			; таблицы: шаг (старшее, младшее слово), страница
kt_sl:	.ds	2
kt_pg:	.ds	1
rw_y:	.ds	1			; строка
rw_n:	.ds	1			; активных рёбер
rw_bk:	.ds	2			; корзина строки
rw_lt:	.ds	1			; текстура левого края прошлой строки (#FE — неизвестна)
rw_pend: .ds	1			; первая строка без текстуры (#FF — нет)
em_start: .ds	1			; пары: начало отрезка, правый край
em_pr:	.ds	1
em_tex:	.ds	1			; текстура отрезка
em_first: .ds	1			; 1 — в строке уже был отрезок (регистры DMA продолжаются)
em_row:	.ds	1			; rw_flush: строка (флаг тени EP_SHF)
pf_t:	.ds	1
fx_l:	.ds	1			; починка: текстура левее пары, справа от пары
fx_r:	.ds	1
fx_pl:	.ds	1			; стороны пары p, q
fx_pr:	.ds	1
fx_ql:	.ds	1
fx_qr:	.ds	1
rw_nf:	.ds	1			; свободных слотов
rw_srt:	.ds	1			; 1 — после шага нужна сортировка
rw_dirty: .ds	1			; 1 — вставки или сдвиг сортировкой: полная починка
ai_sl:	.ds	1			; вставка: слот, старший байт u, место
ai_uh:	.ds	1
ai_k:	.ds	1
fx_k:	.ds	1			; починка: место p
f2_bs:	.ds	1			; вставка в кучку: лучшая оценка, место, границы
f2_bk:	.ds	1
f2_hi:	.ds	1
rw_ni:	.ds	1			; новых рёбер в строке (EP_INS; зумы 0–1)
ar_k0:	.ds	1			; ai_redo: прежнее место, 1 — что-то сдвинулось
ar_mv:	.ds	1
fx_def:	.ds	1			; ael_fix: 1 — левая текстура неизвестна (первая пара — последней)
gb_in:	.ds	1			; _gl_bands: 1 — внутри полосы, её первая строка
gb_y0:	.ds	1

	.area	_BANK24

;; ================================================================ рёбра

;; _gl_ec записей рёбер (6 байт, конвертер Globe.cs) с _gl_eb: +0 va, +2 vb (номер вершины
;; ячейки x 5 — смещение от _gl_res), +4 texL, +5 texR. Win3 — рабочая страница (_gl_wpg);
;; запись ребра — туда же (EP_POOL по _gl_eptr, корзины EP_BUCKET), события края — в страницу
;; рёбер (_gl_epg).
_gl_edges::
	push	ix
	push	iy
	ld	hl, (_gl_eb)
	ld	(e_ptr), hl
	ld	a, (_gl_ec)
	or	a, a
	jr	z, 2$
	ld	(e_cnt), a
1$:	call	edge1
	ld	hl, (e_ptr)
	ld	bc, #6
	add	hl, bc
	ld	(e_ptr), hl
	ld	hl, #e_cnt
	dec	(hl)
	jr	nz, 1$
2$:	pop	iy
	pop	ix
	ret

;; Концы (x, y, z) из проекций по смещению DE -> X, Y, Z
	.macro	VLOAD	X, Y, Z
	ld	hl, (_gl_res)
	add	hl, de
	ld	e, (hl)
	inc	hl
	ld	d, (hl)
	inc	hl
	ld	(X), de
	ld	e, (hl)
	inc	hl
	ld	d, (hl)
	inc	hl
	ld	(Y), de
	ld	e, (hl)
	inc	hl
	ld	d, (hl)
	ld	(Z), de
	.endm

;; Быстрый путь: оба конца перед горизонтом, x обоих в [0, 1023], не горизонтальное —
;; концы сверху вниз в e_xa/e_ya (верх), e_xb/e_yb (низ) и сразу строки; иначе — edge1s
;; (горизонт, окно по x, события левого края). Горизонтальное внутри окна строк не задевает.
edge1:
	ld	hl, (e_ptr)
	ld	e, (hl)
	inc	hl
	ld	d, (hl)
	inc	hl
	ld	c, (hl)
	inc	hl
	ld	b, (hl)
	inc	hl
	ld	a, (hl)
	ld	(e_tL), a
	inc	hl
	ld	a, (hl)
	ld	(e_tR), a
	ld	hl, (_gl_res)
	add	hl, bc
	ex	de, hl			; DE = &B, HL = va
	ld	bc, (_gl_res)
	add	hl, bc			; HL = &A
	push	hl
	ld	hl, #5
	add	hl, de
	ld	a, (hl)			; zb (старший байт)
	pop	hl
	push	hl
	ld	bc, #5
	add	hl, bc
	ld	c, (hl)			; za (старший байт)
	pop	hl
	ld	b, a
	and	a, c
	ret	m			; оба за горизонтом
	ld	a, b
	or	a, c
	jp	m, edge1s		; один за горизонтом
	ld	c, (hl)			; xa
	inc	hl
	ld	b, (hl)
	inc	hl
	ld	(e_xa), bc
	ld	a, (hl)			; ya
	inc	hl
	ld	h, (hl)
	ld	l, a
	ld	(e_ya), hl
	ex	de, hl			; HL = &B
	ld	e, (hl)			; xb
	inc	hl
	ld	d, (hl)
	inc	hl
	ld	(e_xb), de
	ld	a, b			; x обоих в [0, 1023]
	or	a, d
	and	a, #0xFC
	jp	nz, edge1s
	ld	a, (hl)			; yb
	inc	hl
	ld	h, (hl)
	ld	l, a
	ld	(e_yb), hl
	ld	de, (e_ya)
	or	a, a
	sbc	hl, de
	ret	z			; горизонтальное
	jp	p, 1$
	ld	a, (e_tL)		; вверх: концы меняются, слева texL
	ld	(e_tl), a
	ld	a, (e_tR)
	ld	(e_tr), a
	ld	hl, (e_xa)
	ld	de, (e_xb)
	ld	(e_xa), de
	ld	(e_xb), hl
	ld	hl, (e_ya)
	ld	de, (e_yb)
	ld	(e_ya), de
	ld	(e_yb), hl
	jp	e_fast
1$:	ld	a, (e_tR)		; вниз: слева texR
	ld	(e_tl), a
	ld	a, (e_tL)
	ld	(e_tr), a
	jp	e_fast

;; Медленный путь (горизонт, окно по x, события края)
edge1s:
	ld	hl, (e_ptr)
	ld	e, (hl)
	inc	hl
	ld	d, (hl)
	inc	hl
	ld	c, (hl)
	inc	hl
	ld	b, (hl)
	inc	hl
	ld	a, (hl)
	ld	(e_tL), a
	inc	hl
	ld	a, (hl)
	ld	(e_tR), a
	push	bc
	VLOAD	e_xa, e_ya, e_za
	pop	de
	VLOAD	e_xb, e_yb, e_zb
	xor	a, a
	ld	(e_lba), a
	ld	(e_lbb), a
	ld	(e_hz), a
	call	hclip			; горизонт (NZ — ребро целиком за ним)
	ret	nz
	; сторона на экране: вниз — слева texR; вверх — концы меняются, слева texL;
	; горизонтальное: снизу texR, если идёт вправо, иначе texL
	ld	hl, (e_yb)
	ld	de, (e_ya)
	or	a, a
	sbc	hl, de
	jr	nz, 2$
	ld	a, #1
	ld	(e_hz), a
	ld	hl, (e_xb)
	ld	de, (e_xa)
	or	a, a
	sbc	hl, de
	ld	a, (e_tR)
	ld	b, a
	ld	a, (e_tL)
	ld	c, a			; вправо: снизу B = texR, сверху C = texL
	jp	p, 1$
	ld	a, b
	ld	b, c
	ld	c, a
1$:	ld	a, b
	ld	(e_hb), a
	ld	a, c
	ld	(e_ha), a
	jr	4$
2$:	jp	m, 3$
	ld	a, (e_tR)		; вниз
	ld	(e_tl), a
	ld	a, (e_tL)
	ld	(e_tr), a
	jr	4$
3$:	ld	a, (e_tL)		; вверх: A <-> B
	ld	(e_tl), a
	ld	a, (e_tR)
	ld	(e_tr), a
	ld	hl, (e_xa)
	ld	de, (e_xb)
	ld	(e_xa), de
	ld	(e_xb), hl
	ld	hl, (e_ya)
	ld	de, (e_yb)
	ld	(e_ya), de
	ld	(e_yb), hl
	ld	a, (e_lba)
	ld	b, a
	ld	a, (e_lbb)
	ld	(e_lba), a
	ld	a, b
	ld	(e_lbb), a
4$:	call	xclip			; окно по x (NZ — ребро целиком вне)
	ret	nz
	; события левого края: ребро сверху вниз, конец на краю — выше / ниже точки на краю
	; текстура сторон (верх на краю: ниже tl, выше tr; низ на краю: ниже tr, выше tl)
	ld	a, (e_hz)
	or	a, a
	jr	z, 5$
	ld	a, (e_lba)
	ld	b, a
	ld	a, (e_lbb)
	or	a, b
	ret	z
	ld	hl, (e_ya)
	ld	a, (e_hb)
	ld	d, a
	ld	a, (e_ha)
	ld	e, a
	ld	c, #1
	jp	bord_ev			; горизонтальное — только событие
5$:	ld	a, (e_lba)
	or	a, a
	jr	z, 6$
	ld	hl, (e_ya)
	ld	a, (e_tl)
	ld	d, a
	ld	a, (e_tr)
	ld	e, a
	ld	c, #0
	call	bord_ev
6$:	ld	a, (e_lbb)
	or	a, a
	jr	z, 7$
	ld	hl, (e_yb)
	ld	a, (e_tr)
	ld	d, a
	ld	a, (e_tl)
	ld	e, a
	ld	c, #2
	call	bord_ev
7$:
;; Строки ребра (верх e_xa/e_ya, низ e_xb/e_yb): r0 = (ya + 1) >> 2 .. r1 = ((yb + 1) >> 2) - 1
e_rows:
	ld	hl, (e_yb)
	ld	de, (e_ya)
	or	a, a
	sbc	hl, de
	ret	z			; после отсечения — горизонтальное
	ret	m
	ld	hl, (e_ya)
	inc	hl
	sra	h
	rr	l
	sra	h
	rr	l
	ld	(e_r0), hl
	ex	de, hl
	ld	hl, (e_yb)
	inc	hl
	sra	h
	rr	l
	sra	h
	rr	l
	dec	hl
	ld	(e_r1), hl
	bit	7, h
	ret	nz			; r1 < 0
	or	a, a
	sbc	hl, de
	ret	m			; r1 < r0
	ld	hl, #GH - 1
	or	a, a
	sbc	hl, de
	ret	m			; r0 > 199
	call	eslope			; e_s
	; u = 32 xa + 192 + off * s / 4, off = (2 - (ya & 3)) & 3
	ld	hl, (e_xa)
	add	hl, hl
	add	hl, hl
	add	hl, hl
	add	hl, hl
	add	hl, hl
	ld	de, #192
	add	hl, de
	ex	de, hl			; DE = u
	ld	a, (e_ya)
	and	a, #3
	ld	c, a
	ld	a, #2
	sub	a, c
	and	a, #3
	jr	z, 10$
	ld	hl, (e_s)
	sra	h
	rr	l
	cp	a, #2
	jr	z, 9$			; off 2: s / 2
	push	hl
	sra	h
	rr	l			; s / 4
	cp	a, #1
	jr	z, 8$
	pop	bc			; off 3: s / 2 + s / 4
	add	hl, bc
	jr	9$
8$:	pop	bc
9$:	add	hl, de
	ex	de, hl
10$:	ld	(e_u), de
	; начало выше окна: u += (-r0) * s, r0 = 0
	ld	hl, (e_r0)
	bit	7, h
	jr	z, 11$
	ex	de, hl
	ld	hl, #0
	or	a, a
	sbc	hl, de			; -r0
	ld	de, (e_s)
	call	mulu16			; младшие 16 бит — в DE
	ld	hl, (e_u)
	add	hl, de
	ld	(e_u), hl
	ld	hl, #0
	ld	(e_r0), hl
11$:	ld	hl, (e_r1)		; конец ниже окна
	ld	de, #GH
	or	a, a
	sbc	hl, de
	jp	c, e_store
	ld	a, #GH - 1
	ld	(e_r1), a
	jp	e_store

;; Быстрый путь ребра одним куском (оба конца перед горизонтом, x в окне): верх — e_xa/e_ya,
;; низ — e_xb/e_yb, стороны — e_tl/e_tr. Строки r0..r1, шаг s = (|dx| · 65536/dy + 256) >> 9
;; (|dx| < 256 — два умножения 8 x 8 в коде; иначе — eslope), u первой строки, запись в
;; корзину. Пул рёбер проверяет globe.c (перед ячейкой).
e_fast:
	ld	de, (e_ya)
	ld	hl, (e_yb)
	or	a, a
	sbc	hl, de
	ld	(e_dy), hl		; dy > 0
	inc	de			; r0 = (ya + 1) >> 2
	sra	d
	rr	e
	sra	d
	rr	e
	ld	hl, (e_yb)		; r1 = ((yb + 1) >> 2) - 1
	inc	hl
	sra	h
	rr	l
	sra	h
	rr	l
	dec	hl
	bit	7, h
	ret	nz			; r1 < 0
	push	hl
	or	a, a
	sbc	hl, de
	pop	bc			; BC = r1
	ret	m			; r1 < r0
	ld	hl, #GH - 1
	or	a, a
	sbc	hl, de
	ret	m			; r0 > 199
	ld	(e_r0), de
	ld	a, b			; r1 > 199 -> 199
	or	a, a
	jr	nz, 1$
	ld	a, c
	cp	a, #GH
	jr	c, 2$
1$:	ld	a, #GH - 1
2$:	ld	(e_r1), a
	ld	hl, (e_xb)		; |dx| (x обоих в [0, 1023])
	ld	de, (e_xa)
	xor	a, a
	sbc	hl, de
	jr	nc, 3$
	ex	de, hl
	ld	hl, #0
	or	a, a
	sbc	hl, de
	inc	a
3$:	ld	(e_neg), a
	ld	a, h
	or	a, a
	jr	nz, 20$			; |dx| >= 256 — общий наклон
	ld	a, l
	ld	(e_dxl), a
	ld	hl, (e_dy)
	ld	a, h
	cp	a, #2
	jr	nc, 20$			; dy >= 512 — общий наклон
	add	hl, hl
	ld	de, #_recip_tab
	add	hl, de
	ld	a, (hl)
	inc	hl
	ld	h, (hl)
	ld	l, a			; HL = 65536 / dy
	or	a, h
	jr	z, 20$			; dy = 1
	ld	a, (e_dxl)
	ld	b, a
	ld	c, l
	push	hl
	ld	a, b			; DE = B * C: q(B + C) - q(|B - C|)
	add	a, c
	ld	l, a
	ld	a, #>_gl_sq
	adc	a, #0
	ld	h, a
	ld	e, (hl)
	inc	h
	inc	h
	ld	d, (hl)
	ld	a, b
	sub	a, c
	jr	nc, 4$
	neg
4$:	ld	l, a
	ld	h, #>_gl_sq
	ld	a, e
	sub	a, (hl)
	ld	e, a
	inc	h
	inc	h
	ld	a, d
	sbc	a, (hl)
	ld	d, a
	pop	hl			; |dx| · rL
	ld	a, h
	ld	c, a
	push	de
	ld	a, b			; DE = B * C: q(B + C) - q(|B - C|)
	add	a, c
	ld	l, a
	ld	a, #>_gl_sq
	adc	a, #0
	ld	h, a
	ld	e, (hl)
	inc	h
	inc	h
	ld	d, (hl)
	ld	a, b
	sub	a, c
	jr	nc, 5$
	neg
5$:	ld	l, a
	ld	h, #>_gl_sq
	ld	a, e
	sub	a, (hl)
	ld	e, a
	inc	h
	inc	h
	ld	a, d
	sbc	a, (hl)
	ld	d, a
	pop	hl			; + (|dx| · rL) >> 8 + 1, >> 1
	ld	l, h
	ld	h, #0
	add	hl, de
	inc	hl
	srl	h
	rr	l
	ld	a, (e_neg)
	or	a, a
	jr	z, 6$
	ex	de, hl
	ld	hl, #0
	sbc	hl, de			; C = 0 после or
6$:	ld	(e_s), hl
	jr	7$
20$:	call	eslope			; общий случай (длинные, крутые, dy = 1)
	ld	hl, (e_s)
7$:	; u = 32 xa + 192 + off · s / 4, off = (2 - (ya & 3)) & 3; HL = s
	ld	a, (e_ya)
	and	a, #3
	ld	c, a
	ld	a, #2
	sub	a, c
	and	a, #3
	ld	de, #0
	jr	z, 10$
	sra	h
	rr	l			; s / 2
	cp	a, #2
	jr	z, 9$
	ld	d, h
	ld	e, l
	sra	h
	rr	l			; s / 4
	cp	a, #1
	jr	z, 9$
	add	hl, de			; s / 2 + s / 4
9$:	ex	de, hl
10$:	ld	hl, (e_xa)
	add	hl, hl
	add	hl, hl
	add	hl, hl
	add	hl, hl
	add	hl, hl
	add	hl, de
	ld	de, #192
	add	hl, de
	ld	(e_u), hl
	ld	hl, (e_r0)		; начало выше окна: u += (-r0) · s, r0 = 0
	bit	7, h
	jp	z, e_store
	ex	de, hl
	ld	hl, #0
	or	a, a
	sbc	hl, de
	ld	de, (e_s)
	call	mulu16			; младшие 16 бит — в DE
	ld	hl, (e_u)
	add	hl, de
	ld	(e_u), hl
	ld	hl, #0
	ld	(e_r0), hl

;; Запись ребра (e_u, e_s, e_r1, e_tl, e_tr) в корзину строки e_r0: байты — в буфер рабочей
;; страницы (_gl_sptr), адрес записи и связи — в странице рёбер (_gl_eptr; буфер туда копирует
;; globe.c после ячейки), корзины — в рабочей странице (WB_BUCKET)
e_store:
	ld	hl, (_gl_sptr)
	ld	de, (e_u)
	ld	(hl), e
	inc	hl
	ld	(hl), d
	inc	hl
	ld	de, (e_s)
	ld	(hl), e
	inc	hl
	ld	(hl), d
	inc	hl
	ld	a, (e_r1)
	ld	(hl), a
	inc	hl
	ld	a, (e_tl)
	ld	(hl), a
	inc	hl
	ld	a, (e_tr)
	ld	(hl), a
	inc	hl
	inc	hl
	ex	de, hl			; DE = +8
	ld	hl, (e_r0)		; корзина первой строки
	add	hl, hl
	ld	bc, #WB_BUCKET
	add	hl, bc
	ld	c, (hl)			; прежнее первое -> +8
	inc	hl
	ld	b, (hl)
	ex	de, hl
	ld	(hl), c
	inc	hl
	ld	(hl), b
	ld	bc, (_gl_eptr)		; в корзину — адрес записи в странице рёбер
	ex	de, hl
	ld	(hl), b
	dec	hl
	ld	(hl), c
	ld	de, #10
	ld	hl, (_gl_sptr)
	add	hl, de
	ld	(_gl_sptr), hl
	ld	hl, (_gl_eptr)
	add	hl, de
	ld	(_gl_eptr), hl
	ld	hl, (_gl_nedge)
	inc	hl
	ld	(_gl_nedge), hl
	ld	a, (e_r0)		; покрытие строк: +1 в первой, -1 за последней (globe.c: полосы
	ld	l, a			; строк без рёбер)
	ld	h, #>WB_COV
	inc	(hl)
	ld	a, (e_r1)
	inc	a
	ld	l, a
	dec	(hl)
	ret

;; Событие левого края: точка края y = HL (Q2) — ниже неё текстура D, выше E; порядок C
;; (в одной точке: 0 — у ребра отсечён верх, 1 — горизонтальное, 2 — отсечён низ). Строка
;; r = (y + 1) >> 2, ключ k = ((y + 1) & 3) * 4 + C: EVB — с наибольшим ключом, EVA — с
;; наименьшим.
bord_ev:
	inc	hl
	ld	a, l
	and	a, #3
	add	a, a
	add	a, a
	add	a, c
	ld	c, a
	sra	h
	rr	l
	sra	h
	rr	l
	ld	a, h
	or	a, a
	ret	nz			; строка < 0 или > 255
	ld	a, l
	cp	a, #GH
	ret	nc
	push	bc
	ld	a, (_gl_epg)
	ld	bc, #PAGE3_PORT
	out	(c), a
	pop	bc
	ld	h, #>EP_EVKB
	ld	a, c
	cp	a, (hl)
	jr	c, 1$			; k < EVKB
	ld	(hl), a
	ld	h, #>EP_EVB
	ld	(hl), d
1$:	ld	h, #>EP_EVKA
	ld	a, (hl)
	cp	a, c
	jr	c, 2$			; EVKA < k
	ld	(hl), c
	ld	h, #>EP_EVA
	ld	(hl), e
2$:	ld	a, (_gl_wpg)
	ld	bc, #PAGE3_PORT
	out	(c), a
	ret

;; Горизонт (z в Q12): оба конца за ним — NZ (ребро не нужно); один — заменяется точкой
;; ребра с z = 0: P = F + (K - F) · zF / (zF - zK) (проекция линейна по 3D; F — передний
;; конец, K — задний); на зумах 0–1 (_gl_limb) точка левее центра — на левом краю диска.
hclip:
	ld	a, (e_za + 1)
	ld	c, a
	ld	a, (e_zb + 1)
	ld	b, a
	and	a, c
	rlca
	jr	nc, 1$
	or	a, #1
	ret
1$:	bit	7, c
	jr	nz, 2$
	bit	7, b
	jr	nz, 3$
	xor	a, a
	ret
2$:	ld	hl, (e_zb)		; A за горизонтом: F = B
	ld	(lz_a), hl
	ld	de, (e_za)
	or	a, a
	sbc	hl, de
	ld	(lz_d), hl		; zB - zA
	ld	hl, (e_xa)
	ld	de, (e_xb)
	or	a, a
	sbc	hl, de
	call	lerpz
	ld	de, (e_xb)
	add	hl, de
	ld	(e_xa), hl
	call	limb_l
	ld	(e_lba), a
	ld	hl, (e_ya)
	ld	de, (e_yb)
	or	a, a
	sbc	hl, de
	call	lerpz
	ld	de, (e_yb)
	add	hl, de
	ld	(e_ya), hl
	xor	a, a
	ret
3$:	ld	hl, (e_za)		; B за горизонтом: F = A
	ld	(lz_a), hl
	ld	de, (e_zb)
	or	a, a
	sbc	hl, de
	ld	(lz_d), hl		; zA - zB
	ld	hl, (e_xb)
	ld	de, (e_xa)
	or	a, a
	sbc	hl, de
	call	lerpz
	ld	de, (e_xa)
	add	hl, de
	ld	(e_xb), hl
	call	limb_l
	ld	(e_lbb), a
	ld	hl, (e_yb)
	ld	de, (e_ya)
	or	a, a
	sbc	hl, de
	call	lerpz
	ld	de, (e_ya)
	add	hl, de
	ld	(e_yb), hl
	xor	a, a
	ret

;; A = 1, если точка горизонта x = HL на левом краю диска (зумы 0–1, x < 512), иначе 0
limb_l:
	ld	a, (_gl_limb)
	or	a, a
	ret	z
	ld	de, #512
	or	a, a
	sbc	hl, de
	ld	a, #0
	ret	p
	inc	a
	ret

;; HL = HL · lz_a / lz_d (HL со знаком, 0 <= lz_a <= lz_d < 32768), частное к нулю
lerpz:
	ld	a, h
	ld	(lx_s), a
	bit	7, h
	jr	z, 1$
	ex	de, hl
	ld	hl, #0
	or	a, a
	sbc	hl, de
1$:	ld	de, (lz_a)
	call	mulu16			; HL:DE = |HL| · zF
	ld	bc, (lz_d)
	call	div32			; DE = частное (<= |HL|)
	ld	a, (lx_s)
	rlca
	jr	nc, 2$
	ld	hl, #0
	or	a, a
	sbc	hl, de
	ex	de, hl
2$:	ex	de, hl
	ret

;; Окно по x: [0, XR] (Q2). Оба конца левее / правее — NZ. Конец за краем заменяется точкой
;; ребра на краю (y = ya + (край - xa) · dy / dx, к нулю); на левом — флаг края.
xclip:
	ld	hl, (e_xa)
	ld	a, h
	ld	de, (e_xb)
	and	a, d
	rlca
	jr	nc, 1$
	or	a, #1			; оба < 0
	ret
1$:	bit	7, h
	jr	z, 2$
	ld	hl, #0			; A < 0
	call	lerpx
	ld	(e_ya), hl
	ld	hl, #0
	ld	(e_xa), hl
	ld	a, #1
	ld	(e_lba), a
	jr	3$
2$:	ld	hl, (e_xb)
	bit	7, h
	jr	z, 3$
	ld	hl, #0			; B < 0
	call	lerpx
	ld	(e_yb), hl
	ld	hl, #0
	ld	(e_xb), hl
	ld	a, #1
	ld	(e_lbb), a
3$:	ld	hl, (e_xa)		; правый край: x > XR
	ld	de, #XR + 1
	or	a, a
	sbc	hl, de
	ld	a, h
	cpl
	ld	c, a			; C7 = 1 — A правее
	ld	hl, (e_xb)
	or	a, a
	sbc	hl, de
	ld	a, h
	cpl
	ld	b, a			; B7 = 1 — B правее
	and	a, c
	rlca
	jr	nc, 4$
	or	a, #1			; оба правее
	ret
4$:	bit	7, c
	jr	z, 5$
	ld	hl, #XR
	call	lerpx
	ld	(e_ya), hl
	ld	hl, #XR
	ld	(e_xa), hl
	xor	a, a
	ld	(e_lba), a
	ret
5$:	bit	7, b
	ret	z
	ld	hl, #XR
	call	lerpx
	ld	(e_yb), hl
	ld	hl, #XR
	ld	(e_xb), hl
	xor	a, a
	ld	(e_lbb), a
	ret

;; HL = ya + (HL - xa) · (yb - ya) / (xb - xa), частное к нулю (|HL - xa| <= |xb - xa|)
lerpx:
	ld	de, (e_xa)
	or	a, a
	sbc	hl, de			; HL = край - xa
	ld	a, h
	ld	(lx_s), a		; знак
	bit	7, h
	jr	z, 1$
	ex	de, hl
	ld	hl, #0
	or	a, a
	sbc	hl, de
1$:	push	hl
	ld	hl, (e_yb)
	ld	de, (e_ya)
	or	a, a
	sbc	hl, de			; dy
	ld	a, (lx_s)
	xor	a, h
	ld	(lx_s), a
	bit	7, h
	jr	z, 2$
	ex	de, hl
	ld	hl, #0
	or	a, a
	sbc	hl, de
2$:	pop	de
	call	mulu16			; HL:DE = |край - xa| · |dy|
	push	hl
	push	de
	ld	hl, (e_xb)
	ld	de, (e_xa)
	or	a, a
	sbc	hl, de			; dx (не 0: концы по разные стороны края)
	ld	a, (lx_s)
	xor	a, h
	ld	(lx_s), a
	bit	7, h
	jr	z, 3$
	ex	de, hl
	ld	hl, #0
	or	a, a
	sbc	hl, de
3$:	ld	b, h
	ld	c, l
	pop	de
	pop	hl
	call	div32			; DE = частное
	ld	a, (lx_s)
	rlca
	jr	nc, 4$
	ld	hl, #0
	or	a, a
	sbc	hl, de
	ex	de, hl
4$:	ld	hl, (e_ya)
	add	hl, de
	ret

;; DE = HL:DE / BC (без знака; HL < BC < 32768 — частное в 16 битах), HL — остаток
div32:
	ld	a, #16
1$:	ex	de, hl
	add	hl, hl
	ex	de, hl
	adc	hl, hl
	or	a, a
	sbc	hl, bc
	jr	nc, 2$
	add	hl, bc
	jr	3$
2$:	inc	e
3$:	dec	a
	jr	nz, 1$
	ret

;; Шаг u по строкам: s = 128 · dx / dy (8.8 пар на строку, округление, |s| <= 32767);
;; dx, dy — Q2, dy > 0: s = (|dx| · 65536/dy + 256) >> 9, 65536 / dy — по таблице (dy >= 512 —
;; dy делится на 2, сдвиг результата на 1 больше). |dx| < 256 — два умножения 8 x 8.
eslope:
	ld	hl, (e_xb)
	ld	de, (e_xa)
	or	a, a
	sbc	hl, de
	xor	a, a
	bit	7, h
	jr	z, 1$
	ex	de, hl
	ld	hl, #0
	or	a, a
	sbc	hl, de
	inc	a
1$:	ld	(e_neg), a
	push	hl			; |dx| (<= 1024)
	ld	hl, (e_yb)
	ld	de, (e_ya)
	or	a, a
	sbc	hl, de
	ld	b, #1			; сдвиг результата
2$:	ld	a, h
	cp	a, #2			; dy >= 512 — пополам (таблица на 512)
	jr	c, 3$
	srl	h
	rr	l
	inc	b
	jr	2$
3$:	add	hl, hl
	ld	de, #_recip_tab
	add	hl, de
	ld	e, (hl)
	inc	hl
	ld	d, (hl)			; 65536 / dy (dy = 1: 0)
	pop	hl
	ld	a, d
	or	a, e
	jr	z, 7$
	ld	a, h
	or	a, a
	jr	nz, 5$
	push	bc			; |dx| < 256: (|dx| · r) >> 8 = |dx| · rH + (|dx| · rL) >> 8
	ld	b, l
	ld	a, d
	ld	c, e
	push	af
	call	mul8e			; |dx| · rL
	pop	af
	ld	c, a
	push	de
	call	mul8e			; |dx| · rH
	pop	hl
	ld	l, h
	ld	h, #0
	add	hl, de
	pop	bc
	inc	hl			; + 256 до сдвига на 8
4$:	srl	h
	rr	l
	djnz	4$
	jr	9$
5$:	push	bc			; |dx| >= 256: 32 бита
	call	mulu16			; HL:DE = |dx| · 65536/dy
	pop	bc
	ld	a, d			; + 256, >> 8: H:L:D
	add	a, #1
	ld	d, a
	jr	nc, 6$
	inc	hl
6$:	srl	h
	rr	l
	rr	d
	djnz	6$
	ld	a, h			; > 32767 — предел
	or	a, a
	jr	nz, 8$
	bit	7, l
	jr	nz, 8$
	ld	h, l
	ld	l, d
	jr	9$
7$:	ld	a, h			; dy = 1: s = 128 |dx|, если влезает
	or	a, a
	jr	nz, 8$
	ld	h, l
	ld	l, #0
	srl	h
	rr	l
	jr	9$
8$:	ld	hl, #0x7FFF
9$:	ld	a, (e_neg)
	or	a, a
	jr	z, 10$
	ex	de, hl
	ld	hl, #0
	or	a, a
	sbc	hl, de
10$:	ld	(e_s), hl
	ret

;; ================================================================ строки

;; Win3 — страница рёбер. Активные рёбра — в слотах (поля — отдельными страницами по
;; номеру слота: u, шаг, последняя строка, текстуры); порядок по x — массив слотов EP_ORD и
;; параллельный массив старших байтов u EP_OUH (граница в парах). Строки 0..199: новые
;; рёбра корзины -> в слоты и на место по (u, шаг); починка порядка; вывод строки; шаг рёбер,
;; удаление законченных; сортировка вставками с допуском (редко). Строки без рёбер —
;; текстура левого края прошлой строки (или событие края); до первой строки с рёбрами —
;; ждут и заливаются снизу вверх; если рёбер нет совсем — _gl_gtex (сетка 5°).
_gl_rows::
	push	ix
	push	iy
	ld	hl, #EP_FREE		; свободные слоты 0..AEL_MAX-1
	xor	a, a
1$:	ld	(hl), a
	inc	l
	inc	a
	cp	a, #AEL_MAX
	jr	c, 1$
	ld	(rw_nf), a
	xor	a, a
	ld	(rw_n), a
	ld	(rw_y), a
	ld	a, #0xFE
	ld	(rw_lt), a
	ld	a, #0xFF
	ld	(rw_pend), a
	xor	a, a
	ld	(rw_dirty), a
	ld	ix, #EP_ROW
	ld	hl, #EP_BUCKET
	ld	(rw_bk), hl
	ld	bc, #0x28AF		; DMANum = 0 на весь вывод
	xor	a, a
	out	(c), a
rw_row:
	call	ael_ins
	call	ael_fix
	call	ai_redo
	call	rw_emit
	call	ael_step
	ld	de, #6
	add	ix, de
	ld	a, (rw_y)
	inc	a
	ld	(rw_y), a
	cp	a, #GH
	jp	nz, rw_row
	ld	a, (rw_pend)		; рёбер не было: сетка
	cp	a, #0xFF
	jr	z, 4$
	ld	c, a
	ld	b, #0
	ld	ix, #EP_ROW
	add	ix, bc
	add	ix, bc
	add	ix, bc
	add	ix, bc
	add	ix, bc
	add	ix, bc
	ld	a, #GH
	sub	a, c
	ld	b, a
2$:	push	bc
	ld	a, 0 (ix)
	ld	(em_start), a
	ld	c, a
	ld	a, 1 (ix)
	cp	a, c
	jr	c, 3$
	xor	a, a
	ld	(em_first), a
	ld	a, (_gl_gtex)
	ld	(em_tex), a
	ld	a, #GH
	sub	a, b
	ld	(em_row), a
	ld	a, 1 (ix)
	call	rw_flush
3$:	ld	de, #6
	add	ix, de
	pop	bc
	djnz	2$
4$:	pop	iy
	pop	ix
	ret

;; Полосы строк без рёбер (globe.c bands): EP_BT — разности покрытия (e_store) -> #FD у строки
;; диска без рёбер, #FE у остальных; полосы подряд идущих #FD -> _gl_bl (y0, y1; до 16),
;; число — _gl_nb. Win3 = страница рёбер.
_gl_bands::
	xor	a, a
	ld	(_gl_nb), a
	ld	(gb_in), a
	ld	hl, #EP_BT
	ld	de, #EP_ROW
	ld	bc, #(GH << 8)		; B — строк, C — покрытие
1$:	ld	a, (hl)
	add	a, c
	ld	c, a
	jr	nz, 3$			; есть рёбра
	ld	a, (de)			; pl <= pr — строка диска
	inc	de
	ex	de, hl
	cp	a, (hl)
	ex	de, hl
	dec	de
	jr	z, 2$
	jr	nc, 3$
2$:	ld	(hl), #0xFD
	ld	a, (gb_in)
	or	a, a
	jr	nz, 4$
	inc	a			; начало полосы
	ld	(gb_in), a
	ld	a, l
	ld	(gb_y0), a
	jr	4$
3$:	ld	(hl), #0xFE
	ld	a, (gb_in)
	or	a, a
	call	nz, gb_end
4$:	inc	l
	push	hl
	ld	hl, #6
	add	hl, de
	ex	de, hl
	pop	hl
	djnz	1$
	ld	a, (gb_in)
	or	a, a
	ret	z
;; Полоса gb_y0 .. L - 1 -> в список (сохраняет BC, DE, HL)
gb_end:
	xor	a, a
	ld	(gb_in), a
	ld	a, (_gl_nb)
	cp	a, #16
	ret	nc
	push	hl
	push	de
	ld	d, l
	dec	d			; y1
	inc	a
	ld	(_gl_nb), a
	dec	a
	add	a, a
	ld	e, a
	push	de
	ld	d, #0
	ld	hl, #_gl_bl
	add	hl, de
	pop	de
	ld	a, (gb_y0)
	ld	(hl), a
	inc	hl
	ld	(hl), d
	pop	de
	pop	hl
	ret

;; Рёбра корзины строки -> в свободные слоты и в порядок: место — после всех с (u, шаг)
;; <= нового
ael_ins:
	xor	a, a
	ld	(rw_ni), a
	ld	hl, (rw_bk)
	ld	e, (hl)
	inc	hl
	ld	d, (hl)
	inc	hl
	ld	(rw_bk), hl
1$:	ld	a, d
	or	a, e
	ret	z
	ld	a, (rw_nf)
	or	a, a
	ret	z			; слотов нет
	dec	a
	ld	(rw_nf), a
	ld	l, a
	ld	h, #>EP_FREE
	ld	c, (hl)			; C — слот
	ld	a, c
	ld	(ai_sl), a
	ex	de, hl			; запись -> поля слота
	ld	b, #>EP_SUL
	ld	a, (hl)
	ld	(bc), a
	inc	hl
	ld	b, #>EP_SUH
	ld	a, (hl)
	ld	(bc), a
	ld	(ai_uh), a
	inc	hl
	ld	b, #>EP_SSL
	ld	a, (hl)
	ld	(bc), a
	inc	hl
	ld	b, #>EP_SSH
	ld	a, (hl)
	ld	(bc), a
	inc	hl
	ld	b, #>EP_SLAST
	ld	a, (hl)
	ld	(bc), a
	inc	hl
	ld	b, #>EP_STL
	ld	a, (hl)
	ld	(bc), a
	inc	hl
	ld	b, #>EP_STR
	ld	a, (hl)
	ld	(bc), a
	inc	hl
	inc	hl
	ld	e, (hl)			; следующая в корзине (+8)
	inc	hl
	ld	d, (hl)
	push	de
	ld	a, #1			; в строке есть вставки — полная починка
	ld	(rw_dirty), a
	ld	a, (ai_uh)
	ld	d, a			; D — старший байт u нового (ai_cmp сохраняет DE)
	ld	a, (rw_n)		; место: OUH[k] > нового; равные — по (u, шаг)
	ld	b, a
	ld	hl, #EP_OUH
	or	a, a
	jr	z, 5$
2$:	ld	a, d
	cp	a, (hl)
	jr	c, 5$			; OUH[k] > нового
	jr	nz, 3$			; меньше — дальше
	call	ai_cmp
	jr	c, 5$
3$:	inc	l
	djnz	2$
5$:	ld	a, l			; k
	ld	(ai_k), a
	ld	a, (_gl_limb)		; зумы 0–1: (слот, место) — в список новых (место по
	or	a, a			; текстурам — ai_redo, когда в строке уже все новые рёбра);
	jr	z, 6$			; места прежних новых на k и правее — +1
	ld	a, (ai_k)
	ld	c, a
	ld	a, (rw_ni)
	or	a, a
	jr	z, 52$
	ld	b, a
	ld	hl, #EP_INS + 1
51$:	ld	a, (hl)
	cp	a, c
	jr	c, 53$
	inc	(hl)
53$:	inc	l
	inc	l
	djnz	51$
52$:	ld	a, (rw_ni)
	add	a, a
	ld	l, a
	ld	h, #>EP_INS
	ld	a, (ai_sl)
	ld	(hl), a
	inc	l
	ld	(hl), c
	ld	hl, #rw_ni
	inc	(hl)
6$:	call	ai_put
	pop	de
	jp	1$

;; Вставить слот ai_sl (старший байт u — ai_uh) на место ai_k: сдвиг [k, n) вправо в ORD и OUH
ai_put:
	ld	a, (ai_k)
	ld	l, a
	ld	a, (rw_n)
	sub	a, l
	jr	z, 1$
	ld	c, a
	ld	b, #0
	push	bc
	ld	a, (rw_n)
	ld	e, a
	ld	d, #>EP_OUH
	ld	l, e
	dec	l
	ld	h, d
	lddr
	pop	bc
	ld	a, (rw_n)
	ld	e, a
	ld	d, #>EP_ORD
	ld	l, e
	dec	l
	ld	h, d
	lddr
1$:	ld	a, (ai_k)
	ld	l, a
	ld	h, #>EP_ORD
	ld	a, (ai_sl)
	ld	(hl), a
	inc	h
	ld	a, (ai_uh)
	ld	(hl), a
	ld	hl, #rw_n
	inc	(hl)
	ret

;; Зумы 0–1, после вставки всех новых рёбер строки и починки пар: новое ребро, всё ещё
;; несогласованное с соседями по текстурам, — на лучшее место в своей кучке (ai_fix2; соседи —
;; уже со всеми новыми); если что-то сдвинулось — починка ещё раз. Порядок — как в корзине.
ai_redo:
	ld	a, (rw_ni)
	or	a, a
	ret	z
	ld	b, a
	xor	a, a
	ld	(ar_mv), a
	ld	hl, #EP_INS
1$:	push	bc
	push	hl
	ld	c, (hl)			; слот
	inc	l
	ld	e, (hl)			; место при вставке
	ld	a, c
	ld	(ai_sl), a
	call	ar_find
	cp	a, #0xFF
	jr	z, 9$
	ld	(ai_k), a
	ld	(ar_k0), a
	call	ar_ok
	jr	z, 9$			; на месте согласовано
	ld	a, (ai_k)
	ld	l, a
	ld	h, #>EP_ORD
	inc	h
	ld	a, (hl)
	ld	(ai_uh), a
	dec	h
	ld	a, (rw_n)		; убрать: сдвиг [k + 1, n) влево
	dec	a
	ld	(rw_n), a
	sub	a, l
	jr	z, 4$
	ld	c, a
	ld	b, #0
	push	bc
	ld	e, l
	ld	d, h
	inc	l
	ldir
	pop	bc
	ld	a, (ai_k)
	ld	e, a
	ld	d, #>EP_OUH
	ld	l, a
	inc	l
	ld	h, d
	ldir
4$:	call	ai_fix2
	ld	a, (ai_k)
	ld	hl, #ar_k0
	cp	a, (hl)
	jr	z, 5$
	ld	a, #1
	ld	(ar_mv), a
5$:	call	ai_put
9$:	pop	hl
	pop	bc
	inc	l
	inc	l
	djnz	1$
	ld	a, (ar_mv)
	or	a, a
	ret	z
	ld	a, #1
	ld	(rw_dirty), a
	jp	ael_fix

;; Место слота C в ORD: запомненное E (починка пар могла сдвинуть на 1) — E, E + 1, E - 1,
;; иначе поиск по всем; A = место или #FF
ar_find:
	ld	a, (rw_n)
	ld	d, a
	ld	h, #>EP_ORD
	ld	l, e
	ld	a, e
	cp	a, d
	jr	nc, 2$
	ld	a, (hl)
	cp	a, c
	ld	a, l
	ret	z
	inc	l
	ld	a, l
	cp	a, d
	jr	nc, 2$
	ld	a, (hl)
	cp	a, c
	ld	a, l
	ret	z
2$:	ld	a, e
	or	a, a
	jr	z, 3$
	dec	a
	cp	a, d
	jr	nc, 3$
	ld	l, a
	ld	a, (hl)
	cp	a, c
	ld	a, l
	ret	z
3$:	ld	l, #0
	ld	a, d
	or	a, a
	jr	z, 5$
	ld	b, d
4$:	ld	a, (hl)
	cp	a, c
	ld	a, l
	ret	z
	inc	l
	djnz	4$
5$:	ld	a, #0xFF
	ret

;; Z — слот ai_sl на месте ai_k согласован с соседями: слева «справа» левого соседа (у первого —
;; текстура края строки, #FE — любая) == его «слева», справа — нет соседа или «слева» соседа ==
;; его «справа» (как f2_score на том же месте без него)
ar_ok:
	ld	a, (ai_sl)
	ld	e, a
	ld	a, (ai_k)
	or	a, a
	jr	nz, 1$
	ld	a, (rw_y)
	ld	l, a
	ld	h, #>EP_EVB
	ld	a, (hl)
	cp	a, #0xFE
	jr	nz, 2$
	ld	a, (rw_lt)
	cp	a, #0xFE
	jr	z, 3$
	jr	2$
1$:	dec	a
	ld	l, a
	ld	h, #>EP_ORD
	ld	l, (hl)
	ld	h, #>EP_STR
	ld	a, (hl)
2$:	ld	l, e
	ld	h, #>EP_STL
	cp	a, (hl)
	ret	nz
3$:	ld	a, (ai_k)
	inc	a
	ld	hl, #rw_n
	cp	a, (hl)
	ret	z
	ld	l, a
	ld	h, #>EP_ORD
	ld	l, (hl)
	ld	h, #>EP_STL
	ld	a, (hl)
	ld	l, e
	ld	h, #>EP_STR
	cp	a, (hl)
	ret

;; Зумы 0–1: место ai_k не согласовано по текстурам (слева — «справа» левого соседа или
;; текстура края строки; справа — «слева» правого) — лучшее место среди соседей ближе
;; полупары (|u - u нового| < 128): оценка 2 · слева + справа, при равной — ближе к ai_k.
ai_fix2:
	ld	a, (ai_k)
	call	f2_score
	cp	a, #3
	ret	z			; согласовано
	ld	(f2_bs), a
	ld	a, (ai_k)
	ld	(f2_bk), a
	ld	c, a			; C = lo: влево, пока сосед близко
1$:	ld	a, c
	or	a, a
	jr	z, 2$
	dec	a
	call	f2_near
	jr	nc, 2$
	dec	c
	jr	1$
2$:	ld	a, (ai_k)		; hi: вправо, пока сосед близко
	ld	b, a
3$:	ld	a, (rw_n)
	cp	a, b
	jr	z, 4$
	ld	a, b
	call	f2_near
	jr	nc, 4$
	inc	b
	jr	3$
4$:	ld	a, b
	ld	(f2_hi), a
5$:	ld	a, c			; места lo..hi
	push	bc
	call	f2_score
	pop	bc
	ld	e, a
	ld	a, (f2_bs)
	cp	a, e
	jr	c, 6$			; лучше
	jr	nz, 7$
	ld	a, (ai_k)		; равная оценка: ближе к ai_k
	sub	a, c
	jr	nc, 51$
	neg
51$:	ld	d, a
	ld	a, (ai_k)
	ld	hl, #f2_bk
	sub	a, (hl)
	jr	nc, 52$
	neg
52$:	cp	a, d
	jr	c, 7$
	jr	z, 7$
6$:	ld	a, e
	ld	(f2_bs), a
	ld	a, c
	ld	(f2_bk), a
7$:	ld	a, (f2_hi)
	cp	a, c
	jr	z, 8$
	inc	c
	jr	5$
8$:	ld	a, (f2_bk)
	ld	(ai_k), a
	ret

;; C — сосед на месте A (ORD) ближе полупары к новому: |u - u нового| < 128
f2_near:
	push	bc
	ld	l, a
	ld	h, #>EP_ORD
	ld	l, (hl)			; слот соседа
	ld	a, (ai_sl)
	ld	e, a
	ld	h, #>EP_SUL
	ld	d, h
	ld	a, (de)
	sub	a, (hl)
	ld	c, a
	ld	h, #>EP_SUH
	ld	d, h
	ld	a, (de)
	sbc	a, (hl)			; A:C = u нового - u соседа
	jr	z, 1$
	inc	a
	jr	nz, 2$			; |разность| >= 256
	ld	a, c			; -256 < разность < 0: близко, если >= -127 (C >= #81)
	cp	a, #0x81
	ccf
	jr	3$
1$:	ld	a, c
	cp	a, #0x80		; 0 <= разность < 128
	jr	3$
2$:	or	a, a			; далеко: C = 0
3$:	pop	bc
	ret

;; A = оценка места j = A: 2 — слева согласовано, +1 — справа (без изменения BC, IX)
f2_score:
	push	bc
	ld	b, a
	ld	a, (ai_sl)
	ld	e, a
	ld	c, #0
	ld	a, b			; справа: j == n или STL[ORD[j]] == tr нового
	ld	hl, #rw_n
	cp	a, (hl)
	jr	z, 1$
	ld	l, b
	ld	h, #>EP_ORD
	ld	l, (hl)
	ld	h, #>EP_STL
	ld	a, (hl)
	ld	d, #>EP_STR
	ex	de, hl
	cp	a, (hl)
	ex	de, hl
	jr	nz, 2$
1$:	inc	c
2$:	ld	a, b			; слева: STR[ORD[j - 1]] или текстура края == tl нового
	or	a, a
	jr	nz, 3$
	ld	a, (rw_y)
	ld	l, a
	ld	h, #>EP_EVB
	ld	a, (hl)
	cp	a, #0xFE
	jr	nz, 4$
	ld	a, (rw_lt)
	cp	a, #0xFE
	jr	z, 5$			; края не знаем — согласовано
	jr	4$
3$:	dec	a
	ld	l, a
	ld	h, #>EP_ORD
	ld	l, (hl)
	ld	h, #>EP_STR
	ld	a, (hl)
4$:	ld	l, e
	ld	h, #>EP_STL
	cp	a, (hl)
	jr	nz, 6$
5$:	inc	c
	inc	c
6$:	ld	a, c
	pop	bc
	ret

;; Старшие байты u равны: C — ребро на месте HL (OUH) больше нового по (u, шаг со знаком).
;; Сохраняет BC, DE, HL.
ai_cmp:
	push	hl
	push	bc
	push	de
	dec	h
	ld	l, (hl)			; L — слот k
	ld	a, (ai_sl)
	ld	e, a			; E — новый
	ld	h, #>EP_SUL
	ld	d, h
	ld	a, (de)
	cp	a, (hl)			; u новый - u[k]
	jr	nz, 9$
	ld	h, #>EP_SSH		; u равны: шаг[k] > нового — место здесь
	ld	d, h
	ld	a, (de)
	xor	a, (hl)
	jp	p, 2$
	bit	7, (hl)			; разных знаков: больше — неотрицательный
	jr	nz, 1$
	scf
	jr	9$
1$:	or	a, a
	jr	9$
2$:	ld	a, (de)			; одного знака — как без знака
	cp	a, (hl)
	jr	nz, 9$
	ld	h, #>EP_SSL
	ld	d, h
	ld	a, (de)
	cp	a, (hl)
9$:	pop	de
	pop	bc
	pop	hl
	ret

;; Починка порядка всей строки: пары соседей с |u| < 256 (сначала по старшим байтам —
;; разница не больше 1) — в fx_pair. Только если в строке были вставки, прошлая сортировка
;; сдвигала элементы или в строке событие левого края (текстура левее первого ребра
;; сменилась): иначе порядок — как в прошлой строке, уже согласованный.
ael_fix:
	ld	a, (rw_dirty)
	or	a, a
	jr	nz, 0$
	ld	a, (rw_y)
	ld	l, a
	ld	h, #>EP_EVB
	ld	a, (hl)
	cp	a, #0xFE
	ret	z			; порядок не сдвигался, событий края нет
0$:	xor	a, a
	ld	(rw_dirty), a
	ld	(fx_def), a
	ld	a, (rw_n)
	cp	a, #2
	ret	c
	dec	a
	ld	b, a
	ld	hl, #EP_OUH
	ld	a, (rw_y)		; левая текстура неизвестна (первая строка с рёбрами после
	ld	e, a			; ждущих): у первой пары нет левого контекста — её последней
	ld	d, #>EP_EVB
	ld	a, (de)
	cp	a, #0xFE
	jr	nz, 1$
	ld	a, (rw_lt)
	cp	a, #0xFE
	jr	nz, 1$
	ld	a, #1
	ld	(fx_def), a
	inc	l
	dec	b
	jr	z, 4$
1$:	ld	c, (hl)
	inc	l
	ld	a, (hl)
	sub	a, c
	inc	a
	cp	a, #3
	jr	c, 3$
2$:	djnz	1$
4$:	ld	a, (fx_def)		; отложенная первая пара
	or	a, a
	ret	z
	ld	hl, #EP_OUH
	ld	c, (hl)
	inc	l
	ld	a, (hl)
	sub	a, c
	inc	a
	cp	a, #3
	ret	nc
	ld	l, #0
	jp	fx_pair
3$:	push	bc
	push	hl
	dec	l
	call	fx_pair
	pop	hl
	pop	bc
	jr	2$

;; Пара k = L, k + 1 (L — место в OUH): p, q меняются местами, если так текстуры
;; согласованы (левее — tl, между — одна, правее — tl следующего) а сейчас нет. Левее первого
;; — событие края строки или текстура края прошлой строки; #FE — любая.
fx_pair:
	ld	a, l
	ld	(fx_k), a
	dec	h
	ld	e, (hl)			; p
	inc	l
	ld	d, (hl)			; q
	ld	h, #>EP_STL		; переставленные несогласованы между собой (q.tr != p.tl) —
	ld	l, e			; не менять (почти все пары)
	ld	c, (hl)
	ld	h, #>EP_STR
	ld	l, d
	ld	a, (hl)
	cp	a, c
	ret	nz
	ld	h, #>EP_SUL		; |u[q] - u[p]| < 256
	ld	l, d
	ld	a, (hl)
	ld	l, e
	sub	a, (hl)
	ld	c, a
	ld	h, #>EP_SUH
	ld	l, d
	ld	a, (hl)
	ld	l, e
	sbc	a, (hl)
	or	a, a
	jr	z, 1$
	inc	a
	ret	nz
	ld	a, c
	or	a, a
	ret	z
1$:	ld	h, #>EP_STL
	ld	l, e
	ld	a, (hl)
	ld	(fx_pl), a
	ld	l, d
	ld	a, (hl)
	ld	(fx_ql), a
	ld	h, #>EP_STR
	ld	l, e
	ld	a, (hl)
	ld	(fx_pr), a
	ld	l, d
	ld	a, (hl)
	ld	(fx_qr), a
	ld	a, (fx_k)		; правее: tl ребра k + 2 (если есть)
	add	a, #2
	ld	c, a
	ld	a, (rw_n)
	cp	a, c
	ld	a, #0xFE
	jr	z, 2$
	jr	c, 2$
	ld	l, c
	ld	h, #>EP_ORD
	ld	l, (hl)
	ld	h, #>EP_STL
	ld	a, (hl)
2$:	ld	(fx_r), a
	ld	a, (fx_k)		; левее: tr ребра k - 1, у первого — край
	or	a, a
	jr	nz, 3$
	ld	a, (rw_y)
	ld	l, a
	ld	h, #>EP_EVB
	ld	a, (hl)
	cp	a, #0xFE
	jr	nz, 4$
	ld	a, (rw_lt)
	jr	4$
3$:	dec	a
	ld	l, a
	ld	h, #>EP_ORD
	ld	l, (hl)
	ld	h, #>EP_STR
	ld	a, (hl)
4$:	ld	(fx_l), a
	ld	c, a			; сейчас: L ~ p.tl, p.tr = q.tl, q.tr ~ R
	ld	a, (fx_pl)
	call	fx_eq
	jr	nz, 5$
	ld	a, (fx_pr)
	ld	c, a
	ld	a, (fx_ql)
	cp	a, c
	jr	nz, 5$
	ld	a, (fx_r)
	ld	c, a
	ld	a, (fx_qr)
	call	fx_eq
	ret	z			; согласованы — не трогать
5$:	ld	a, (fx_l)		; переставленные: L ~ q.tl, q.tr = p.tl, p.tr ~ R
	ld	c, a
	ld	a, (fx_ql)
	call	fx_eq
	ret	nz
	ld	a, (fx_qr)
	ld	c, a
	ld	a, (fx_pl)
	cp	a, c
	ret	nz
	ld	a, (fx_r)
	ld	c, a
	ld	a, (fx_pr)
	call	fx_eq
	ret	nz
	ld	a, (fx_k)		; обмен в ORD и OUH
	ld	l, a
	ld	h, #>EP_ORD
	ld	a, (hl)
	inc	l
	ld	c, (hl)
	ld	(hl), a
	dec	l
	ld	(hl), c
	inc	h
	ld	a, (hl)
	inc	l
	ld	c, (hl)
	ld	(hl), a
	dec	l
	ld	(hl), c
	ret

;; Z — A и C совпадают или одна из них #FE (любая)
fx_eq:
	cp	a, c
	ret	z
	cp	a, #0xFE
	ret	z
	ld	a, c
	cp	a, #0xFE
	ret

;; Вывод строки (IX — запись строки): от pl до pr отрезками одной текстуры; текстура левее
;; первого ребра — его «слева», за ребром — его «справа». Ребро не правее начала отрезка
;; только меняет текстуру отрезка. Адреса DMA (SAL = DAL = 2 pl, SAX, DAH, DAX) — один раз в
;; начале строки: отрезки идут подряд, у каждого — только блок источника, длина, запуск.
;; В цикле: HL — место в ORD, B — рёбер осталось, C — текстура отрезка, D — его начало,
;; E — «справа» ребра; во втором наборе C' = #AF (порты DMA), D' — блок строки, E' = pr + 1.
rw_emit:
	ld	c, 0 (ix)
	ld	a, 1 (ix)
	ld	(em_pr), a
	cp	a, c
	ret	c			; строки диска нет
	ld	a, c
	ld	(em_start), a
	xor	a, a
	ld	(em_first), a
	ld	a, (rw_n)
	or	a, a
	jp	z, rw_band
	ld	a, (EP_ORD)		; текстура края — слева от первого ребра
	ld	l, a
	ld	h, #>EP_STL
	ld	a, (hl)
	ld	(rw_lt), a
	ld	a, (rw_pend)
	cp	a, #0xFF
	call	nz, rw_pfill
	ld	a, (rw_y)		; строка с тенью — свой вывод
	ld	l, a
	ld	h, #>EP_SHF
	ld	a, (hl)
	or	a, a
	jp	nz, rw_emit_s
	ld	bc, #0x27AF		; адреса DMA строки — после конца прошлого отрезка
1$:	in	a, (c)
	jp	m, 1$
	ld	a, 0 (ix)
	add	a, a
	ld	b, #0x1A
	out	(c), a			; SAL
	ld	b, #0x1D
	out	(c), a			; DAL
	ld	a, 5 (ix)
	ld	b, #0x1C
	out	(c), a			; SAX
	ld	a, 2 (ix)
	ld	b, #0x1E
	out	(c), a			; DAH
	ld	a, 3 (ix)
	inc	b
	out	(c), a			; DAX
	exx
	ld	c, #0xAF
	ld	d, 4 (ix)		; блок строки
	ld	a, 1 (ix)
	inc	a
	ld	e, a			; pr + 1
	exx
	ld	a, (rw_lt)
	ld	c, a			; текстура отрезка
	ld	d, 0 (ix)		; начало — pl
	ld	a, (rw_n)
	ld	b, a
	ld	hl, #EP_ORD
2$:	ld	e, (hl)			; слот
	inc	h
	ld	a, (hl)			; граница (OUH)
	dec	h
	inc	l
	push	hl
	ld	l, e
	ld	h, #>EP_STR
	ld	e, (hl)			; E — справа
	pop	hl
	cp	a, d
	jr	c, 6$			; не правее начала: только текстура
	jr	z, 6$
	exx
	cp	a, e
	exx
	jr	nc, 8$			; за правым краем — дальше не смотреть
	ex	af, af'
	ld	a, e
	cp	a, c
	jr	z, 7$			; текстура не меняется
	ex	af, af'			; A — граница: отрезок [D, A - 1] текстуры C
	push	hl
	ld	h, a
	sub	a, d
	dec	a
	ld	l, a			; слов - 1
	ld	a, c
	exx
	add	a, d			; блок строки + текстура (океан — 13)
	ld	b, #0x27
3$:	in	h, (c)			; ждать конца прошлого
	jp	m, 3$
	ld	b, #0x1B
	out	(c), a			; SAH
	exx
	ld	a, l
	exx
	ld	b, #0x26
	out	(c), a			; DMALen
	inc	b
	ld	a, #1
	out	(c), a			; DMACtrl: RAM -> RAM, старт
	exx
	ld	d, h			; новое начало — граница
	ld	c, e			; текстура — справа от ребра
	pop	hl
	djnz	2$
	jr	8$
6$:	ld	c, e
7$:	djnz	2$
8$:	exx				; последний отрезок [D, pr]
	ld	a, e
	exx
	dec	a
	sub	a, d
	ret	c
	ld	l, a			; слов - 1
	ld	a, c
	exx
	add	a, d
	ld	b, #0x27
9$:	in	h, (c)
	jp	m, 9$
	ld	b, #0x1B
	out	(c), a			; SAH
	exx
	ld	a, l
	exx
	ld	b, #0x26
	out	(c), a			; DMALen
	inc	b
	ld	a, #1
	out	(c), a			; DMACtrl
	exx
	ret

;; Строка с тенью (EP_SHF): как rw_emit, отрезок — em_run_s (суша — BLT2 узора на подкладку
;; суши L, океан — копия цвета O из x + 256 той же строки заднего буфера; globe_sh.c)
rw_emit_s:
	ld	bc, #0x27AF		; адреса DMA строки — после конца прошлого отрезка
1$:	in	a, (c)
	jp	m, 1$
	ld	a, 0 (ix)
	add	a, a
	ld	b, #0x1A
	out	(c), a			; SAL
	ld	b, #0x1D
	out	(c), a			; DAL
	ld	a, 5 (ix)
	ld	b, #0x1C
	out	(c), a			; SAX
	ld	a, 2 (ix)
	ld	b, #0x1E
	out	(c), a			; DAH
	ld	a, 3 (ix)
	inc	b
	out	(c), a			; DAX
	exx
	ld	c, #0xAF
	ld	d, 4 (ix)		; блок строки
	ld	a, 1 (ix)
	inc	a
	ld	e, a			; pr + 1
	exx
	ld	a, (rw_lt)
	ld	c, a			; текстура отрезка
	ld	d, 0 (ix)		; начало — pl
	ld	a, (rw_n)
	ld	b, a
	ld	hl, #EP_ORD
2$:	ld	e, (hl)			; слот
	inc	h
	ld	a, (hl)			; граница (OUH)
	dec	h
	inc	l
	push	hl
	ld	l, e
	ld	h, #>EP_STR
	ld	e, (hl)			; E — справа
	pop	hl
	cp	a, d
	jr	c, 6$			; не правее начала: только текстура
	jr	z, 6$
	exx
	cp	a, e
	exx
	jr	nc, 8$			; за правым краем — дальше не смотреть
	ex	af, af'
	ld	a, e
	cp	a, c
	jr	z, 7$			; текстура не меняется
	ex	af, af'			; A — граница: отрезок [D, A - 1] текстуры C
	push	hl
	ld	h, a
	sub	a, d
	dec	a
	ld	l, a			; слов - 1
	ld	a, c
	call	em_run_s
	ld	d, h			; новое начало — граница
	ld	c, e			; текстура — справа от ребра
	pop	hl
	djnz	2$
	jr	8$
6$:	ld	c, e
7$:	djnz	2$
8$:	exx				; последний отрезок [D, pr]
	ld	a, e
	exx
	dec	a
	sub	a, d
	ret	c
	ld	l, a			; слов - 1
	ld	a, c
	jp	em_run_s

;; Отрезок строки с тенью: A — текстура, L — слов − 1 (основной набор); второй набор — как в
;; rw_emit (C' = #AF, D' — блок строки). Младший байт адреса источника продолжается: у узора и
;; у x + 256 строки он тот же, что у приёмника.
em_run_s:
	exx
	cp	a, #13
	jr	z, 2$
	add	a, d
	ld	b, #0x27
1$:	in	h, (c)			; ждать конца прошлого
	jp	m, 1$
	ld	b, #0x1B
	out	(c), a			; SAH — блок узора
	ld	a, 5 (ix)
	ld	b, #0x1C
	out	(c), a			; SAX — страница узоров строки
	ld	h, #0x46		; BLT2 по полубайтам с насыщением: узор + L = getLandShadow
	jr	4$
2$:	ld	b, #0x27
3$:	in	h, (c)
	jp	m, 3$
	ld	a, 2 (ix)
	inc	a
	ld	b, #0x1B
	out	(c), a			; SAH — x + 256 строки заднего буфера (цвет океана)
	ld	a, 3 (ix)
	ld	b, #0x1C
	out	(c), a			; SAX — страница строки
	ld	h, #1			; копия
4$:	exx
	ld	a, l
	exx
	ld	b, #0x26
	out	(c), a			; DMALen
	inc	b
	out	(c), h			; DMACtrl
	exx
	ret

;; Строка без рёбер: событие края строки, иначе текстура края прошлой строки; неизвестна —
;; строка ждёт первой строки с рёбрами
rw_band:
	ld	a, (rw_y)
	ld	l, a
	ld	h, #>EP_EVB
	ld	a, (hl)
	cp	a, #0xFE
	jr	z, 1$
	ld	(rw_lt), a
1$:	ld	h, #>EP_BT		; текстура полосы по сетке 5° (globe.c) — надёжнее событий
	ld	a, (hl)
	cp	a, #0xFE
	jr	z, 11$
	ld	(rw_lt), a
11$:	ld	a, (rw_lt)
	cp	a, #0xFE
	jr	nz, 2$
	ld	a, (rw_pend)
	cp	a, #0xFF
	ret	nz
	ld	a, (rw_y)
	ld	(rw_pend), a
	ret
2$:	ld	(em_tex), a
	ld	a, (rw_y)
	ld	(em_row), a
	ld	a, (em_pr)
	jp	rw_flush

;; Ждущие строки rw_pend..y-1 снизу вверх: текстура края этой строки, выше события —
;; текстура над ним (EVA)
rw_pfill:
	push	ix
	ld	a, (rw_lt)
	ld	(pf_t), a
	ld	a, (rw_y)
	ld	c, a			; C = r + 1
1$:	push	bc
	ld	l, c
	ld	h, #>EP_EVA
	ld	a, (hl)
	cp	a, #0xFE
	jr	z, 2$
	ld	(pf_t), a
2$:	ld	de, #-6
	add	ix, de
	ld	a, 0 (ix)
	ld	(em_start), a
	ld	b, a
	ld	a, 1 (ix)
	cp	a, b
	jr	c, 3$
	xor	a, a
	ld	(em_first), a
	ld	a, (pf_t)
	ld	(em_tex), a
	ld	a, c
	dec	a
	ld	(em_row), a
	ld	a, 1 (ix)
	call	rw_flush
3$:	pop	bc
	dec	c
	ld	a, (rw_pend)
	cp	a, c
	jr	nz, 1$
	ld	a, #0xFF
	ld	(rw_pend), a
	pop	ix
	ld	a, 0 (ix)		; строка y — заново
	ld	(em_start), a
	xor	a, a
	ld	(em_first), a
	ld	a, (rw_lt)
	ld	(em_tex), a
	ret

;; Отрезок пар [em_start, A] текстуры em_tex -> DMA. Первый в строке — все регистры,
;; следующие — блок источника, длина, запуск (адреса продолжаются с конца прошлого).
rw_flush:
	ld	hl, #em_start
	sub	a, (hl)
	ret	c			; пусто
	ld	e, a			; слов - 1
	ld	a, (em_tex)
	add	a, 4 (ix)		; блок строки + текстура (океан — 13)
	ld	d, a
	ld	bc, #0x27AF		; ждать конца прошлого
3$:	in	a, (c)
	jp	m, 3$
	ld	a, (em_first)
	or	a, a
	jr	nz, 4$
	inc	a
	ld	(em_first), a
	ld	a, (hl)			; x = начало * 2
	add	a, a
	ld	b, #0x1A
	out	(c), a			; SAL
	ld	b, #0x1D
	out	(c), a			; DAL
	ld	a, 5 (ix)
	ld	b, #0x1C
	out	(c), a			; SAX
	ld	a, 2 (ix)
	ld	b, #0x1E
	out	(c), a			; DAH
	ld	a, 3 (ix)
	inc	b
	out	(c), a			; DAX
4$:	ld	a, (em_row)		; строка с тенью (EP_SHF)
	push	hl
	ld	l, a
	ld	h, #>EP_SHF
	ld	a, (hl)
	pop	hl
	or	a, a
	jr	nz, 5$
	ld	b, #0x1B
	out	(c), d			; SAH — блок
	ld	b, #0x26
	out	(c), e			; DMALen
	inc	b
	ld	a, #1
	out	(c), a			; DMACtrl: RAM -> RAM, старт
	ret
5$:	ld	a, (em_tex)		; тень: суша — BLT2 узора на подкладку L, океан — копия x + 256
	cp	a, #13
	jr	z, 6$
	ld	b, #0x1B
	out	(c), d			; SAH — блок
	ld	a, 5 (ix)
	ld	b, #0x1C
	out	(c), a			; SAX — страница узоров
	ld	a, #0x46		; BLT2 по полубайтам, насыщение
	jr	7$
6$:	ld	a, 2 (ix)
	inc	a
	ld	b, #0x1B
	out	(c), a			; SAH — x + 256 строки заднего буфера
	ld	a, 3 (ix)
	ld	b, #0x1C
	out	(c), a			; SAX
	ld	a, #1
7$:	ld	b, #0x26
	out	(c), e			; DMALen
	inc	b
	out	(c), a			; DMACtrl
	ret

;; Шаг рёбер по порядку: u += шаг (новый старший байт — в OUH); ребро, у которого строка
;; была последней, — слот в свободные (сжатие ORD/OUH на месте). Левый старший байт больше
;; нового больше чем на 1 — нужна сортировка.
ael_step:
	ld	a, (rw_n)
	or	a, a
	ret	z
	ld	b, a
	xor	a, a
	ld	(rw_srt), a
	ld	a, (rw_y)
	ld	c, a
	ld	de, #EP_ORD		; чтение
	exx
	ld	de, #EP_ORD		; запись
	ld	bc, #0			; B' — оставлено, C' — прошлый старший байт
	exx
1$:	ld	a, (de)
	inc	e
	ld	l, a			; слот
	ld	h, #>EP_SLAST
	ld	a, (hl)
	cp	a, c
	jr	z, 4$			; последняя строка — удалить
7$:	ld	a, l
	exx
	ld	(de), a			; ORD[w]
	exx
	ld	h, #>EP_SUL
	ld	a, (hl)
	inc	h
	add	a, (hl)
	dec	h
	ld	(hl), a
	ld	h, #>EP_SUH
	ld	a, (hl)
	inc	h
	adc	a, (hl)
	dec	h
	ld	(hl), a
	exx
	inc	d
	ld	(de), a			; OUH[w]
	dec	d
	inc	e
	inc	b
	ld	l, a
	ld	a, c
	sub	a, l			; прошлый - новый > 1 — сортировка
	jr	c, 2$
	cp	a, #2
	jr	c, 2$
	ld	a, #1
	ld	(rw_srt), a
2$:	ld	c, l
	exx
	djnz	1$
	jr	5$
4$:	push	de			; слот -> свободные
	ld	a, (rw_nf)
	ld	e, a
	ld	d, #>EP_FREE
	ld	a, l
	ld	(de), a
	ld	hl, #rw_nf
	inc	(hl)
	pop	de
	djnz	6$
	jr	5$
6$:	ld	a, (de)			; следующее после удалённого: новые соседи несогласованы
	inc	e			; (tr оставленного левее или текстура края != tl) — в
	ld	l, a			; следующей строке полная починка
	ld	h, #>EP_SLAST
	ld	a, (hl)
	cp	a, c
	jr	z, 4$
	exx
	ld	a, e			; оставлено левее (ORD записи)
	exx
	or	a, a
	jr	z, 61$
	dec	a
	push	hl
	ld	l, a
	ld	h, #>EP_ORD
	ld	l, (hl)
	ld	h, #>EP_STR
	ld	a, (hl)
	pop	hl
	jr	62$
61$:	ld	a, (rw_lt)
62$:	cp	a, #0xFE
	jr	z, 7$
	ld	h, #>EP_STL
	cp	a, (hl)
	jr	z, 7$
	ld	a, #1
	ld	(rw_dirty), a
	jr	7$
5$:	exx
	ld	a, b
	exx
	ld	(rw_n), a
	ld	a, (rw_srt)
	or	a, a
	ret	z

;; Сортировка вставками по OUH: элемент сдвигается влево, пока левый больше него на 2 и
;; больше (редко: округление рёбер у почти общих точек)
ael_sort:
	ld	a, (rw_n)
	cp	a, #2
	ret	c
	dec	a
	ld	b, a
	ld	hl, #EP_OUH + 1
1$:	push	bc
	push	hl
	ld	c, (hl)			; OUH[i]
	dec	h
	ld	b, (hl)			; ORD[i]
	inc	h
2$:	ld	a, l
	or	a, a
	jr	z, 4$
	dec	l
	ld	a, (hl)			; OUH[j - 1] - OUH[i] > 1 — сдвиг
	sub	a, c
	jr	c, 3$
	cp	a, #2
	jr	c, 3$
	ld	a, #1
	ld	(rw_dirty), a
	ld	a, (hl)
	inc	l
	ld	(hl), a
	dec	l
	dec	h
	ld	a, (hl)
	inc	l
	ld	(hl), a
	dec	l
	inc	h
	jr	2$
3$:	inc	l
4$:	ld	(hl), c
	dec	h
	ld	(hl), b
	pop	hl
	pop	bc
	inc	l
	djnz	1$
	ret

;; ---------------------------------------------------------------- проекция

;; Вершины (globe.c): _gl_pn векторов (6 байт: X, Y, Z по 2 байта — v & 127, v >> 7) с
;; _gl_pv -> {x, y (i16, Q2), z8 (Q6)} с _gl_pd. Произведение v · K — таблицами рабочей
;; страницы (gl_ktab): T_hi[v >> 7] + T_lo[v & 127], сумма — Q2 пикселя. Каждый байт индекса
;; берётся в L один раз для всех констант: x копится в IX, y — в IY, z — в A'.
;;   x = 512 + K0·X + K1·Y         (страницы #C0.., #C4..)
;;   y = 400 + K2·X + K3·Y + K4·Z  (#C8.., #CC.., #D0..)
;;   z = k5·X + k6·Y + k7·Z        (#D4.., #D8.., #DC.., Q12; копится в HL'); _gl_noz = 1 —
;;                                  ячейка вся на передней стороне, z не считается (4096)
;; Страницы константы K: P, P+1 — T_hi (младшие, старшие байты), P+2, P+3 — T_lo.

PK0	= 0xC0
PK1	= 0xC4
PK2	= 0xC8
PK3	= 0xCC
PK4	= 0xD0
PZ0	= 0xD4
PZ1	= 0xD8
PZ2	= 0xDC

	.macro	TADD	P, R			; R (IX / IY) += слово таблицы P по индексу L
	ld	h, #P
	ld	c, (hl)
	inc	h
	ld	b, (hl)
	add	R, bc
	.endm

	.macro	ZADD	P			; HL' += слово таблицы P по индексу L
	ld	h, #P
	ld	c, (hl)
	inc	h
	ld	b, (hl)
	push	bc
	exx
	pop	bc
	add	hl, bc
	exx
	.endm

	.macro	PJXY	ZX		; x, y (и z, если ZX = 1) вершины (DE) -> (pj_dst), 6 байт
	ld	ix, #512
	ld	iy, #400
	.if	ZX
	exx
	ld	hl, #0
	exx
	.endif
	ld	a, (de)			; X & 127
	inc	de
	ld	l, a
	TADD	PK0+2, ix
	TADD	PK2+2, iy
	.if	ZX
	ZADD	PZ0+2
	.endif
	ld	a, (de)			; X >> 7
	inc	de
	ld	l, a
	TADD	PK0, ix
	TADD	PK2, iy
	.if	ZX
	ZADD	PZ0
	.endif
	ld	a, (de)			; Y & 127
	inc	de
	ld	l, a
	TADD	PK1+2, ix
	TADD	PK3+2, iy
	.if	ZX
	ZADD	PZ1+2
	.endif
	ld	a, (de)			; Y >> 7
	inc	de
	ld	l, a
	TADD	PK1, ix
	TADD	PK3, iy
	.if	ZX
	ZADD	PZ1
	.endif
	ld	a, (de)			; Z & 127
	inc	de
	ld	l, a
	TADD	PK4+2, iy
	.if	ZX
	ZADD	PZ2+2
	.endif
	ld	a, (de)			; Z >> 7
	inc	de
	ld	l, a
	TADD	PK4, iy
	.if	ZX
	ZADD	PZ2
	.endif
	ld	hl, (pj_dst)
	push	ix
	pop	bc
	ld	(hl), c
	inc	hl
	ld	(hl), b
	inc	hl
	push	iy
	pop	bc
	ld	(hl), c
	inc	hl
	ld	(hl), b
	inc	hl
	.if	ZX
	exx
	push	hl
	exx
	pop	bc
	ld	(hl), c
	inc	hl
	ld	(hl), b
	.else
	ld	(hl), #0
	inc	hl
	ld	(hl), #0x10
	.endif
	inc	hl
	ld	(pj_dst), hl
	.endm

_gl_project::
	push	ix
	push	iy
	ld	hl, (_gl_pd)
	ld	(pj_dst), hl
	ld	de, (_gl_pv)
	ld	a, (_gl_pn)
	or	a, a
	jp	z, 9$
	ld	(pj_n), a
	ld	a, (_gl_noz)
	or	a, a
	jp	nz, 5$
1$:	PJXY	1
	ld	hl, #pj_n
	dec	(hl)
	jp	nz, 1$
	jp	9$
5$:	PJXY	0
	ld	hl, #pj_n
	dec	(hl)
	jp	nz, 5$
9$:	pop	iy
	pop	ix
	ret

;; Таблицы произведений на K = _gl_k (int32 = R·c, c в Q14) в страницах P = _gl_kpg:
;; T_hi[j] = round(K·j / 2^19), j = -128..127 (индекс — j как байт): младшие байты в P,
;; старшие — в P + 1; T_lo[i] = round(K·i / 2^26), i = 0..127: в P + 2, P + 3. Накопитель —
;; 16.16 с шагом K / 2^3 (T_hi) и K / 2^10 (T_lo), значение — старшее слово.
_gl_ktab::
	ld	hl, (_gl_k)
	ld	de, (_gl_k + 2)
	ld	b, #3
1$:	sra	d
	rr	e
	rr	h
	rr	l
	djnz	1$
	ld	a, (_gl_kpg)
	call	kt_put16
	ld	hl, (_gl_k)
	ld	de, (_gl_k + 2)
	ld	b, #10
2$:	sra	d
	rr	e
	rr	h
	rr	l
	djnz	2$
	ld	a, (_gl_kpg)
	add	a, #2
	jp	kt_lo16

;; T[j], j = 0..127 (индексы 0..127) и -1..-128 (255..128), страницы A (младшие байты) и
;; A + 1 (старшие); шаг DE:HL (16.16, DE — старшее слово)
kt_put16:
	call	kt_lo16
	exx				; вниз: j = -1..-128
	ld	de, (kt_sh)
	ld	a, (kt_pg)
	ld	b, a
	ld	c, #0
	ld	hl, #0
	exx
	ld	de, (kt_sl)
	ld	hl, #0x8000
	exx
1$:	dec	c
	exx
	or	a, a
	sbc	hl, de
	exx
	sbc	hl, de
	ld	a, l
	ld	(bc), a
	inc	b
	ld	a, h
	ld	(bc), a
	dec	b
	ld	a, c
	cp	a, #0x80
	jr	nz, 1$
	exx
	ret

;; 128 значений вверх (j = 0..127), страницы A, A + 1; шаг DE:HL. Во втором наборе: B'C' —
;; место, HL' — значение, D'E' — шаг (старшее); в основном: HL — дробь, DE — шаг (младшее).
kt_lo16:
	ld	(kt_sh), de
	ld	(kt_sl), hl
	ld	(kt_pg), a
	exx
	ld	de, (kt_sh)
	ld	b, a
	ld	c, #0
	ld	hl, #0
	exx
	ld	de, (kt_sl)
	ld	hl, #0x8000
	exx
1$:	ld	a, l
	ld	(bc), a
	inc	b
	ld	a, h
	ld	(bc), a
	dec	b
	inc	c
	exx
	add	hl, de
	exx
	adc	hl, de
	bit	7, c
	jr	z, 1$
	exx
	ret

;; DE = B * C (8 x 8, без знака): q(B + C) - q(|B - C|), q — таблица _gl_sq
;; (младшие байты — страницы +0, +1; старшие — +2, +3)
mul8e:
	ld	a, b
	add	a, c
	ld	l, a
	ld	a, #>_gl_sq
	adc	a, #0
	ld	h, a
	ld	e, (hl)
	inc	h
	inc	h
	ld	d, (hl)
	ld	a, b
	sub	a, c
	jr	nc, 1$
	neg
1$:	ld	l, a
	ld	h, #>_gl_sq
	ld	a, e
	sub	a, (hl)
	ld	e, a
	inc	h
	inc	h
	ld	a, d
	sbc	a, (hl)
	ld	d, a
	ret

;; HL:DE = HL * DE (16 x 16 -> 32 без знака, точно: 4 произведения 8 x 8)
mulu16:
	ld	(mu_a), hl
	ld	(mu_b), de
	ld	b, l
	ld	c, e
	call	mul8e			; aL·bL
	ld	(mu_r0), de
	ld	a, (mu_a)
	ld	b, a
	ld	a, (mu_b + 1)
	ld	c, a
	call	mul8e			; aL·bH
	push	de
	ld	a, (mu_a + 1)
	ld	b, a
	ld	a, (mu_b)
	ld	c, a
	call	mul8e			; aH·bL
	pop	hl
	add	hl, de			; середина (17 бит)
	ld	a, #0
	adc	a, a
	ld	(mu_c), a
	push	hl
	ld	a, (mu_a + 1)
	ld	b, a
	ld	a, (mu_b + 1)
	ld	c, a
	call	mul8e			; aH·bH
	pop	hl			; середина
	ld	a, (mu_r0 + 1)
	add	a, l
	ld	b, a			; байт 1
	ld	a, h
	adc	a, e
	ld	l, a			; байт 2
	ld	a, (mu_c)
	adc	a, d
	ld	h, a			; байт 3
	ld	d, b
	ld	a, (mu_r0)
	ld	e, a
	ret
