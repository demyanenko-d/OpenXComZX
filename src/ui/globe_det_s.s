;; Банк 26: точки и отрезки деталей глобуса (globe_det.c) в задний буфер (строки экрана 280..479) или
;; на экран (метки) — строка под y = 0 в DL_YB; окно 256 x 200. Параметры — в переменных globe_det.c
;; (хвост страницы банка): DL_X0, DL_Y0, DL_X1, DL_Y1 (int16), DL_C. Экран 256c: строка — 512 байт, страница — 32 строки (src/map.h): адрес в Win3 =
;; #C000 + (Y & 31) · 512 + x, страница #10 + Y >> 5.

	.module globe_det_s
	.optsdcc -mz80 sdcccall(1)

	.globl	_det_line, _det_pset, _det_poly, _det_clip, _det_muldiv, _pg_map3, ___muluint2ulong

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
