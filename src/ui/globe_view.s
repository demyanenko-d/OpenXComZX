;; Банк 25: предрасчитанные виды глобуса с SD (GVIEW.PAK, конвертер Core/GlobeViews.cs;
;; project_docs/globe.md §12.5). На зумах 0–2 вид берётся готовым: на каждую из 200 строк —
;; число отрезков и пары (длина в парах − 1, текстура 0..13). Геометрии в кадре нет вовсе:
;; остаются тень, вывод отрезков (globe_s.s _gl_rows_pre) и копия на экран.
;;
;; Сетка видов: поворот i из nLon (угол i · 65536 / nLon), наклон j из nTilt (угол
;; (j − (nTilt − 1) / 2) · tstep); наклон ограничен ±27°, зумы 3–5 остаются рёберному рендеру.
;; Поток вида читается прямо в страницу рёбер с EP_VIEW, указатели зума — с EP_VIDX; рёберный
;; рендер их затирает, поэтому globe_f.s сбрасывает кэш через gview_reset.
;;
;; Соглашения. Банковые функции (__banked): аргументы — в стеке (на входе SP + 5 — первый),
;; стек чистит вызывающий, результат-байт — в A. Вызовы банка 12 (fat_*) — так же, через
;; ___sdcc_bcall_ehl. Функции ядра (sdcccall(1)): far_t — в HL:DE (HL — старшее слово), прочие
;; аргументы — в стеке, чистит вызываемая; __mul*int2*long — HL x DE -> HL:DE. IX сохраняется.

	.module globe_view
	.optsdcc -mz80 sdcccall(1)

	.globl	_gview_open, _gview_pick, _gview_load, _gview_reset, _globe_snap
	.globl	b_gview_open, b_gview_pick, b_gview_load, b_gview_reset, b_globe_snap
	.globl	_fat_mount, _fat_open, _fat_read, b_fat_mount, b_fat_open, b_fat_read
	.globl	_far_read, _far_word, _res_game
	.globl	___sdcc_bcall_ehl, ___muluint2ulong, ___mulsint2slong

b_gview_open	= 25
b_gview_pick	= 25
b_gview_load	= 25
b_gview_reset	= 25
b_globe_snap	= 25

EP_VIEW	= 0x1800			; страница рёбер: поток отрезков вида
EP_VIDX	= 0x3400			;   указатели видов зума (u16 на вид)
GV_Z	= 3				; зумов с предрасчётом
HDR	= 8 + GV_Z * 16			; заголовок пакета: 'GVW1', u16 nZoom, zFirst, по 16 байт на зум

	.area	_DATA

gv_state:	.ds	1		; 0 — не открывали, 1 — есть, 2 — нет
gv_izp1:	.ds	1		; зум + 1, чьи указатели лежат в EP_VIDX (0 — ничьи)
gv_curok:	.ds	1		; 1 — в EP_VIEW лежит вид gv_cur
gv_cur:		.ds	2
gv_nlon:	.ds	2 * GV_Z
gv_ntilt:	.ds	2 * GV_Z
gv_tstep:	.ds	2 * GV_Z	; шаг наклона (65536 = 360°)
gv_isec:	.ds	4 * GV_Z	; сектор указателей зума
gv_dsec:	.ds	4 * GV_Z	; сектор данных зума
gv_lon:		.ds	2 * 72 * GV_Z	; углы поворота сетки i · 65536 / nLon (деление один раз)
gvf:		.ds	69		; fat_file_t
gv_path:	.ds	20		; путь пакета в окне 1: литерал банка 25 не виден из банка 12
gv_hdr:		.ds	HDR
gv_ep:		.ds	1		; страница рёбер текущего вызова
gv_z:		.ds	1
gv_iv:		.ds	2
gv_s0:		.ds	2
gv_cnt:		.ds	2
gv_tmp4:	.ds	4
pk_n:		.ds	1		; выбор вида: nLon, k, i, шаг наклона, j
pk_k:		.ds	1
pk_i:		.ds	1
pk_ts:		.ds	2
pk_j:		.ds	2

	.area	_BANK25

gv_ptht:	.ascii	"OXZ/TFTD/GVIEW.PAK"
		.db	0, 0
gv_pthu:	.ascii	"OXZ/UFO/GVIEW.PAK"
		.db	0, 0, 0

;; ================================================================ открытие

;; uint8_t gview_open(uint8_t ep) __banked — 1: пакет открыт и разобран (повторно — ответ из кэша)
_gview_open::
	ld	a, (gv_state)
	or	a, a
	jr	z, go_new
	cp	a, #1
	ld	a, #1
	ret	z
	xor	a, a
	ret
go_new:
	push	ix
	ld	hl, #7
	add	hl, sp
	ld	a, (hl)
	ld	(gv_ep), a
	ld	a, #2
	ld	(gv_state), a
	ld	e, #b_fat_mount
	ld	hl, #_fat_mount
	call	___sdcc_bcall_ehl
	or	a, a
	jp	nz, go_fail
	call	_res_game		; 2 — TFTD
	ld	hl, #gv_pthu
	cp	a, #2
	jr	nz, 1$
	ld	hl, #gv_ptht
1$:	ld	de, #gv_path
	ld	bc, #20
	ldir
	ld	hl, #gvf		; fat_open(gv_path, &gvf)
	push	hl
	ld	hl, #gv_path
	push	hl
	ld	e, #b_fat_open
	ld	hl, #_fat_open
	call	___sdcc_bcall_ehl
	pop	hl
	pop	hl
	or	a, a
	jp	nz, go_fail
	ld	hl, #0			; fat_read(&gvf, 0, FAR(ep, EP_VIDX), HDR)
	push	hl
	ld	hl, #HDR
	push	hl
	ld	de, #EP_VIDX
	call	gv_fard
	push	hl
	push	de
	ld	hl, #0
	push	hl
	push	hl
	ld	hl, #gvf
	push	hl
	ld	e, #b_fat_read
	ld	hl, #_fat_read
	call	___sdcc_bcall_ehl
	ld	hl, #14
	add	hl, sp
	ld	sp, hl
	or	a, a
	jp	nz, go_fail
	ld	hl, #HDR		; far_read(FAR(ep, EP_VIDX), gv_hdr, HDR)
	push	hl
	ld	hl, #gv_hdr
	push	hl
	ld	de, #EP_VIDX
	call	gv_fard
	call	_far_read
	ld	hl, #gv_hdr		; 'GVW1', nZoom >= 3, первый зум 0
	ld	a, (hl)
	cp	a, #0x47
	jp	nz, go_fail
	inc	hl
	ld	a, (hl)
	cp	a, #0x56
	jp	nz, go_fail
	inc	hl
	ld	a, (hl)
	cp	a, #0x57
	jp	nz, go_fail
	inc	hl
	ld	a, (hl)
	cp	a, #0x31
	jp	nz, go_fail
	ld	a, (gv_hdr + 4)
	cp	a, #GV_Z
	jp	c, go_fail
	ld	a, (gv_hdr + 6)
	or	a, a
	jp	nz, go_fail
	xor	a, a
	ld	(gv_z), a
	ld	ix, #gv_hdr + 8
go_zoom:				; запись зума: nLon, nTilt, шаг наклона, R, isec, dsec
	ld	a, (gv_z)
	add	a, a
	ld	e, a
	ld	d, #0			; DE = 2z
	ld	a, 1 (ix)		; nLon: 1..72
	or	a, a
	jp	nz, go_fail
	ld	a, 0 (ix)
	or	a, a
	jp	z, go_fail
	cp	a, #73
	jp	nc, go_fail
	ld	hl, #gv_nlon
	add	hl, de
	ld	(hl), a
	inc	hl
	ld	(hl), #0
	ld	a, 2 (ix)		; nTilt != 0
	or	a, 3 (ix)
	jp	z, go_fail
	ld	hl, #gv_ntilt
	add	hl, de
	ld	a, 2 (ix)
	ld	(hl), a
	inc	hl
	ld	a, 3 (ix)
	ld	(hl), a
	ld	a, 4 (ix)		; шаг наклона != 0
	or	a, 5 (ix)
	jp	z, go_fail
	ld	hl, #gv_tstep
	add	hl, de
	ld	a, 4 (ix)
	ld	(hl), a
	inc	hl
	ld	a, 5 (ix)
	ld	(hl), a
	ex	de, hl
	add	hl, hl
	push	hl			; 4z
	ld	de, #gv_isec
	add	hl, de
	ex	de, hl
	push	ix
	pop	hl
	ld	bc, #8
	add	hl, bc
	ld	bc, #4
	ldir				; isec
	ex	de, hl			; HL — dst isec + 4, DE — запись + 12
	pop	hl			; 4z
	push	de
	ld	de, #gv_dsec
	add	hl, de
	ex	de, hl
	pop	hl
	ld	bc, #4
	ldir				; dsec
	; углы сетки: gv_lon[z][i] = (i · 65536 + n/2) / n; n <= 72 — делитель байт, остаток < n
	ld	hl, #gv_lon
	ld	a, (gv_z)
	or	a, a
	jr	z, 3$
	ld	b, a
	ld	de, #144
2$:	add	hl, de
	djnz	2$
3$:	ld	c, 0 (ix)		; C — n
	ld	b, #0			; B — i
4$:	push	bc
	push	hl
	ld	a, c
	srl	a
	ld	e, a
	ld	d, #0			; DE — младшее слово делимого, в него же вдвигается частное
	ld	h, b			; H — остаток (старшее слово делимого = i < n)
	ld	l, #16
5$:	sla	e
	rl	d
	rl	h
	ld	a, h
	sub	a, c
	jr	c, 6$
	ld	h, a
	inc	e
6$:	dec	l
	jr	nz, 5$
	pop	hl
	ld	(hl), e
	inc	hl
	ld	(hl), d
	inc	hl
	pop	bc
	inc	b
	ld	a, b
	cp	a, c
	jr	c, 4$
	ld	bc, #16
	add	ix, bc
	ld	a, (gv_z)
	inc	a
	ld	(gv_z), a
	cp	a, #GV_Z
	jp	c, go_zoom
	ld	a, #1
	ld	(gv_state), a
	pop	ix
	ret
go_fail:
	pop	ix
	xor	a, a
	ret

;; FAR(gv_ep, DE) -> HL (старшее слово), DE (младшее); DE < #4000
gv_fard:
	ld	a, (gv_ep)
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

;; ================================================================ выбор вида

;; uint8_t gview_pick(uint8_t z, uint16_t *lon, int16_t *lat, uint16_t *iv) __banked —
;; ближайший вид сетки: точные углы вида и его номер (0 — предрасчёта для зума нет)
_gview_pick::
	push	ix
	ld	ix, #0
	add	ix, sp			; +7 z, +8 lon*, +10 lat*, +12 iv*
	ld	a, 7 (ix)
	cp	a, #GV_Z
	jp	nc, pk_no
	ld	a, (gv_state)
	cp	a, #1
	jp	nz, pk_no
	ld	a, 7 (ix)
	add	a, a
	ld	e, a
	ld	d, #0
	ld	hl, #gv_nlon
	add	hl, de
	ld	a, (hl)
	ld	(pk_n), a
	ld	hl, #gv_tstep
	add	hl, de
	ld	a, (hl)
	inc	hl
	ld	h, (hl)
	ld	l, a
	ld	(pk_ts), hl
	ld	hl, #gv_ntilt
	add	hl, de
	ld	a, (hl)
	dec	a
	srl	a
	ld	(pk_k), a		; k = (nTilt − 1) / 2
	ld	l, 8 (ix)		; i = (lon · n + 32768) >> 16
	ld	h, 9 (ix)
	ld	e, (hl)
	inc	hl
	ld	d, (hl)
	ex	de, hl
	ld	a, (pk_n)
	ld	e, a
	ld	d, #0
	call	___muluint2ulong
	ld	a, d
	add	a, #0x80
	ld	a, l
	adc	a, #0
	ld	b, a			; i < 256
	ld	a, (pk_n)
	ld	c, a
	ld	a, b
	cp	a, c
	jr	c, 1$
	sub	a, c			; i >= n — по кругу
1$:	ld	(pk_i), a
	ld	l, 10 (ix)		; j = t >= 0 ? (t + ts/2) / ts : −((ts/2 − t) / ts)
	ld	h, 11 (ix)
	ld	e, (hl)
	inc	hl
	ld	d, (hl)
	ld	hl, (pk_ts)
	srl	h
	rr	l
	bit	7, d
	jr	nz, 2$
	add	hl, de
	ld	de, (pk_ts)
	call	div16
	jr	3$
2$:	or	a, a
	sbc	hl, de
	ld	de, (pk_ts)
	call	div16
	ex	de, hl
	ld	hl, #0
	or	a, a
	sbc	hl, de
3$:	ld	a, (pk_k)		; j в пределах ±k
	ld	e, a
	ld	d, #0
	push	hl
	or	a, a
	sbc	hl, de
	pop	hl
	jp	m, 4$
	jr	z, 4$
	ex	de, hl			; j > k — k
	jr	5$
4$:	push	hl
	or	a, a
	adc	hl, de			; adc, не add: 16-битный add не ставит флаг знака
	pop	hl
	jp	p, 5$
	ld	hl, #0			; j < −k — −k
	or	a, a
	sbc	hl, de
5$:	ld	(pk_j), hl
	ld	hl, #gv_lon		; *lon = gv_lon[z][i]
	ld	a, 7 (ix)
	or	a, a
	jr	z, 7$
	ld	b, a
	ld	de, #144
6$:	add	hl, de
	djnz	6$
7$:	ld	a, (pk_i)
	ld	e, a
	ld	d, #0
	add	hl, de
	add	hl, de
	ld	e, (hl)
	inc	hl
	ld	d, (hl)
	ld	l, 8 (ix)
	ld	h, 9 (ix)
	ld	(hl), e
	inc	hl
	ld	(hl), d
	ld	hl, (pk_j)		; *lat = j · ts
	ld	de, (pk_ts)
	call	___mulsint2slong
	ld	l, 10 (ix)
	ld	h, 11 (ix)
	ld	(hl), e
	inc	hl
	ld	(hl), d
	ld	hl, (pk_j)		; *iv = (j + k) · n + i
	ld	a, (pk_k)
	ld	e, a
	ld	d, #0
	add	hl, de
	ld	a, (pk_n)
	ld	e, a
	call	___muluint2ulong
	ld	a, (pk_i)
	add	a, e
	ld	e, a
	ld	a, d
	adc	a, #0
	ld	d, a
	ld	l, 12 (ix)
	ld	h, 13 (ix)
	ld	(hl), e
	inc	hl
	ld	(hl), d
	ld	a, #1
	pop	ix
	ret
pk_no:
	xor	a, a
	pop	ix
	ret

;; HL = HL / DE без знака (остаток < DE <= 32767), портит A, BC, DE не трогает
div16:
	ld	a, h
	ld	c, l
	ld	hl, #0
	ld	b, #16
1$:	sla	c
	rla
	adc	hl, hl
	sbc	hl, de
	jr	nc, 2$
	add	hl, de
	djnz	1$
	jr	3$
2$:	inc	c
	djnz	1$
3$:	ld	h, a
	ld	l, c
	ret

;; ================================================================ чтение вида

;; uint8_t gview_load(uint8_t ep, uint8_t z, uint16_t iv) __banked — указатели зума и сам вид ->
;; страница рёбер ep (1 — вид в EP_VIEW)
_gview_load::
	push	ix
	ld	ix, #0
	add	ix, sp			; +7 ep, +8 z, +9 iv
	ld	a, 7 (ix)
	ld	(gv_ep), a
	ld	a, (gv_izp1)
	dec	a
	cp	a, 8 (ix)
	jp	z, ld_view
	ld	a, 8 (ix)		; указатели зума: (nLon · nTilt + 1) · 2 байт с сектора isec
	add	a, a
	ld	e, a
	ld	d, #0
	ld	hl, #gv_nlon
	add	hl, de
	ld	a, (hl)
	ld	hl, #gv_ntilt
	add	hl, de
	ld	e, (hl)
	ld	d, #0
	ld	l, a
	ld	h, #0
	call	___muluint2ulong
	inc	de
	ex	de, hl
	add	hl, hl
	ld	de, #0
	push	de			; len
	push	hl
	ld	de, #EP_VIDX		; dst
	call	gv_fard
	push	hl
	push	de
	ld	a, 8 (ix)		; pos = isec[z] << 9
	add	a, a
	add	a, a
	ld	e, a
	ld	d, #0
	ld	hl, #gv_isec
	add	hl, de
	call	shl9
	push	hl
	push	de
	ld	hl, #gvf
	push	hl
	ld	e, #b_fat_read
	ld	hl, #_fat_read
	call	___sdcc_bcall_ehl
	ld	hl, #14
	add	hl, sp
	ld	sp, hl
	or	a, a
	jp	nz, ld_fail
	ld	a, 8 (ix)
	inc	a
	ld	(gv_izp1), a
	xor	a, a
	ld	(gv_curok), a
ld_view:
	ld	a, (gv_curok)		; тот же вид — уже лежит
	or	a, a
	jr	z, 1$
	ld	hl, (gv_cur)
	ld	e, 9 (ix)
	ld	d, 10 (ix)
	or	a, a
	sbc	hl, de
	jr	nz, 1$
	ld	a, #1
	pop	ix
	ret
1$:	xor	a, a
	ld	(gv_curok), a
	ld	l, 9 (ix)		; s0 = указатель[iv], s1 = указатель[iv + 1]
	ld	h, 10 (ix)
	add	hl, hl
	ld	de, #EP_VIDX
	add	hl, de
	push	hl
	ex	de, hl
	call	gv_fard
	call	_far_word
	ld	(gv_s0), de
	pop	de
	inc	de
	inc	de
	call	gv_fard
	call	_far_word
	ld	hl, (gv_s0)
	ex	de, hl			; HL = s1, DE = s0
	or	a, a
	sbc	hl, de
	jp	z, ld_fail
	jp	c, ld_fail
	ld	(gv_cnt), hl		; секторов
	ld	a, 8 (ix)		; (dsec[z] + s0) — в gv_tmp4
	add	a, a
	add	a, a
	ld	e, a
	ld	d, #0
	ld	hl, #gv_dsec
	add	hl, de
	ld	de, #gv_tmp4
	ld	bc, #4
	ldir
	ld	hl, (gv_tmp4)
	ld	de, (gv_s0)
	add	hl, de
	ld	(gv_tmp4), hl
	ld	hl, (gv_tmp4 + 2)
	ld	de, #0
	adc	hl, de
	ld	(gv_tmp4 + 2), hl
	ld	de, (gv_cnt)		; len = секторов << 9
	call	shl9w
	push	hl
	push	de
	ld	de, #EP_VIEW		; dst
	call	gv_fard
	push	hl
	push	de
	ld	hl, #gv_tmp4		; pos
	call	shl9
	push	hl
	push	de
	ld	hl, #gvf
	push	hl
	ld	e, #b_fat_read
	ld	hl, #_fat_read
	call	___sdcc_bcall_ehl
	ld	hl, #14
	add	hl, sp
	ld	sp, hl
	or	a, a
	jr	nz, ld_fail
	ld	l, 9 (ix)
	ld	h, 10 (ix)
	ld	(gv_cur), hl
	ld	a, #1
	ld	(gv_curok), a
	pop	ix
	ret
ld_fail:
	xor	a, a
	pop	ix
	ret

;; (HL) — uint32 v -> HL (старшее), DE (младшее) = v << 9 (старший байт v не нужен: секторов < 2^23)
shl9:
	ld	d, (hl)
	inc	hl
	ld	a, (hl)
	inc	hl
	ld	h, (hl)
	ld	l, a
	ld	e, #0
	sla	d
	rl	l
	rl	h
	ret

;; DE << 9 -> HL (старшее), DE (младшее)
shl9w:
	ld	h, #0
	ld	l, d
	ld	d, e
	ld	e, #0
	sla	d
	rl	l
	rl	h
	ret

;; void gview_reset(void) __banked — рёберный рендер затёр поток и указатели
_gview_reset::
	xor	a, a
	ld	(gv_izp1), a
	ld	(gv_curok), a
	ret

;; uint8_t globe_snap(uint8_t zoom, uint16_t *lon, int16_t *lat) __banked — привязка вида к сетке
;; предрасчёта (scr_geo.c после поворота и смены зума); 1 — углы поправлены
_globe_snap::
	push	ix
	ld	ix, #0
	add	ix, sp			; +7 zoom, +8 lon*, +10 lat*
	ld	hl, #gv_iv
	push	hl
	ld	l, 10 (ix)
	ld	h, 11 (ix)
	push	hl
	ld	l, 8 (ix)
	ld	h, 9 (ix)
	push	hl
	ld	a, 7 (ix)
	push	af
	inc	sp
	ld	e, #b_gview_pick
	ld	hl, #_gview_pick
	call	___sdcc_bcall_ehl
	ld	hl, #7
	add	hl, sp
	ld	sp, hl
	pop	ix
	ret
