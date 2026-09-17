;; Банк 2: точки и клики глобуса (src/inc/globe.h) — ортографическая проекция OpenXcom
;; (Globe::polarToCart / cartToPolar) и синус 16-битного угла для констант вида рендера (банки 24,
;; 25). Перевод globe_ui.c на ассемблер.
;;
;; Соглашения — как в globe_view.s: банковые функции берут аргументы из стека (SP + 5 — первый),
;; результат-байт — A, 16 бит — DE; ядро sdcccall(1) (__mulsint2slong: HL x DE -> HL:DE); IX
;; сохраняется. 32-битные величины — в переменных, операции над ними — процедурами ниже.

	.module globe_ui
	.optsdcc -mz80 sdcccall(1)

	.globl	_globe_sin, _globe_xy, _globe_lonlat
	.globl	b_globe_sin, b_globe_xy, b_globe_lonlat
	.globl	_sin_q14, _atan_tab, _ctx, _st_, ___mulsint2slong, ___muluint2ulong

b_globe_sin	= 2
b_globe_xy	= 2
b_globe_lonlat	= 2

GLOBE_CX	= 128
GLOBE_CY	= 100
ST_ZOOM		= 72			; state_t.zoom
CTX_LON		= 6			; ctx.globe_lon, globe_lat
CTX_LAT		= 8

	.area	_BANK2

;; рабочие переменные (только код банка 2)

gu_sp:		.ds	2		; точка: sin, cos широты; sin, cos разности долгот
gu_cp:		.ds	2
gu_sd:		.ds	2
gu_cd:		.ds	2
gu_sc:		.ds	2		; наклон вида: sin, cos
gu_cc:		.ds	2
gu_pc:		.ds	2
gu_r:		.ds	2
gu_u:		.ds	2
gu_t:		.ds	4
gu_x:		.ds	2		; клик: X, Y (Q14 доли радиуса), Z, s, pc, h
gu_y:		.ds	2
gu_z:		.ds	2
gu_s:		.ds	2
gu_pz:		.ds	2
gu_h:		.ds	2
gu_lat:		.ds	2
gu_vlon:	.ds	2
is_v:		.ds	4		; корень: число, результат, бит, сумма
is_r:		.ds	4
is_b:		.ds	4
is_t:		.ds	4
at_y:		.ds	2		; арктангенс: y, x, |y|, |x|
at_x:		.ds	2
at_sw:		.ds	1


zoom_r:	.dw	90, 120, 180, 280, 450, 720	; Globe::setupRadii

;; ================================================================ синус

;; HL — угол (65536 = 360°) -> DE = sin (Q14); портит A, B, HL
sin16:
	ld	b, h
	ld	a, h
	and	a, #0x3F
	ld	h, a
	srl	h
	rr	l
	srl	h
	rr	l
	srl	h
	rr	l
	srl	h
	rr	l			; i = (a >> 4) & 1023
	bit	6, b
	jr	z, 1$
	ex	de, hl
	ld	hl, #1024
	or	a, a
	sbc	hl, de
1$:	add	hl, hl
	ld	de, #_sin_q14
	add	hl, de
	ld	e, (hl)
	inc	hl
	ld	d, (hl)
	bit	7, b
	ret	z
	xor	a, a			; DE = −DE (значение — в DE, не в HL)
	sub	a, e
	ld	e, a
	sbc	a, a
	sub	a, d
	ld	d, a
	ret

;; int16_t globe_sin(uint16_t a) __banked
_globe_sin::
	ld	hl, #5
	add	hl, sp
	ld	a, (hl)
	inc	hl
	ld	h, (hl)
	ld	l, a
	jp	sin16

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

;; R зума (ST в Win3) -> HL
zoom_rad:
	ld	a, (_st_ + ST_ZOOM)
	cp	a, #6
	jr	c, 1$
	ld	a, #5
1$:	add	a, a
	ld	e, a
	ld	d, #0
	ld	hl, #zoom_r
	add	hl, de
	ld	a, (hl)
	inc	hl
	ld	h, (hl)
	ld	l, a
	ret

;; ================================================================ точка -> экран

;; uint8_t globe_xy(const geo_t *p, int16_t *x, int16_t *y) __banked — Globe::polarToCart; 0 —
;; точка на задней стороне или у края окна
_globe_xy::
	push	ix
	ld	ix, #0
	add	ix, sp			; +7 p, +9 x*, +11 y*
	ld	l, 7 (ix)
	ld	h, 8 (ix)
	push	hl
	pop	iy			; IY — geo_t: lon (+0), lat (+4)
	ld	l, 6 (iy)		; a = старшее слово широты
	ld	h, 7 (iy)
	push	hl
	call	sin16
	ld	(gu_sp), de
	pop	hl
	ld	de, #0x4000
	add	hl, de
	call	sin16
	ld	(gu_cp), de
	ld	l, 2 (iy)		; d = старшее слово долготы − λ0
	ld	h, 3 (iy)
	ld	de, (_ctx + CTX_LON)
	or	a, a
	sbc	hl, de
	push	hl
	call	sin16
	ld	(gu_sd), de
	pop	hl
	ld	de, #0x4000
	add	hl, de
	call	sin16
	ld	(gu_cd), de
	call	zoom_rad
	ld	(gu_r), hl
	ld	hl, (_ctx + CTX_LAT)
	push	hl
	call	sin16
	ld	(gu_sc), de
	pop	hl
	ld	de, #0x4000
	add	hl, de
	call	sin16
	ld	(gu_cc), de
	ld	hl, (gu_cp)		; pc = SHR14(cp · cd)
	ld	de, (gu_cd)
	call	___mulsint2slong
	call	shr14
	ld	(gu_pc), hl
	ld	hl, (gu_cc)		; cc · pc + sc · sp < 0 — сзади (Globe::pointBack)
	ld	de, (gu_pc)
	call	___mulsint2slong
	ld	(gu_t), de
	ld	(gu_t + 2), hl
	ld	hl, (gu_sc)
	ld	de, (gu_sp)
	call	___mulsint2slong
	push	hl
	ld	hl, (gu_t)
	add	hl, de
	pop	de
	ld	hl, (gu_t + 2)
	adc	hl, de
	bit	7, h
	jp	nz, xy_no
	ld	hl, (gu_cp)		; u = SHR14(cp · sd)
	ld	de, (gu_sd)
	call	___mulsint2slong
	call	shr14
	ld	(gu_u), hl
	ld	hl, (gu_cc)		; v = SHR14(cc · sp − sc · pc)
	ld	de, (gu_sp)
	call	___mulsint2slong
	ld	(gu_t), de
	ld	(gu_t + 2), hl
	ld	hl, (gu_sc)
	ld	de, (gu_pc)
	call	___mulsint2slong
	push	hl
	ld	hl, (gu_t)
	or	a, a
	sbc	hl, de
	ex	de, hl
	pop	bc
	ld	hl, (gu_t + 2)
	sbc	hl, bc
	call	shr14			; HL = v
	ex	de, hl
	ld	hl, (gu_r)		; py = 100 + SHR14(r · v)
	call	___mulsint2slong
	call	shr14
	ld	de, #GLOBE_CY
	add	hl, de
	push	hl
	ld	hl, (gu_r)		; px = 128 + SHR14(r · u)
	ld	de, (gu_u)
	call	___mulsint2slong
	call	shr14
	ld	de, #GLOBE_CX
	add	hl, de			; HL = px
	pop	de			; DE = py
	bit	7, h			; 1 <= px <= 254
	jr	nz, xy_no
	ld	a, h
	or	a, a
	jr	nz, xy_no
	ld	a, l
	or	a, a
	jr	z, xy_no
	cp	a, #255
	jr	nc, xy_no
	bit	7, d			; 1 <= py <= 198
	jr	nz, xy_no
	ld	a, d
	or	a, a
	jr	nz, xy_no
	ld	a, e
	or	a, a
	jr	z, xy_no
	cp	a, #199
	jr	nc, xy_no
	ld	c, 9 (ix)
	ld	b, 10 (ix)
	ld	a, l
	ld	(bc), a
	inc	bc
	ld	a, h
	ld	(bc), a
	ld	c, 11 (ix)
	ld	b, 12 (ix)
	ld	a, e
	ld	(bc), a
	inc	bc
	ld	a, d
	ld	(bc), a
	ld	a, #1
	pop	ix
	ret
xy_no:
	xor	a, a
	pop	ix
	ret

;; ================================================================ экран -> точка

;; void globe_lonlat(int16_t x, int16_t y, geo_t *p) __banked — Globe::cartToPolar: точка сферы
;; (X, Y, Z) в долях радиуса Q14, обратный поворот на наклон C: sinφ = cosC·Y + sinC·Z,
;; cosφ·cosΔ = cosC·Z − sinC·Y, cosφ·sinΔ = X. За краем диска — на край.
_globe_lonlat::
	push	ix
	ld	ix, #0
	add	ix, sp			; +7 x, +9 y, +11 p
	call	zoom_rad
	ld	b, h
	ld	c, l			; BC = r
	push	bc
	ld	l, 7 (ix)		; X = ((x − 128) << 14) / r
	ld	h, 8 (ix)
	ld	de, #-GLOBE_CX
	add	hl, de
	call	divq14
	ld	(gu_x), hl
	pop	bc
	ld	l, 9 (ix)		; Y = ((y − 100) << 14) / r
	ld	h, 10 (ix)
	ld	de, #-GLOBE_CY
	add	hl, de
	call	divq14
	ld	(gu_y), hl
	call	sq2			; is_v = X² + Y²
	ld	a, (is_v + 3)		; > 16384² = #10000000 — за краем
	cp	a, #0x10
	jr	c, 1$
	jr	nz, 2$
	ld	hl, (is_v)
	ld	a, h
	or	a, l
	ld	hl, (is_v + 2)
	or	a, l
	jr	z, 1$			; ровно на краю
2$:	call	isqrt32			; m = sqrt(rr); X, Y = X · 16384 / m
	ld	b, h
	ld	c, l
	push	bc
	ld	hl, (gu_x)
	call	divq14
	ld	(gu_x), hl
	pop	bc
	ld	hl, (gu_y)
	call	divq14
	ld	(gu_y), hl
	ld	hl, #0			; rr = 16384²
	ld	(is_v), hl
	ld	hl, #0x1000
	ld	(is_v + 2), hl
1$:	ld	hl, #0			; Z = sqrt(16384² − rr)
	or	a, a
	ld	de, (is_v)
	sbc	hl, de
	ld	(is_v), hl
	ld	hl, #0x1000
	ld	de, (is_v + 2)
	sbc	hl, de
	ld	(is_v + 2), hl
	call	isqrt32
	ld	(gu_z), hl
	ld	hl, (_ctx + CTX_LAT)	; наклон вида
	push	hl
	call	sin16
	ld	(gu_sc), de
	pop	hl
	ld	de, #0x4000
	add	hl, de
	call	sin16
	ld	(gu_cc), de
	ld	hl, (gu_cc)		; s = (cc · Y + sc · Z) >> 14
	ld	de, (gu_y)
	call	___mulsint2slong
	ld	(gu_t), de
	ld	(gu_t + 2), hl
	ld	hl, (gu_sc)
	ld	de, (gu_z)
	call	___mulsint2slong
	push	hl
	ld	hl, (gu_t)
	add	hl, de
	ex	de, hl
	pop	bc
	ld	hl, (gu_t + 2)
	adc	hl, bc
	call	shr14
	ld	(gu_s), hl
	ld	hl, (gu_cc)		; pc = (cc · Z − sc · Y) >> 14
	ld	de, (gu_z)
	call	___mulsint2slong
	ld	(gu_t), de
	ld	(gu_t + 2), hl
	ld	hl, (gu_sc)
	ld	de, (gu_y)
	call	___mulsint2slong
	push	hl
	ld	hl, (gu_t)
	or	a, a
	sbc	hl, de
	ex	de, hl
	pop	bc
	ld	hl, (gu_t + 2)
	sbc	hl, bc
	call	shr14
	ld	(gu_pz), hl
	ld	hl, (gu_pz)		; h = sqrt(pc² + X²)
	ld	(gu_y), hl		; (Y больше не нужен — sq2 берёт gu_x и gu_y)
	call	sq2
	call	isqrt32
	ld	(gu_h), hl
	ld	hl, (gu_s)		; lat = atan2(s, h)
	ld	de, (gu_h)
	call	atan2_16
	ld	(gu_lat), hl
	ld	hl, (gu_x)		; d = atan2(X, pc)
	ld	de, (gu_pz)
	call	atan2_16
	ld	de, (_ctx + CTX_LON)	; lon = λ0 + d
	add	hl, de
	ld	(gu_vlon), hl
	ld	c, 11 (ix)		; p->lon = lon << 16, p->lat = lat << 16
	ld	b, 12 (ix)
	xor	a, a
	ld	(bc), a
	inc	bc
	ld	(bc), a
	inc	bc
	ld	hl, (gu_vlon)
	ld	a, l
	ld	(bc), a
	inc	bc
	ld	a, h
	ld	(bc), a
	inc	bc
	xor	a, a
	ld	(bc), a
	inc	bc
	ld	(bc), a
	inc	bc
	ld	hl, (gu_lat)
	ld	a, l
	ld	(bc), a
	inc	bc
	ld	a, h
	ld	(bc), a
	pop	ix
	ret

;; HL (int16) · 16384 / BC (> 0) -> HL, деление с усечением к нулю
divq14:
	ld	a, h			; знак
	push	af
	bit	7, h
	call	nz, neg16
	ld	a, l			; HL:DE = |HL| << 14
	ld	e, #0
	ld	d, l
	ld	l, h
	ld	h, #0			; HL:DE = |HL| << 8
	ld	a, #6
1$:	sla	d
	rl	l
	rl	h
	dec	a
	jr	nz, 1$			; << 14
	call	div3216			; DE — частное
	ex	de, hl
	pop	af
	bit	7, a
	ret	z
	jp	neg16

;; HL:DE (без знака, HL < BC) / BC -> DE (частное), HL (остаток); портит A
div3216:
	ld	a, #16
1$:	sla	e
	rl	d
	adc	hl, hl
	jr	c, 2$
	sbc	hl, bc
	jr	nc, 3$
	add	hl, bc
	jr	4$
2$:	or	a, a
	sbc	hl, bc
3$:	inc	e
4$:	dec	a
	jr	nz, 1$
	ret

;; is_v = gu_x² + gu_y² (32 бита)
sq2:
	ld	hl, (gu_x)
	ld	de, (gu_x)
	call	___mulsint2slong
	ld	(is_v), de
	ld	(is_v + 2), hl
	ld	hl, (gu_y)
	ld	de, (gu_y)
	call	___mulsint2slong
	push	hl
	ld	hl, (is_v)
	add	hl, de
	ld	(is_v), hl
	pop	de
	ld	hl, (is_v + 2)
	adc	hl, de
	ld	(is_v + 2), hl
	ret

;; HL = floor(sqrt(is_v)) (is_v портится)
isqrt32:
	ld	hl, #0
	ld	(is_r), hl
	ld	(is_r + 2), hl
	ld	(is_b), hl
	ld	hl, #0x4000
	ld	(is_b + 2), hl
1$:	ld	hl, #is_v		; пока b > v: b >>= 2
	ld	de, #is_b
	call	cmp32
	jr	nc, 2$
	ld	hl, #is_b
	call	shr32
	call	shr32
	jr	1$
2$:	ld	hl, (is_b)		; пока b
	ld	a, h
	or	a, l
	ld	hl, (is_b + 2)
	or	a, h
	or	a, l
	jr	z, 5$
	ld	hl, #is_r		; t = r + b
	ld	de, #is_t
	ld	bc, #4
	ldir
	ld	hl, #is_t
	ld	de, #is_b
	call	add32
	ld	hl, #is_v		; v >= t: v −= t, r = (r >> 1) + b; иначе r >>= 1
	ld	de, #is_t
	call	cmp32
	jr	c, 3$
	call	sub32
	ld	hl, #is_r
	call	shr32
	ld	de, #is_b
	call	add32
	jr	4$
3$:	ld	hl, #is_r
	call	shr32
4$:	ld	hl, #is_b
	call	shr32
	call	shr32
	jr	2$
5$:	ld	hl, (is_r)
	ret

;; CY = (HL) < (DE) — 32 бита без знака; HL, DE сохраняются
cmp32:
	push	hl
	push	de
	ld	bc, #3
	add	hl, bc
	ex	de, hl
	add	hl, bc
	ex	de, hl
	ld	b, #4
1$:	ld	a, (de)
	ld	c, a
	ld	a, (hl)
	cp	a, c
	jr	nz, 2$
	dec	hl
	dec	de
	djnz	1$
	or	a, a
2$:	pop	de
	pop	hl
	ret

;; (HL) += (DE) — 32 бита; HL, DE сохраняются
add32:
	push	hl
	push	de
	ld	b, #4
	or	a, a
1$:	ld	a, (de)
	adc	a, (hl)
	ld	(hl), a
	inc	hl
	inc	de
	djnz	1$
	pop	de
	pop	hl
	ret

;; (HL) −= (DE) — 32 бита; HL, DE сохраняются
sub32:
	push	hl
	push	de
	ld	b, #4
	or	a, a
1$:	ld	a, (hl)
	ex	de, hl
	sbc	a, (hl)
	ex	de, hl
	ld	(hl), a
	inc	hl
	inc	de
	djnz	1$
	pop	de
	pop	hl
	ret

;; (HL) >>= 1 — 32 бита без знака; HL сохраняется
shr32:
	push	hl
	ld	bc, #3
	add	hl, bc
	or	a, a
	ld	b, #4
1$:	rr	(hl)
	dec	hl
	djnz	1$
	pop	hl
	ret

;; Угол вектора (x = DE, y = HL; оба по модулю < 65536) в единицах 65536 = 360° -> HL
;; (atan_tab с линейной интерполяцией)
atan2_16:
	ld	(at_y), hl
	ld	(at_x), de
	bit	7, h			; |y| -> HL (neg16 портит DE — x перечитывается)
	call	nz, neg16
	push	hl
	ld	hl, (at_x)		; |x|
	bit	7, h
	call	nz, neg16
	ex	de, hl			; DE = |x|
	pop	hl			; HL = |y|
	ld	a, h			; оба нуля — 0
	or	a, l
	or	a, d
	or	a, e
	jr	nz, 1$
	ld	hl, #0
	ret
1$:	xor	a, a			; swap = |y| > |x|: num = меньший, den = больший
	ld	(at_sw), a
	push	hl
	or	a, a
	sbc	hl, de
	pop	hl
	jr	z, 2$
	jr	c, 2$
	ex	de, hl
	ld	a, #1
	ld	(at_sw), a
2$:	; HL = num, DE = den (num <= den)
	push	hl
	or	a, a
	sbc	hl, de
	pop	hl
	jr	nz, 3$
	ld	hl, (_atan_tab + 512)	; num = den: t = 65536 — atan_tab[256]
	jr	5$
3$:	ld	b, d			; t = (num << 16) / den (< 65536)
	ld	c, e
	ld	de, #0
	call	div3216			; DE = t
	ld	a, d			; i = t >> 8, f = t & 255
	ld	l, a
	ld	h, #0
	add	hl, hl
	ld	bc, #_atan_tab
	add	hl, bc
	ld	c, (hl)
	inc	hl
	ld	b, (hl)			; BC = atan_tab[i]
	inc	hl
	ld	a, (hl)
	inc	hl
	ld	h, (hl)
	ld	l, a			; HL = atan_tab[i + 1]
	push	bc
	or	a, a
	sbc	hl, bc			; разность (без знака)
	ld	d, #0			; E = f
	call	___muluint2ulong	; HL:DE = разность · f
	ld	h, l			; >> 8
	ld	l, d
	pop	bc
	add	hl, bc
5$:	ld	a, (at_sw)		; swap — 16384 − a
	or	a, a
	jr	z, 6$
	ex	de, hl
	ld	hl, #16384
	or	a, a
	sbc	hl, de
6$:	ld	a, (at_x + 1)		; x < 0 — 32768 − a
	bit	7, a
	jr	z, 7$
	ex	de, hl
	ld	hl, #32768
	or	a, a
	sbc	hl, de
7$:	ld	a, (at_y + 1)		; y < 0 — −a
	bit	7, a
	ret	z
	jp	neg16
