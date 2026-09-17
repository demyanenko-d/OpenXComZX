;; Банк 25: проход тени глобуса (src/geoscape/earth/globe_sh.c, globe.md §6.4). Win3 — страница таблиц
;; тени (SH_*, раскладка — globe_sh.c).
;;
;;   _sh_rows — строки блоков 0..49 (блок 8x4 пикселя = 4 пары x 4 строки): уровень блока по его
;;              центру (2t = TX[столбец] + TY + TZ[z8], уровень — SH_LUT; через столбец, между
;;              равными соседями — их уровень: вдоль строки e·s унимодальна), затем отрезки
;;              равных уровней блоков -> DMA из образцов уровней: подкладка суши L в x 0..255,
;;              цвет океана O в x + 256 строки заднего буфера. Общая часть 4 строк [max pl,
;;              min pr] — одно 2D DMA на отрезок (8 пачек: L, O по строкам), края строк —
;;              построчно (2 пачки); флаги строк с тенью.
;;
;; Таблицы уровня (SH_TX, SH_TZL, SH_LUT, SH_Z8, SH_LB) — в хвосте страницы банка с #B800
;; (tools/build.ps1: код банка — до #B800): окно 2 кэшируется, окно 3 — нет.

	.module globe_sh_s
	.optsdcc -mz80 sdcccall(1)

	.globl	_sh_ramp, _sh_radr, _sh_rhs, _sh_rn, _sh_rv, _sh_rdv
	.globl	_sh_rows, add32, blk_eval, build, iv_out, set_num
	.globl	_sh_zi, _sh_page, _sh_ty, _sh_dty, _sh_bh, _sh_nbr
	.globl	_sh_row, _sh_shift, _sh_br

SH_TX	= 0xB800			; ОЗУ банка: 2t от столбца блока, младшие [32], старшие — +128
SH_LB	= 0xB900			;   уровни блоков текущей строки блоков [32]
SH_TZL	= 0xBA00			;   2t от z [256], старшие — +256
SH_LUT	= 0xBC00			;   уровень по 2t + 128 [256]
SH_Z8	= 0xBD00			;   z центров блоков зума [25 × 16]
SH_FLG	= 0xC000 + 0x3000		; флаги строк [200] (страница shp + 2, их копирует банк 24)
BACK_Y	= 280
SCREEN_PAGE = 0x10

	.area	_DATA

_sh_zi:	.ds	2			; зум · 400 (sh_row: pl, pr по строкам)
_sh_page: .ds	1			; страница образцов уровней 0..7 (8..10 — следующая)
_sh_ty:	.ds	4			; 2t центра строки блоков · 65536, шаг
_sh_dty: .ds	4
_sh_radr: .ds	2			; лестница: адрес младших, до старших, число, значение и шаг (16.16)
_sh_rhs: .ds	2
_sh_rn:	.ds	2
_sh_rv:	.ds	4
_sh_rdv: .ds	4
sr_y:	.ds	1
sr_br:	.ds	1			; строка блоков
sr_zr:	.ds	2			; строка z8 в SH_Z8 (четверть, зеркально; кратна 16)
sr_pl:	.ds	1			; отрезок строки (пары)
sr_pr:	.ds	1
sr_cpr:	.ds	1			; общая часть 4 строк: min pr, max pl
sr_cpl:	.ds	1
sr_cnt:	.ds	1
sr_brp:	.ds	2			; указатель в SH_BR
sr_tyy:	.ds	2
sr_any:	.ds	1			; в строке блоков есть уровень > 0
sr_same: .ds	1			; 1 — у 4 строк блока одни пределы (нет краёв)
sr_flg:	.ds	1
sr_rp2:	.ds	1			; строка образца · 2 (SAH)
sr_sax:	.ds	1
sr_num:	.ds	1			; текущий DMANum
sr_dah:	.ds	1
sr_dax:	.ds	1
sr_sh:	.ds	1

	.area	_BANK25

;; ================================================================ лестница

;; _sh_rn значений старшего слова _sh_rv (+= _sh_rdv): младшие байты с _sh_radr, старшие — с
;; _sh_radr + _sh_rhs (таблицы t кадра)
;; Значение и шаг — в регистрах: старшие слова HL и DE, младшие — HL' и DE'; IX — младшие
;; байты таблицы, IY — старшие (+ _sh_rhs), B — счётчик (0 — 256 значений). Было 323 такта
;; на значение (через add32 и переменные), стало ~113.
_sh_ramp::
	push	ix
	push	iy
	ld	bc, (_sh_radr)
	push	bc
	pop	ix
	ld	hl, (_sh_rhs)
	add	hl, bc
	push	hl
	pop	iy
	ld	hl, (_sh_rv + 2)
	ld	de, (_sh_rdv + 2)
	ld	a, (_sh_rn)
	ld	b, a
	exx
	ld	hl, (_sh_rv)
	ld	de, (_sh_rdv)
	exx
1$:	ld	a, l
	ld	0 (ix), a
	ld	a, h
	ld	0 (iy), a
	inc	ix
	inc	iy
	exx				; значение += шаг (32 бита)
	add	hl, de
	exx
	adc	hl, de
	djnz	1$
	pop	iy
	pop	ix
	ret

;; (HL) += (DE), 32 бита
add32:
	ld	a, (de)
	add	a, (hl)
	ld	(hl), a
	inc	hl
	inc	de
	ld	a, (de)
	adc	a, (hl)
	ld	(hl), a
	inc	hl
	inc	de
	ld	a, (de)
	adc	a, (hl)
	ld	(hl), a
	inc	hl
	inc	de
	ld	a, (de)
	adc	a, (hl)
	ld	(hl), a
	ret

;; ================================================================ блоки и строки

;; Уровень блока столбца C -> SH_LB[C], IXL |= уровень (портит A, DE, HL; BC цел).
;; Постоянные строки блоков вписаны в код (_sh_rows): ty + 128 и адрес строки z8.
blk_eval:
	ld	l, c			; 2t + 128 = tx[c] + (ty + 128) + tz[z8]
	ld	h, #>SH_TX
	ld	e, (hl)
	set	7, l
	ld	d, (hl)
	.db	0x21			; ld hl, #ty + 128
be_ty:	.dw	0
	add	hl, de
	ld	a, c			; z8 столбца: c' = c < 16 ? c : 31 − c
	cp	a, #16
	jr	c, 1$
	cpl
	add	a, #32
1$:	.db	0xF6			; or a, #младший байт строки z8
be_zl:	.db	0
	ld	e, a
	.db	0x16			; ld d, #старший
be_zh:	.db	0
	ld	a, (de)
	ld	e, a
	ld	d, #>SH_TZL
	ld	a, (de)
	add	a, l
	ld	l, a
	inc	d
	ld	a, (de)
	adc	a, h
	jr	nz, 3$			; 2t + 128 вне 0..255 — 0 / 10
	ld	h, #>SH_LUT
	ld	a, (hl)
2$:	ld	l, c
	ld	h, #>SH_LB
	ld	(hl), a
	.db	0xDD, 0xB5		; or a, ixl
	.db	0xDD, 0x6F		; ld ixl, a
	ret
3$:	rla
	ld	a, #0
	jr	c, 2$
	ld	a, #10
	jr	2$

;; Пределы 2t строки блоков по столбцам C..B (sr_zr, sr_tyy готовы): tx и tz — лестницы (монотонны),
;; z центра блока растёт к столбцу 15 -> крайние 2t — на концах. A = 0 — строка смешанная, 1 — вся
;; день (2t < −128: blk_eval дал бы 0), 2 — вся ночь (2t ≥ 128: 10). BC сохраняется.
row_rng:
	push	bc
	ld	h, #>SH_TX		; tx[c0], tx[c1]
	ld	l, c
	ld	e, (hl)
	set	7, l
	ld	d, (hl)
	ld	l, b
	ld	a, (hl)
	set	7, l
	ld	h, (hl)
	ld	l, a
	call	minmax
	ld	(rr_lo), de
	ld	(rr_hi), hl
	ld	a, c			; z: наименьший — у столбца, дальнего от центра, наибольший — столбец 15
	call	mirror
	ld	d, a
	ld	a, b
	call	mirror
	cp	a, d
	jr	c, 1$
	ld	a, d
1$:	ld	hl, (sr_zr)
	or	a, l
	ld	l, a
	ld	e, (hl)			; E — z наименьший
	or	a, #15
	ld	l, a
	ld	l, (hl)			; tz(z наибольший) -> HL
	ld	h, #>SH_TZL
	ld	a, (hl)
	inc	h
	ld	h, (hl)
	ld	l, a
	ld	d, #>SH_TZL		; tz(z наименьший) -> DE
	ld	a, (de)
	ld	c, a
	inc	d
	ld	a, (de)
	ld	d, a
	ld	e, c
	call	minmax
	ld	bc, (sr_tyy)
	add	hl, bc			; наибольшее 2t + 128 < 0 — день
	push	de
	ld	de, (rr_hi)
	add	hl, de
	ld	de, #128
	add	hl, de
	pop	de
	bit	7, h
	ld	a, #1
	jr	nz, 3$
	ex	de, hl			; наименьшее 2t + 128 ≥ 256 — ночь
	add	hl, bc
	ld	de, (rr_lo)
	add	hl, de
	ld	de, #128
	add	hl, de
	xor	a, a
	bit	7, h
	jr	nz, 3$
	or	a, h
	jr	z, 3$
	ld	a, #2
3$:	pop	bc
	ret

;; DE, HL -> DE = меньшее, HL = большее (со знаком; |значения| < 16384)
minmax:
	push	hl
	or	a, a
	sbc	hl, de
	pop	hl
	ret	p
	ex	de, hl
	ret

;; A — столбец блоков -> столбец четверти (c < 16 ? c : 31 − c)
mirror:
	cp	a, #16
	ret	c
	cpl
	add	a, #32
	ret

;; DMANum = A (если сменился — дождаться конца прошлого DMA)
set_num:
	ld	hl, #sr_num
	cp	a, (hl)
	ret	z
	ld	(hl), a
	ld	e, a
	ld	bc, #0x27AF
1$:	in	a, (c)
	jp	m, 1$
	ld	b, #0x28
	out	(c), e
	ret

_sh_rows::
	push	ix
	push	iy
	xor	a, a
	ld	(sr_y), a
	ld	(sr_br), a
	ld	iy, #_sh_row		; IY — строка таблицы: pl, pr
	ld	de, (_sh_zi)
	add	iy, de
	ld	a, #0xFF
	ld	(sr_num), a
	ld	hl, #_sh_br
	ld	(sr_brp), hl
br_loop:
	; 4 строки блока: C — наименьшая pl, B — наибольшая pr (столбцы блоков), D — наибольшая
	; pl, E — наименьшая pr (общая часть; строки нет: 255, 0 — общей части нет). Пределы
	; зависят только от зума — готовы в _sh_br (ОЗУ окна 1, globe_sh.c при смене зума)
	ld	hl, (sr_brp)
	ld	c, (hl)			; min pl
	inc	hl
	ld	b, (hl)			; max pr
	inc	hl
	ld	e, (hl)			; min pr
	inc	hl
	ld	d, (hl)			; max pl
	inc	hl
	ld	(sr_brp), hl
	ld	(sr_cpr), de		; sr_cpr = E, sr_cpl = D
	xor	a, a
	ld	(sr_any), a
	ld	a, c			; у 4 строк одни пределы — краёв нет (sr_same)
	cp	a, d
	jr	nz, 5$
	ld	a, b
	cp	a, e
5$:	ld	a, #0
	jr	nz, 6$
	inc	a
6$:	ld	(sr_same), a
	ld	a, b
	cp	a, c
	jp	c, br_rows		; диска в строке блоков нет
	; строка z8: зеркально b' = br < (nbr + 1) / 2 ? br : nbr − 1 − br; адрес SH_Z8 + b'·16
	ld	a, (_sh_nbr)
	ld	e, a
	inc	a
	srl	a
	ld	d, a
	ld	a, (sr_br)
	cp	a, d
	jr	c, 7$
	cpl
	add	a, e			; nbr − 1 − br
7$:	ld	l, a
	ld	h, #0
	add	hl, hl
	add	hl, hl
	add	hl, hl
	add	hl, hl
	ld	de, #SH_Z8
	add	hl, de
	ld	(sr_zr), hl
	ld	hl, (_sh_ty + 2)
	ld	(sr_tyy), hl
	; столбцы блоков c0 = pl >> 2 .. c1 = pr >> 2: чётные от c0 и c1, затем между ними
	srl	b
	srl	b
	srl	c
	srl	c
	call	row_rng			; вся строка блоков одного края — уровни не считать
	or	a, a
	jr	z, 11$
	dec	a
	jp	z, br_rows		; день: sr_any = 0
	ld	a, #10			; ночь: LB[c0..c1] = 10
	ld	(sr_any), a
	ld	h, #>SH_LB
	ld	l, c
12$:	ld	(hl), a
	ld	a, l
	cp	a, b
	ld	a, #10
	inc	l
	jr	c, 12$
	jr	blk_done
11$:	ld	hl, (sr_tyy)		; постоянные строки — в код blk_eval
	ld	de, #128
	add	hl, de
	ld	(be_ty), hl
	ld	hl, (sr_zr)
	ld	a, l
	ld	(be_zl), a
	ld	a, h
	ld	(be_zh), a
	.db	0xDD, 0x2E, 0		; ld ixl, #0 — ИЛИ уровней строки
	push	bc
blk_even:
	call	blk_eval
	inc	c
	inc	c
	ld	a, b
	cp	a, c
	jr	nc, blk_even
	inc	a			; c1 не попал (c = c1 + 1) — отдельно
	cp	a, c
	jr	nz, 8$
	dec	c
	call	blk_eval
8$:	pop	bc
	ld	a, b			; нечётные от c0 + 1 до c1 − 1: (c1 − c0) / 2 штук
	sub	a, c
	rra
	jr	z, blk_fin
	ld	b, a
	ld	h, #>SH_LB
	ld	l, c			; L — c − 1
blk_odd:
	ld	a, (hl)			; LB[c − 1]
	inc	l
	inc	l
	cp	a, (hl)			; LB[c + 1]
	jr	nz, 9$
	dec	l
	ld	(hl), a			; соседи равны — их уровень
	inc	l
	djnz	blk_odd
	jr	blk_fin
9$:	dec	l
	ld	c, l
	call	blk_eval
	ld	h, #>SH_LB
	ld	l, c
	inc	l
	djnz	blk_odd
blk_fin:
	.db	0xDD, 0x7D		; ld a, ixl
	ld	(sr_any), a
blk_done:
	ld	a, (sr_any)
	or	a, a
	jr	z, br_rows		; вся строка блоков — день
	ld	a, (sr_y)		; сдвиг образца строки блоков
	ld	e, a
	ld	d, #0
	ld	hl, #_sh_shift
	add	hl, de
	ld	a, (hl)
	ld	(sr_sh), a
	ld	a, (sr_cpl)		; общая часть — 2D на 4 строки
	ld	c, a
	ld	a, (sr_cpr)
	cp	a, c
	jr	c, br_rows
	ld	(sr_pr), a
	ld	a, c
	ld	(sr_pl), a
	xor	a, a
	ld	(sr_rp2), a
	ld	a, (_sh_bh)		; DMANum = 2 · строк − 1
	add	a, a
	dec	a
	call	set_num
	call	build
br_rows:
	ld	a, (sr_same)		; краёв нет: флаги строк блока = есть тень
	or	a, a
	jr	z, br_slow
	ld	a, (sr_any)
	or	a, a
	jr	z, 1$
	ld	a, #1
1$:	ld	c, a
	ld	a, (_sh_bh)
	ld	b, a
	add	a, a
	ld	e, a
	ld	d, #0
	add	iy, de			; IY += 2 · строк
	ld	a, (sr_y)
	ld	l, a
	add	a, b
	ld	(sr_y), a
	ld	h, #>SH_FLG
2$:	ld	(hl), c
	inc	l
	djnz	2$
	jp	br_next
br_slow:
	ld	a, (_sh_bh)		; строки блока: края и флаги
	ld	b, a
row_loop:
	push	bc
	xor	a, a
	ld	(sr_flg), a
	ld	a, (sr_any)
	or	a, a
	jr	z, row_next		; вся строка блоков — день
	ld	a, 0 (iy)
	ld	c, a
	ld	a, 1 (iy)
	cp	a, c
	jr	c, row_next		; строки диска нет
	ld	a, #1
	ld	(sr_flg), a
	ld	a, (_sh_bh)		; строка образца = y & (строк − 1)
	dec	a
	ld	e, a
	ld	a, (sr_y)
	and	a, e
	add	a, a
	ld	(sr_rp2), a
	ld	a, (sr_cpl)
	ld	e, a
	ld	a, (sr_cpr)
	cp	a, e
	jr	nc, 13$
	ld	a, 0 (iy)		; общей части нет — вся строка
	ld	(sr_pl), a
	ld	a, 1 (iy)
	ld	(sr_pr), a
	call	edge
	jr	row_next
13$:	ld	a, 0 (iy)		; левый край [pl, cpl − 1]
	cp	a, e
	jr	nc, 14$
	ld	(sr_pl), a
	ld	a, e
	dec	a
	ld	(sr_pr), a
	call	edge
14$:	ld	a, (sr_cpr)		; правый край [cpr + 1, pr]
	ld	e, a
	ld	a, 1 (iy)
	cp	a, e
	jr	z, row_next
	jr	c, row_next
	ld	(sr_pr), a
	ld	a, e
	inc	a
	ld	(sr_pl), a
	call	edge
row_next:
	ld	a, (sr_y)		; флаг строки
	ld	l, a
	ld	h, #>SH_FLG
	ld	a, (sr_flg)
	ld	(hl), a
	inc	iy
	inc	iy
	ld	hl, #sr_y
	inc	(hl)
	pop	bc
	djnz	row_loop
br_next:
	ld	hl, #_sh_ty		; ty += dty (строка блоков)
	ld	de, #_sh_dty
	call	add32
	ld	a, (_sh_nbr)
	ld	b, a
	ld	a, (sr_br)
	inc	a
	ld	(sr_br), a
	cp	a, b
	jp	c, br_loop
	xor	a, a			; дождаться DMA, DMANum = 0
	call	set_num
	pop	iy
	pop	ix
	ret

;; Край строки: одна строка (2 пачки)
edge:
	ld	a, #1
	call	set_num
	; дальше build

;; Отрезки [sr_pl, sr_pr] по уровням блоков (смена — на границе блока, пара 4c) и DMA
build:
	ld	a, (sr_y)		; строка экрана 280 + y: DAH = (row & 31) << 1, DAX = 16 + row >> 5
	ld	l, a
	ld	h, #0
	ld	de, #BACK_Y
	add	hl, de
	ld	a, l
	and	a, #31
	add	a, a
	ld	(sr_dah), a
	add	hl, hl
	add	hl, hl
	add	hl, hl			; row << 3: старший байт = row >> 5
	ld	a, h
	add	a, #SCREEN_PAGE
	ld	(sr_dax), a
	ld	a, (sr_pr)		; E — столбцов после первого: pr >> 2 − pl >> 2
	rrca
	rrca
	and	a, #0x3F
	ld	e, a
	ld	a, (sr_pl)
	ld	d, a			; D — начало отрезка
	rrca
	rrca
	and	a, #0x3F
	ld	l, a			; HL — уровень столбца
	ld	h, #>SH_LB
	ld	b, (hl)			; B — уровень отрезка
	ld	a, e
	sub	a, l
	jr	z, 3$
	ld	e, a
1$:	inc	l
	ld	a, (hl)
	cp	a, b
	jr	z, 2$
	push	hl
	push	af
	ld	a, l			; C — первая пара столбца
	add	a, a
	add	a, a
	ld	c, a
	call	iv_out			; [D, C) уровня B -> DMA (BC, DE целы)
	pop	af
	pop	hl
	ld	b, a
	ld	d, c
2$:	dec	e
	jr	nz, 1$
3$:	ld	a, (sr_pr)		; последний отрезок — до pr + 1
	inc	a
	ld	c, a
	jp	iv_out

;; Отрезок [D, C) уровня B -> сразу DMA: источник — образец уровня k (страница _sh_page + k / 4,
;; смещение (k % 4)·4096 + строка·512: L, O через 256) со сдвигом строки блоков, приёмник — строка
;; sr_y заднего буфера (x, x + 256; DAH, DAX — build); DMANum — set_num. Ждёт конца прошлой
;; передачи, восемь записей в порты. BC, DE целы.
iv_out:
	push	bc
	push	de
	ld	a, c			; DMALen = слов − 1
	sub	a, d
	dec	a
	ex	af, af'
	ld	a, d			; D — DAL = 2 · начало
	add	a, a
	ld	d, a
	ld	a, b			; E — SAX = страница образцов + уровень / 4
	rrca
	rrca
	and	a, #3
	ld	hl, #_sh_page
	add	a, (hl)
	ld	e, a
	ld	a, b			; H — SAH = (уровень % 4) · 16 + строка образца · 2
	and	a, #3
	add	a, a
	add	a, a
	add	a, a
	add	a, a
	ld	hl, #sr_rp2
	add	a, (hl)
	ld	h, a
	ld	a, (sr_sh)		; L — SAL = сдвиг образца + DAL (заворот внутри 256 байт образца)
	add	a, d
	ld	l, a
	ld	bc, #0x27AF
1$:	in	a, (c)			; дождаться конца прошлой передачи
	jp	m, 1$
	ld	b, #0x1A
	out	(c), l			; SAL
	inc	b
	out	(c), h			; SAH
	inc	b
	out	(c), e			; SAX
	inc	b
	out	(c), d			; DAL
	inc	b
	ld	a, (sr_dah)
	out	(c), a			; DAH
	inc	b
	ld	a, (sr_dax)
	out	(c), a			; DAX
	ld	b, #0x26
	ex	af, af'
	out	(c), a			; DMALen
	inc	b
	ld	a, #0x31		; RAM -> RAM, S_ALGN | D_ALGN — пуск
	out	(c), a
	pop	de
	pop	bc
	ret

	.area	_BANK25

rr_lo:	.ds	2			; row_rng: пределы tx
rr_hi:	.ds	2
