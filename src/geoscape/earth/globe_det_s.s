;; Банк 26: точки и отрезки деталей глобуса (globe_det.c) в задний буфер (строки экрана 280..479) или
;; на экран (метки) — строка под y = 0 в DL_YB; окно 256 x 200. Параметры — в переменных globe_det.c
;; (хвост страницы банка): DL_X0, DL_Y0, DL_X1, DL_Y1 (int16), DL_C. Экран 256c: строка — 512 байт, страница — 32 строки (src/map.h): адрес в Win3 =
;; #C000 + (Y & 31) · 512 + x, страница #10 + Y >> 5.

	.module globe_det_s
	.optsdcc -mz80 sdcccall(1)

	.globl	_det_line, _det_pset, _det_poly, _det_clip, _det_muldiv, _det_mq, _det_xline, _det_ring, _det_marks, _pg_map3
	.globl	___muluint2ulong, ___mulsint2slong

DL_X0	= 0xBFF0
DL_Y0	= 0xBFF2
DL_X1	= 0xBFF4
DL_Y1	= 0xBFF6
DL_C	= 0xBFF8
DP_PTR	= 0xBFFA			; det_poly: вершины (проекции x, y — 1/4 точки, z) и их число
DP_CNT	= 0xBFFC
MD_SG	= 0xBFFD			; det_muldiv: знак
MD_HI	= 0xBFFE			;   старшее слово произведения
DL_YB	= 0xBFE2			; строка экрана под y = 0: 280 — задний буфер, 0 — экран
MK_SPR	= 0xBD40			; globe_det.c: кадры меток 9 x 9, фаза мигания, число и список (x, y, кадр)
MK_BLINK = 0xBD92
MK_N	= 0xBD93
MK_LIST	= 0xBDA0
DL_OCEAN = 0xBFE4			; det_xline: первый цвет океана (globe.rul oceanPalette · 16)
XL_DX	= 0xBFE6			;   Δx, Δy отрезка
XL_DY	= 0xBFE8
XL_LEN	= 0xBFEA
LN_PG	= 0xBFF9			; подключённая страница экрана
BACK_Y	= 280
SCREEN_PAGE = 0x10
GLOBE_H	= 200
OP_INC_L = 0x2C
OP_DEC_L = 0x2D

	.area	_BANK26

;; void det_pset(void) — точка (DL_X0, DL_Y0) цветом DL_C, если в окне 256 x 200
_det_pset::
	ld	hl, (DL_X0)
	ld	a, h
	or	a, a
	ret	nz
	ld	de, (DL_Y0)
	ld	a, d
	or	a, a
	ret	nz
	ld	a, e
	cp	a, #GLOBE_H
	ret	nc
	ld	a, l
	call	addr
	ld	a, (DL_C)
	ld	(hl), a
	ret

;; void det_poly(void) — линия DP_CNT вершин с DP_PTR: отрезок, если оба конца на передней стороне
;; (z >= 0); целиком в окне — сразу det_line, иначе det_clip (SDL_gfx _clipLine, globe_det.c)
_det_poly::
	push	ix
	push	iy
	ld	ix, (DP_PTR)
	ld	a, (DP_CNT)
	ld	b, a
	dec	b
	jp	z, dp_done
	jp	m, dp_done
dp_seg:
	push	bc
	bit	7, 5 (ix)
	jp	nz, dp_next
	bit	7, 11 (ix)
	jp	nz, dp_next
	ld	l, 0 (ix)			; концы: (v >> 2) со знаком
	ld	h, 1 (ix)
	sra	h
	rr	l
	sra	h
	rr	l
	ld	(DL_X0), hl
	ld	d, h
	ld	l, 2 (ix)
	ld	h, 3 (ix)
	sra	h
	rr	l
	sra	h
	rr	l
	ld	(DL_Y0), hl
	ld	l, 6 (ix)
	ld	h, 7 (ix)
	sra	h
	rr	l
	sra	h
	rr	l
	ld	(DL_X1), hl
	ld	d, h
	ld	l, 8 (ix)
	ld	h, 9 (ix)
	sra	h
	rr	l
	sra	h
	rr	l
	ld	(DL_Y1), hl
	ld	a, (DL_X0 + 1)			; целиком в окне: старшие байты x0, y0, x1, y1 — 0, y < 200
	or	a, h
	or	a, d
	ld	h, a
	ld	a, (DL_Y0 + 1)
	or	a, h
	jr	nz, dp_clip
	ld	a, (DL_Y0)
	cp	a, #GLOBE_H
	jr	nc, dp_clip
	ld	a, (DL_Y1)
	cp	a, #GLOBE_H
	jr	c, dp_draw
dp_clip:
	push	iy
	push	ix
	call	_det_clip			; C: IX, IY — сохранить
	pop	ix
	pop	iy
	or	a, a
	jr	z, dp_next
dp_draw:
	call	_det_line
dp_next:
	ld	de, #6
	add	ix, de
	pop	bc
	dec	b
	jp	nz, dp_seg
dp_done:
	pop	iy
	pop	ix
	ret

;; A — x, E — y (окно) -> HL — адрес в Win3 (страница строки подключена)
addr:
	push	af
	ld	d, #0
	ld	hl, (DL_YB)			; Y = y + DL_YB
	add	hl, de
	ld	a, l
	and	a, #31
	add	a, a
	add	a, #0xC0
	ld	e, a				; E — старший байт адреса
	add	hl, hl
	add	hl, hl
	add	hl, hl
	ld	a, h
	add	a, #SCREEN_PAGE
	ld	(LN_PG), a
	push	de
	call	_pg_map3			; портит HL, E, BC
	pop	de
	pop	af
	ld	h, e
	ld	l, a
	ret

;; void det_marks(void) — метки из списка MK_LIST (MK_N штук: x, y — центр в окне 1..254 x 1..198, кадр) на
;; экран (DL_YB = 0): кадр 3x3, точка 0 — прозрачна, цвет + фаза мигания (кадр 8 — город — не мигает)
_det_marks::
	push	ix
	ld	a, (MK_N)
	or	a, a
	jr	z, 9$
	ld	b, a
	ld	ix, #MK_LIST
1$:	push	bc
	xor	a, a
	ld	(ln_sy), a
	ld	a, 2 (ix)			; C — прибавка цвета, DE — кадр
	ld	c, #0
	cp	a, #8
	jr	z, 2$
	ld	a, (MK_BLINK)
	ld	c, a
	ld	a, 2 (ix)
2$:	ld	l, a
	ld	h, #0
	ld	e, l
	ld	d, h
	add	hl, hl
	add	hl, hl
	add	hl, hl
	add	hl, de
	ld	de, #MK_SPR
	add	hl, de
	push	hl
	ld	a, 1 (ix)			; первая точка (x − 1, y − 1)
	dec	a
	ld	e, a
	ld	a, 0 (ix)
	dec	a
	push	bc
	call	addr				; HL — адрес (портит BC)
	pop	bc
	pop	de				; DE — кадр
	ld	b, #3
3$:	push	bc
	ld	b, #3
4$:	ld	a, (de)
	inc	de
	or	a, a
	jr	z, 5$
	add	a, c
	ld	(hl), a
5$:	inc	l
	djnz	4$
	dec	l
	dec	l
	dec	l
	pop	bc
	push	bc
	call	ystep				; следующая строка (страница — при переходе)
	pop	bc
	djnz	3$
	ld	de, #3
	add	ix, de
	pop	bc
	djnz	1$
9$:	pop	ix
	ret

;; void det_line(void) — SDL_gfx lineColor после _clipLine (концы уже в окне): dx = |Δx| + 1, dy = |Δy| + 1,
;; по большей оси — каждый шаг, по меньшей — когда накопитель y += dy достигает dx (y −= dx)
_det_line::
	push	ix
	ld	a, (DL_X0)
	ld	de, (DL_Y0)
	call	addr				; HL — первая точка
	push	hl
	ld	hl, (DL_X1)			; sx, |Δx| + 1
	ld	de, (DL_X0)
	or	a, a
	sbc	hl, de
	ld	a, #OP_INC_L
	jr	nc, 1$
	call	neg
	ld	a, #OP_DEC_L
1$:	ld	(xm_sx), a
	ld	(ym_sx), a
	inc	hl
	ld	b, h
	ld	c, l				; BC = dx
	ld	hl, (DL_Y1)			; sy, |Δy| + 1
	ld	de, (DL_Y0)
	or	a, a
	sbc	hl, de
	ld	a, #0
	jr	nc, 2$
	call	neg
	inc	a
2$:	ld	(ln_sy), a
	inc	hl				; HL = dy
	ld	a, (DL_C)
	.db	0xDD, 0x6F			; ld ixl, a — цвет
	push	hl
	or	a, a
	sbc	hl, bc
	pop	hl
	jr	nc, ln_ymaj			; dx <= dy... dx < dy — ось y
	;; ось x: BC — шагов (dx), накопитель y в HL', шаг DE' = dy, предел BC' = dx
ln_xmaj:
	push	bc
	push	hl
	exx
	pop	de
	pop	bc
	ld	hl, #0
	exx
	pop	hl
xm_loop:
	.db	0xDD, 0x7D			; ld a, ixl
	ld	(hl), a
xm_sx:	inc	l				; inc l / dec l
	exx
	add	hl, de
	or	a, a
	sbc	hl, bc
	jr	nc, 1$
	add	hl, bc
	exx
	jr	2$
1$:	exx
	call	ystep
2$:	dec	bc
	ld	a, b
	or	a, c
	jr	nz, xm_loop
	pop	ix
	ret
	;; ось y: шагов dy, накопитель x += dx до dy
ln_ymaj:
	push	hl				; шагов
	push	bc
	push	hl
	exx
	pop	bc				; предел — dy
	pop	de				; шаг — dx
	ld	hl, #0
	exx
	pop	bc
	pop	hl
	jr	ym_loop
ym_loop:
	.db	0xDD, 0x7D			; ld a, ixl
	ld	(hl), a
	call	ystep
	exx
	add	hl, de
	or	a, a
	sbc	hl, bc
	jr	nc, 1$
	add	hl, bc
	exx
	jr	ym_nx
1$:	exx
ym_sx:	inc	l				; inc l / dec l
ym_nx:	dec	bc
	ld	a, b
	or	a, c
	jr	nz, ym_loop
	pop	ix
	ret

;; Строка экрана вниз (ln_sy = 0) или вверх: H ± 2, на краю страницы — соседняя страница
ystep:
	ld	a, (ln_sy)
	or	a, a
	jr	nz, 2$
	inc	h
	inc	h
	ret	nz				; #FE + 2 = 0 — следующая страница
	ld	a, (LN_PG)
	inc	a
	jr	3$
2$:	dec	h
	dec	h
	ld	a, h
	cp	a, #0xC0
	ret	nc
	ld	a, (LN_PG)
	dec	a
3$:	ld	(LN_PG), a
	push	hl
	push	bc
	push	de
	call	_pg_map3
	pop	de
	pop	bc
	pop	hl
	ld	a, (ln_sy)
	or	a, a
	ld	h, #0xC0
	ret	z
	ld	h, #0xFE
	ret

;; int16_t det_muldiv(int16_t a /*HL*/, int16_t b /*DE*/, int16_t c /*стек*/) — a · b / c с отбрасыванием дробной
;; части (как C); частное должно помещаться в 16 бит. Произведение 16 x 16 -> 32, деление 32 / 16 за 16 шагов.
_det_muldiv::
	ld	a, h
	xor	a, d
	ld	(MD_SG), a
	bit	7, h
	call	nz, negh
	ex	de, hl
	bit	7, h
	call	nz, negh
	call	___muluint2ulong		; HL:DE = |a| · |b| (портит IY)
	ld	(MD_HI), hl
	ld	hl, #2
	add	hl, sp
	ld	c, (hl)
	inc	hl
	ld	b, (hl)				; BC = c
	ld	a, (MD_SG)
	xor	a, b
	ld	(MD_SG), a
	bit	7, b
	jr	z, 1$
	xor	a, a
	sub	a, c
	ld	c, a
	sbc	a, a
	sub	a, b
	ld	b, a
1$:	ld	hl, (MD_HI)			; остаток — старшее слово (< делителя), в DE вдвигается частное
	ld	a, #16
2$:	sla	e
	rl	d
	adc	hl, hl
	jr	c, 3$				; остаток за 16 бит — точно не меньше делителя
	or	a, a
	sbc	hl, bc
	jr	nc, 4$
	add	hl, bc
	jr	5$
3$:	or	a, a
	sbc	hl, bc
4$:	inc	e
5$:	dec	a
	jr	nz, 2$
	ld	a, (MD_SG)
	bit	7, a
	jr	z, 6$
	xor	a, a
	sub	a, e
	ld	e, a
	sbc	a, a
	sub	a, d
	ld	d, a
6$:	pop	hl				; возврат, аргумент c — со стека
	pop	bc
	jp	(hl)

;; int16_t det_mq(int16_t a /*HL*/, int16_t b /*DE*/) — (a · b) >> 14 со знаком (Q14)
_det_mq::
	call	___mulsint2slong		; HL:DE
	sla	e
	rl	d
	rl	l
	rl	h
	sla	e
	rl	d
	rl	l
	rl	h
	ex	de, hl
	ret

;; void det_xline(void) — Globe::XuLine после отсечения (концы в окне): len = большая из |Δx|, |Δy|, по
;; большей оси — шаг ±1, по меньшей — Δ/len (8.8, отбрасывание дроби), len точек с первого конца (второй
;; не рисуется); точка — затемнение цвета под ней: океан (OCEAN..OCEAN+31) -> OCEAN + 14, суша — +6 в
;; пределах группы из 16 цветов (CreateShadow::getOceanShadow / getLandShadow, shade 6)
_det_xline::
	push	ix
	ld	hl, (DL_X1)
	ld	de, (DL_X0)
	or	a, a
	sbc	hl, de
	ld	(XL_DX), hl
	ld	hl, (DL_Y1)
	ld	de, (DL_Y0)
	or	a, a
	sbc	hl, de
	ld	(XL_DY), hl
	call	absl
	ld	b, c				; B = |Δy|
	ld	hl, (XL_DX)
	call	absl				; C = |Δx|
	ld	a, c
	cp	a, b
	jp	c, xl_ymaj			; |Δx| < |Δy| — ось y
	or	a, a
	jp	z, xl_done
	;; ось x: x ± 1 (inc l / dec l), y += Δy / len (8.8) — при смене строки указатель на строку
	ld	(XL_LEN), a
	ld	hl, (XL_DY)
	call	xl_step				; DE — шаг y
	push	de
	exx					; второй набор: DE — шаг, HL — y (8.8)
	pop	de
	ld	a, (DL_Y0)
	ld	h, a
	ld	l, #0
	exx
	ld	hl, (XL_DX)
	ld	a, #OP_INC_L
	bit	7, h
	jr	z, 1$
	ld	a, #OP_DEC_L
1$:	ld	(xx_sx), a
	ld	a, (DL_X0)
	ld	de, (DL_Y0)
	call	addr				; HL — первая точка
	ld	a, (DL_C)
	ld	c, a
	ld	a, (XL_LEN)
	ld	b, a
xx_l:	call	xl_shade
xx_sx:	inc	l				; inc l / dec l
	exx
	ld	a, h
	add	hl, de
	sub	a, h				; прежняя строка − новая: 0, −1 (вниз), 1 (вверх)
	exx
	jr	z, 3$
	jp	m, 2$
	ld	a, #1
	ld	(ln_sy), a
	call	ystep
	jr	3$
2$:	xor	a, a
	ld	(ln_sy), a
	call	ystep
3$:	djnz	xx_l
	jp	xl_done
xl_ymaj:				; ось y: строка ± 1, x += Δx / len — при смене столбца inc l / dec l
	ld	a, b
	ld	(XL_LEN), a
	ld	hl, (XL_DX)
	call	xl_step
	push	de
	exx					; второй набор: DE — шаг, HL — x (8.8)
	pop	de
	ld	a, (DL_X0)
	ld	h, a
	ld	l, #0
	exx
	ld	hl, (XL_DY)
	xor	a, a
	bit	7, h
	jr	z, 4$
	inc	a
4$:	ld	(ln_sy), a
	ld	a, (DL_X0)
	ld	de, (DL_Y0)
	call	addr
	ld	a, (DL_C)
	ld	c, a
	ld	a, (XL_LEN)
	ld	b, a
yy_l:	call	xl_shade
	call	ystep
	exx
	ld	a, h
	add	hl, de
	sub	a, h
	exx
	jr	z, 6$
	jp	m, 5$
	dec	l
	jr	6$
5$:	inc	l
6$:	djnz	yy_l
xl_done:
	pop	ix
	ret

;; (HL) — точка цветом C (круг радара — сплошной контур, просьба пользователя; у OpenXcom XuLine
;; затемняет точку под линией)
xl_shade:
	ld	(hl), c
	ret

;; HL — Δ (|Δ| <= len), A — len (1..255) -> DE = Δ · 256 / len с отбрасыванием дроби (деление 16 / 8)
xl_step:
	ld	c, a
	ld	b, h				; B — знак
	ld	a, l
	bit	7, h
	jr	z, 1$
	neg
1$:	ld	d, a				; DE = |Δ| · 256
	ld	e, #0
	xor	a, a				; A — остаток
	ld	h, #16
2$:	sla	e
	rl	d
	rla
	jr	c, 3$
	cp	a, c
	jr	c, 4$
3$:	sub	a, c
	inc	e
4$:	dec	h
	jr	nz, 2$
	bit	7, b
	ret	z
	xor	a, a
	sub	a, e
	ld	e, a
	sbc	a, a
	sub	a, d
	ld	d, a
	ret

;; void det_ring(void) — отрезки круга радара: DP_CNT точек (x, y int16; y = #8000 — сзади) с DP_PTR,
;; отрезок от точки к прошлой, если обе спереди (drawGlobeCircle); целиком в окне — сразу det_xline,
;; иначе det_clip
_det_ring::
	push	ix
	push	iy
	ld	ix, (DP_PTR)
	ld	a, (DP_CNT)
	ld	b, a
	dec	b
	jp	z, dr_done
	jp	m, dr_done
dr_seg:
	push	bc
	ld	a, 7 (ix)			; текущая (ix + 4) и прошлая (ix) спереди
	cp	a, #0x80
	jr	nz, 1$
	ld	a, 6 (ix)
	or	a, a
	jp	z, dr_next
1$:	ld	a, 3 (ix)
	cp	a, #0x80
	jr	nz, 2$
	ld	a, 2 (ix)
	or	a, a
	jp	z, dr_next
2$:	ld	l, 4 (ix)			; DL_X0, DL_Y0 — текущая, DL_X1, DL_Y1 — прошлая
	ld	h, 5 (ix)
	ld	(DL_X0), hl
	ld	l, 6 (ix)
	ld	h, 7 (ix)
	ld	(DL_Y0), hl
	ld	l, 0 (ix)
	ld	h, 1 (ix)
	ld	(DL_X1), hl
	ld	l, 2 (ix)
	ld	h, 3 (ix)
	ld	(DL_Y1), hl
	ld	a, 5 (ix)			; целиком в окне: старшие байты — 0, y < 200
	or	a, 7 (ix)
	or	a, 1 (ix)
	or	a, 3 (ix)
	jr	nz, dr_clip
	ld	a, 6 (ix)
	cp	a, #GLOBE_H
	jr	nc, dr_clip
	ld	a, 2 (ix)
	cp	a, #GLOBE_H
	jr	c, dr_draw
dr_clip:
	push	ix
	call	_det_clip			; C: IX, IY — сохранить
	pop	ix
	or	a, a
	jr	z, dr_next
dr_draw:
	push	ix
	call	_det_xline
	pop	ix
dr_next:
	ld	de, #4
	add	ix, de
	pop	bc
	dec	b
	jp	nz, dr_seg
dr_done:
	pop	iy
	pop	ix
	ret

;; HL (|HL| < 256) -> C = |HL|
absl:
	ld	c, l
	bit	7, h
	ret	z
	xor	a, a
	sub	a, l
	ld	c, a
	ret

;; HL = −HL (DE не трогает; портит A)
negh:
	xor	a, a
	sub	a, l
	ld	l, a
	sbc	a, a
	sub	a, h
	ld	h, a
	ret

;; HL = −HL
neg:
	ex	de, hl
	ld	hl, #0
	or	a, a
	sbc	hl, de
	ret

ln_sy:	.db	0				; 0 — y растёт
