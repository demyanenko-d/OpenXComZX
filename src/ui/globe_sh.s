;; Банк 25: тень глобуса (день и ночь) — OpenXcom Globe::drawShadow (Globe.cpp:176–245,
;; 986–1008) без сезонов, ступенчато по блокам: уровень k = ⌊тень/3⌋ (0..10) один на блок
;; 8x4 пикселя — по центру блока (project_docs/globe.md §6.4, модель tools/globe_shadow.js
;; BLOCK=8x4; оригинал тоже ступенчатый). Перевод globe_sh.c на ассемблер.
;;
;; Вывод. Строка заднего буфера с тенью заранее заполняется: x 0..255 — добавка суши
;; L = ⌊(v_k − шум)/3⌋, x 256..511 — цвет океана O = OCEAN + (v_k − шум) (v_k = 3k + 1, 0 и 31 у
;; крайних уровней). Проход строк (globe_s.s) кладёт сушу BLT2 по полубайтам с насыщением
;; (узор + L = ровно getLandShadow), океан — копией из x + 256.
;;
;; Страницы: shp — образцы уровней 0..7 (по 2 КБ), shp + 1 — уровни 8..10 и таблицы кадра
;; (её возвращает globe_shadow: флаги строк GLOBE_SH_FLG). Уровень блока: 2t = −500·(e·s) =
;; TX[столбец] + TY[строка блоков] + TZ[z центра] — лестницы без умножений; проход блоков и
;; строк — globe_sh_s.s. Здесь — подготовка кадра (солнце, узоры, таблицы).
;;
;; Соглашения — как в globe_view.s: банковые функции берут аргументы из стека (SP + 5 — первый),
;; результат-байт — A, 16 бит — DE; ядро sdcccall(1); IX сохраняется. Нулевое начальное _DATA:
;; «нет значения» — 0 (страница shp, цвет океана) или зум + 1.

	.module globe_sh
	.optsdcc -mz80 sdcccall(1)

	.globl	_globe_rows_pl, _globe_sunlon, _globe_shadow
	.globl	b_globe_rows_pl, b_globe_sunlon, b_globe_shadow
	.globl	_sh_row, _sh_z8, _sh_noise, _sh_lut, _sh_br
	.globl	_sh_ramp, _sh_radr, _sh_rhs, _sh_rn, _sh_rv, _sh_rdv
	.globl	_sh_rows, _sh_zi, _sh_page, _sh_ty, _sh_dty
	.globl	_globe_sin, b_globe_sin
	.globl	_pg_alloc, _pg_map3, ___mulsint2slong, ___muluint2ulong, ___sdcc_bcall_ehl

b_globe_rows_pl	= 25
b_globe_sunlon	= 25
b_globe_shadow	= 25

SH_TX	= 0xB800			; ОЗУ банка (globe_sh_s.s): 2t от столбца блока [32] мл., ст. (+128)
SH_TZ	= 0xBA00			;   2t от z [256] мл., ст. (+256)
SH_LUT	= 0xBC00			;   уровень по 2t + 128 [256]
SH_Z8	= 0xBD00			;   z центров блоков зума [25 × 16]
SH_DBG	= 0xC000 + 0x3FF0		; отладка: солнце кадра sx, sy, sz
NPAT	= 11
PG_NONE	= 0xFF
ST_SEC	= 0xC000 + 0x36			; state_t: секунды, минуты, час (Win3 = страница состояния)
ST_MIN	= 0xC000 + 0x37
ST_HOUR	= 0xC000 + 0x38

	.area	_DATA

_sh_br::	.ds	200		; на строку блоков: min pl, max pr, min pr, max pl (globe_sh_s.s)
shp:		.ds	1		; страница образцов (0 — не выделена)
pat_ocean:	.ds	1		; цвет океана построенных образцов (0 — не строились)
z8_zp1:		.ds	1		; зум + 1 таблиц SH_Z8 и sh_br (0 — ничьи)
gs_old:		.ds	1
gs_sd:		.ds	2		; солнце: sin, cos (λs − λ0), sin, cos наклона
gs_cd:		.ds	2
gs_sc:		.ds	2
gs_cc:		.ds	2
gs_sx:		.ds	2		; солнце в осях вида (Q14)
gs_sy:		.ds	2
gs_sz:		.ds	2
gs_k:		.ds	2		; множители зума (zk)
pt_k:		.ds	1		; образцы: уровень, значение, цвет океана, начало образца, lv[4], ov[4]
pt_v:		.ds	1
pt_oc:		.ds	1
pt_p:		.ds	2
pt_lv:		.ds	8
bf_mnl:		.ds	1		; пределы 4 строк блока
bf_mxr:		.ds	1
bf_mnr:		.ds	1
bf_mxl:		.ds	1

	.area	_BANK25

;; 2t = −500·(sx·X + sy·Y + sz·Z) / (R·16384): множители по зумам (деление 32 бит в SDCC стоило
;; ~11 400 тактов). 1984000/r и 1568000/r, шаги — с 16-кратной точностью: 2048000/r и 1024000/r
zk:	.dw	22044, 22756, 17422, 11378, 16533, 17067, 13066, 8533, 11022, 11378, 8711, 5689
	.dw	7085, 7314, 5600, 3657, 4408, 4551, 3484, 2276, 2755, 2844, 2177, 1422

;; ================================================================ пары диска, время

;; void globe_rows_pl(uint8_t zoom, uint8_t *dst) __banked — пары диска [pl, pr] строк зума в
;; записи строк (6 байт на строку; банк 24 читает таблицу этим вызовом)
_globe_rows_pl::
	ld	hl, #5
	add	hl, sp
	ld	a, (hl)
	inc	hl
	ld	e, (hl)
	inc	hl
	ld	d, (hl)
	ld	hl, #_sh_row
	call	zoom400
	ld	b, #200
1$:	ld	a, (hl)
	ld	(de), a
	inc	hl
	inc	de
	ld	a, (hl)
	ld	(de), a
	inc	hl
	inc	de
	inc	de
	inc	de
	inc	de
	inc	de
	djnz	1$
	ret

;; HL += A · 400 (A — зум), портит A, BC
zoom400:
	or	a, a
	ret	z
	ld	bc, #400
1$:	add	hl, bc
	dec	a
	jr	nz, 1$
	ret

;; uint16_t globe_sunlon(void) __banked — Globe::getSunDirection без сезонов:
;; λs = #4000 − ((час + 18) mod 24 · 3600 + мин · 60 + сек) · 65536 / 86400 (ST в Win3)
_globe_sunlon::
	ld	a, (ST_HOUR)
	add	a, #18
	cp	a, #24
	jr	c, 1$
	sub	a, #24
1$:	ld	l, a			; sec = h · 3600 + m · 60 + s (до 86399, 17 бит)
	ld	h, #0
	ld	de, #3600
	call	___muluint2ulong
	push	hl
	push	de
	ld	a, (ST_MIN)
	ld	l, a
	ld	h, #0
	ld	de, #60
	call	___muluint2ulong
	ld	a, (ST_SEC)
	add	a, e
	ld	e, a
	jr	nc, 2$
	inc	d
2$:	pop	hl
	add	hl, de
	pop	de
	jr	nc, 3$
	inc	de
3$:	push	de			; x = sec · 3107 (* 65536 / 86400 ≈ 3107 / 4096)
	ld	de, #3107
	call	___muluint2ulong
	pop	bc
	ld	a, c
	or	a, a
	jr	z, 4$
	ld	bc, #3107
	add	hl, bc
4$:	ld	b, #4			; day = (x >> 12) & #FFFF: биты 12..27
5$:	srl	h
	rr	l
	rr	d
	rr	e
	djnz	5$
	ld	e, d
	ld	d, l
	ld	hl, #0x4000
	or	a, a
	sbc	hl, de
	ex	de, hl
	ret

;; ================================================================ кадр

;; uint8_t globe_shadow(uint16_t lon, int16_t lat, uint8_t zoom, uint16_t sunlon, uint8_t ocean)
;; __banked — тень кадра; возвращает страницу с флагами строк (shp + 1) или PG_NONE
_globe_shadow::
	push	ix
	ld	ix, #0
	add	ix, sp			; +7 lon, +9 lat, +11 zoom, +12 sunlon, +14 ocean
	ld	a, (shp)
	or	a, a
	jr	nz, 1$
	ld	l, #1
	ld	a, #2
	call	_pg_alloc
	cp	a, #PG_NONE
	jr	nz, 2$
	pop	ix
	ret
2$:	ld	(shp), a
1$:	inc	a
	call	_pg_map3
	ld	(gs_old), a
	ld	a, (pat_ocean)
	cp	a, 14 (ix)
	jr	z, 3$
	ld	a, 14 (ix)
	call	patterns		; оставляет Win3 = shp + 1
3$:	ld	l, 12 (ix)		; солнце в осях вида: s = (sin d, −sinC·cos d, cosC·cos d), d = λs − λ0
	ld	h, 13 (ix)
	ld	e, 7 (ix)
	ld	d, 8 (ix)
	or	a, a
	sbc	hl, de
	push	hl
	call	sin16
	ld	(gs_sd), de
	pop	hl
	ld	de, #0x4000
	add	hl, de
	call	sin16
	ld	(gs_cd), de
	ld	l, 9 (ix)
	ld	h, 10 (ix)
	push	hl
	call	sin16
	ld	(gs_sc), de
	pop	hl
	ld	de, #0x4000
	add	hl, de
	call	sin16
	ld	(gs_cc), de
	ld	hl, (gs_sd)
	ld	(gs_sx), hl
	ld	hl, (gs_sc)		; sy = −SHR14(sinC · cos d)
	ld	de, (gs_cd)
	call	___mulsint2slong
	call	shr14
	ex	de, hl
	ld	hl, #0
	or	a, a
	sbc	hl, de
	ld	(gs_sy), hl
	ld	hl, (gs_cc)		; sz = SHR14(cosC · cos d)
	ld	de, (gs_cd)
	call	___mulsint2slong
	call	shr14
	ld	(gs_sz), hl
	ld	a, 11 (ix)		; множители зума
	add	a, a
	add	a, a
	add	a, a
	ld	e, a
	ld	d, #0
	ld	hl, #zk
	add	hl, de
	ld	(gs_k), hl
	; TX: 32 значения, v = (k0 · sx) >> 3, шаг (−(k1 · sx)) >> 7
	ld	e, (hl)
	inc	hl
	ld	d, (hl)
	ex	de, hl
	ld	de, (gs_sx)
	call	___mulsint2slong
	ld	b, #3
	call	sar32
	ld	(_sh_rv), de
	ld	(_sh_rv + 2), hl
	ld	hl, (gs_k)
	inc	hl
	inc	hl
	ld	e, (hl)
	inc	hl
	ld	d, (hl)
	ex	de, hl
	ld	de, (gs_sx)
	call	___mulsint2slong
	call	neg32
	ld	b, #7
	call	sar32
	ld	(_sh_rdv), de
	ld	(_sh_rdv + 2), hl
	ld	hl, #SH_TX
	ld	(_sh_radr), hl
	ld	hl, #128
	ld	(_sh_rhs), hl
	ld	hl, #32
	ld	(_sh_rn), hl
	call	_sh_ramp
	; TZ: 256 значений от 0, шаг −((sz · 2008) >> 8) (2000/255 ≈ 2008/256)
	ld	hl, (gs_sz)
	ld	de, #2008
	call	___mulsint2slong
	ld	b, #8
	call	sar32
	call	neg32
	ld	(_sh_rdv), de
	ld	(_sh_rdv + 2), hl
	ld	hl, #0
	ld	(_sh_rv), hl
	ld	(_sh_rv + 2), hl
	ld	hl, #SH_TZ
	ld	(_sh_radr), hl
	ld	hl, #256
	ld	(_sh_rhs), hl
	ld	(_sh_rn), hl
	call	_sh_ramp
	; TY строки блоков 0: (k2 · sy) >> 3 + #18000, шаг (−(k3 · sy)) >> 7
	ld	hl, (gs_k)
	ld	de, #4
	add	hl, de
	ld	e, (hl)
	inc	hl
	ld	d, (hl)
	ex	de, hl
	ld	de, (gs_sy)
	call	___mulsint2slong
	ld	b, #3
	call	sar32
	ld	a, d
	add	a, #0x80
	ld	d, a
	ld	a, l
	adc	a, #1
	ld	l, a
	ld	a, h
	adc	a, #0
	ld	h, a
	ld	(_sh_ty), de
	ld	(_sh_ty + 2), hl
	ld	hl, (gs_k)
	ld	de, #6
	add	hl, de
	ld	e, (hl)
	inc	hl
	ld	d, (hl)
	ex	de, hl
	ld	de, (gs_sy)
	call	___mulsint2slong
	call	neg32
	ld	b, #7
	call	sar32
	ld	(_sh_dty), de
	ld	(_sh_dty + 2), hl
	; z центров блоков и пределы строк блоков — при смене зума
	ld	a, (z8_zp1)
	dec	a
	cp	a, 11 (ix)
	jr	z, 6$
	ld	a, 11 (ix)
	ld	hl, #_sh_z8
	call	zoom400
	ld	de, #SH_Z8
	ld	bc, #400
	ldir
	ld	a, 11 (ix)
	call	br_fill
	ld	a, 11 (ix)
	inc	a
	ld	(z8_zp1), a
6$:	ld	a, 11 (ix)		; sh_zi = зум · 400
	ld	hl, #0
	call	zoom400
	ld	(_sh_zi), hl
	ld	a, (shp)
	ld	(_sh_page), a
	ld	hl, (gs_sx)
	ld	(SH_DBG), hl
	ld	hl, (gs_sy)
	ld	(SH_DBG + 2), hl
	ld	hl, (gs_sz)
	ld	(SH_DBG + 4), hl
	call	_sh_rows
	ld	a, (gs_old)
	call	_pg_map3
	ld	a, (shp)
	inc	a
	pop	ix
	ret

;; HL — угол -> DE = globe_sin(HL) (Q14, банк 2)
sin16:
	push	hl
	ld	e, #b_globe_sin
	ld	hl, #_globe_sin
	call	___sdcc_bcall_ehl
	pop	hl
	ret

;; HL:DE (int32) -> HL = старшее слово (HL:DE << 2) — SHR14 из C
shr14:
	sla	d
	rl	l
	rl	h
	sla	d
	rl	l
	rl	h
	ret

;; HL:DE >>= B (арифметически)
sar32:
	sra	h
	rr	l
	rr	d
	rr	e
	djnz	sar32
	ret

;; HL:DE = −HL:DE
neg32:
	xor	a, a
	sub	a, e
	ld	e, a
	ld	a, #0
	sbc	a, d
	ld	d, a
	ld	a, #0
	sbc	a, l
	ld	l, a
	ld	a, #0
	sbc	a, h
	ld	h, a
	ret

;; Пределы 4 строк каждого блока (A — зум) -> _sh_br: min pl, max pr, min pr, max pl
br_fill:
	ld	hl, #_sh_row
	call	zoom400
	ld	de, #_sh_br
	ld	c, #50
1$:	ld	a, #0xFF
	ld	(bf_mnl), a
	ld	(bf_mnr), a
	xor	a, a
	ld	(bf_mxr), a
	ld	(bf_mxl), a
	ld	b, #4
2$:	ld	a, (hl)			; pl
	inc	hl
	push	hl
	ld	hl, #bf_mnl
	cp	a, (hl)
	jr	nc, 3$
	ld	(hl), a
3$:	ld	hl, #bf_mxl
	cp	a, (hl)
	jr	c, 4$
	jr	z, 4$
	ld	(hl), a
4$:	pop	hl
	ld	a, (hl)			; pr
	inc	hl
	push	hl
	ld	hl, #bf_mxr
	cp	a, (hl)
	jr	c, 5$
	jr	z, 5$
	ld	(hl), a
5$:	ld	hl, #bf_mnr
	cp	a, (hl)
	jr	nc, 6$
	ld	(hl), a
6$:	pop	hl
	djnz	2$
	ld	a, (bf_mnl)
	ld	(de), a
	inc	de
	ld	a, (bf_mxr)
	ld	(de), a
	inc	de
	ld	a, (bf_mnr)
	ld	(de), a
	inc	de
	ld	a, (bf_mxl)
	ld	(de), a
	inc	de
	dec	c
	jr	nz, 1$
	ret

;; Образцы уровней 0..10 (A — цвет океана): уровень k — страница shp + k / 8, смещение (k % 8) ·
;; 2048, 4 строки по 512: x — L = ⌊s/3⌋, x + 256 — OCEAN + s, s = v_k − шум (не меньше 0);
;; затем таблица уровня по 2t в shp + 1. Шум — таблицей sh_noise (4 строки по 256).
patterns:
	ld	(pt_oc), a
	xor	a, a
	ld	(pt_k), a
pt_lvl:
	ld	a, (pt_k)
	srl	a
	srl	a
	srl	a
	ld	hl, #shp
	add	a, (hl)
	call	_pg_map3
	ld	a, (pt_k)
	and	a, #7
	add	a, a
	add	a, a
	add	a, a
	add	a, #0xC0
	ld	(pt_p + 1), a
	xor	a, a
	ld	(pt_p), a
	ld	a, (pt_k)		; v = k == 0 ? 0 : k >= 10 ? 31 : 3k + 1
	or	a, a
	jr	z, 1$
	cp	a, #10
	jr	c, 2$
	ld	a, #31
	jr	1$
2$:	ld	b, a
	add	a, a
	add	a, b
	inc	a
1$:	ld	(pt_v), a
	ld	c, #0			; по шуму n = 0..3: lv[n] = s / 3, ov[n] = ocean + s
	ld	hl, #pt_lv
3$:	ld	a, (pt_v)
	cp	a, c
	jr	z, 4$
	jr	c, 4$
	sub	a, c
	jr	5$
4$:	xor	a, a
5$:	ld	b, a
	ld	d, #0
6$:	cp	a, #3
	jr	c, 7$
	sub	a, #3
	inc	d
	jr	6$
7$:	ld	(hl), d
	push	hl
	ld	de, #4
	add	hl, de
	ld	a, (pt_oc)
	add	a, b
	ld	(hl), a
	pop	hl
	inc	hl
	inc	c
	ld	a, c
	cp	a, #4
	jr	c, 3$
	ld	hl, (pt_p)		; 4 строки: p[x] = lv[шум], p[x + 256] = ov[шум]
	ld	de, #_sh_noise
	ld	c, #4
8$:	ld	b, #0
9$:	ld	a, (de)
	inc	de
	push	de
	push	hl
	ld	e, a
	ld	d, #0
	ld	hl, #pt_lv
	add	hl, de
	ld	a, (hl)
	inc	hl
	inc	hl
	inc	hl
	inc	hl
	ld	e, (hl)
	pop	hl
	ld	(hl), a
	inc	h
	ld	(hl), e
	dec	h
	inc	hl
	pop	de
	djnz	9$
	inc	h
	dec	c
	jr	nz, 8$
	ld	a, (pt_k)
	inc	a
	ld	(pt_k), a
	cp	a, #NPAT
	jp	c, pt_lvl
	ld	a, (shp)
	inc	a
	call	_pg_map3
	ld	hl, #_sh_lut
	ld	de, #SH_LUT
	ld	bc, #256
	ldir
	ld	a, (pt_oc)
	ld	(pat_ocean), a
	ret
