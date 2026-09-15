;; Банк 25: горячие проходы тени глобуса (src/ui/globe_sh.c, globe.md §6.4). Win3 — страница
;; тени (SH_*, раскладка — globe_sh.c).
;;
;;   _sh_e0   — таблица E0 (эллипс большого круга ⟂ s): для j = j0 .. j0 + n − 1 (v = j − 256
;;              пикселей) X левой / правой точки (пары 8.8 от центра окна): xc(v) ∓ W(v),
;;              W в четвертях пары — «догоняющий» корень из QW = W4²·512 (приращения второго
;;              порядка, без умножений); вне эллипса (QW < 0) — #8000.
;;   _sh_rows — строки 0..199: уровни первой / последней пары (и экстремума строки на зумах 0–1)
;;              таблицами t, пересечения эллипсов уровней (накопители индексов E0 по уровням),
;;              интервалы уровней -> 2D DMA из строк-образцов: подкладка суши L в x 0..255,
;;              цвет океана O в x + 256 строки заднего буфера; флаги строк с тенью.

	.module globe_sh_s
	.optsdcc -mz80 sdcccall(1)

	.globl	_sh_ramp, _sh_radr, _sh_rhs, _sh_rn, _sh_rv, _sh_rdv
	.globl	_sh_e0, _sh_rows, add32, lvl_of, tx_at, tz_add, cross_side, build, iv_put, iv_dma
	.globl	_sh_j0, _sh_jn, _sh_qw, _sh_dq, _sh_ddq, _sh_w4, _sh_sw, _sh_xc, _sh_dxc
	.globl	_sh_zi, _sh_nlev, _sh_cross, _sh_dl, _sh_top, _sh_base, _sh_limb, _sh_page, _sh_txh
	.globl	_sh_ty, _sh_dty, _sh_lut
	.globl	_sh_row, _sh_shift

SH_TLL	= 0xC000 + 0x2200		; E0: X левой точки, младшие / старшие [512]
SH_TLH	= 0xC000 + 0x2400
SH_TRL	= 0xC000 + 0x2600		;     правой
SH_TRH	= 0xC000 + 0x2800
SH_TX	= 0xC000 + 0x2A00		; t16 от x пары: младшие [128], старшие — +128
SH_TZL	= 0xC000 + 0x2B00		; t16 от z [256]
SH_TAL	= 0xC000 + 0x2D00		; t16 экстремума строки по ρ [256], старшие — +256
SH_FLG	= 0xC000 + 0x2F00		; флаги строк [200]
LV_A0	= 0xC000 + 0x3200		; уровни (по 16): накопитель индекса E0 (дробь, j мл., j ст.),
LV_SF	= 0xC000 + 0x3230		;   шаг: 1 + SF/256 строки E0 на строку экрана,
LV_CXL	= 0xC000 + 0x3240		;   сдвиг x (пары 8.8, с центром и сдвигом выборки), ст. — +16,
LV_SC	= 0xC000 + 0x3260		;   масштаб a_k: x − x >> SC (0 — нет)
LV_L	= 0xC000 + 0x3270		;   видимые j стороны L: lo мл., lo ст., hi мл., hi ст., inv (+16)
LV_R	= 0xC000 + 0x32C0		;   стороны R
LV_ORDL	= 0xC000 + 0x3310		; порядок уровней: L, R
LV_ORDR	= 0xC000 + 0x3320
SH_DBG	= 0xC000 + 0x3400		; отладка: по строке 12 байт — интервалов, lf, (пара, уровень) x 5
SH_CP	= 0xC000 + 0x3E00		; пересечения строки: пара, смена уровня
SH_CD	= 0xC000 + 0x3E20
SH_AP	= 0xC000 + 0x3E40		; активные
SH_AD	= 0xC000 + 0x3E60
SH_IVP	= 0xC000 + 0x3E80		; интервалы: начало, уровень
SH_IVL	= 0xC000 + 0x3EA0
BACK_Y	= 280
SCREEN_PAGE = 0x10

	.area	_DATA

_sh_j0:	.ds	2			; E0: первый j, число строк
_sh_jn:	.ds	2
_sh_qw:	.ds	4			; QW = W4²·512 при v (растёт на dq), dq += ddq
_sh_dq:	.ds	4
_sh_ddq: .ds	4
_sh_w4:	.ds	2			; W (четверти пары) и W4²·512
_sh_sw:	.ds	4
_sh_xc:	.ds	4			; xc (пары 8.8) · 256, шаг
_sh_dxc: .ds	4
_sh_zi:	.ds	2			; строки: зум · 1000 (таблица sh_row: pl, pr, zf, zl, rh)
_sh_nlev: .ds	1
_sh_cross: .ds	1			; 1 — пересечения эллипсов (иначе только крайние пары)
_sh_dl:	.ds	1			; смена уровня на пересечении L (±1; R — обратная)
_sh_top: .ds	1			; наибольший уровень (10 или 5)
_sh_base: .ds	1			; узор уровня 0 (0 или 11)
_sh_limb: .ds	1			; 1 — зумы 0–1 (экстремум строки в окне)
_sh_page: .ds	1			; страница тени (источник DMA)
_sh_txh: .ds	2			; t16 полупары (первая пара — правый пиксель)
_sh_ty:	.ds	4			; t16 строки · 65536, шаг
_sh_dty: .ds	4
_sh_lut: .ds	2			; уровень по t (+128): sh_lut_full / sh_lut_pair
_sh_radr: .ds	2			; лестница: адрес младших, до старших, число, значение и шаг (16.16)
_sh_rhs: .ds	2
_sh_rn:	.ds	2
_sh_rv:	.ds	4
_sh_rdv: .ds	4
e0_j:	.ds	2
e0_t:	.ds	4
sr_y:	.ds	1
sr_pl:	.ds	1
sr_pr:	.ds	1
sr_lf:	.ds	1
sr_ll:	.ds	1
sr_lm:	.ds	1
sr_tyy:	.ds	2
sr_n:	.ds	1			; пересечений, из них L
sr_nla:	.ds	1
sr_na:	.ds	1			; активных, из них L
sr_nl0:	.ds	1
sr_ni:	.ds	1
sr_flg:	.ds	1
sr_sd:	.ds	1			; смена уровня текущей стороны
sr_dah:	.ds	1
sr_dax:	.ds	1
sr_sh:	.ds	1
sr_ii:	.ds	1
cs_tp:	.ds	1
cs_vb:	.ds	2
cs_i:	.ds	2

	.area	_BANK25

;; ================================================================ лестница

;; _sh_rn значений старшего слова _sh_rv (+= _sh_rdv): младшие байты с _sh_radr, старшие — с
;; _sh_radr + _sh_rhs (таблицы t кадра)
_sh_ramp::
	ld	hl, (_sh_radr)
	ld	de, (_sh_rhs)
	ld	bc, (_sh_rn)
1$:	push	bc
	ld	a, (_sh_rv + 2)
	ld	(hl), a
	push	hl
	add	hl, de
	ld	a, (_sh_rv + 3)
	ld	(hl), a
	ld	hl, #_sh_rv
	push	de
	ld	de, #_sh_rdv
	call	add32
	pop	de
	pop	hl
	inc	hl
	pop	bc
	dec	bc
	ld	a, b
	or	a, c
	jr	nz, 1$
	ret

;; ================================================================ E0

;; Строки E0 j = _sh_j0 .. + _sh_jn − 1. Состояние: QW (Q9 от W4²), dq, ddq; W4 и W4²·512;
;; xc (16.8 от пар 8.8), dxc.
_sh_e0::
	ld	bc, (_sh_jn)
	ld	a, b
	or	a, c
	ret	z
	ld	hl, (_sh_j0)
	ld	(e0_j), hl
e0_loop:
	push	bc
	ld	a, (_sh_qw + 3)		; QW < 0 — вне эллипса
	bit	7, a
	jr	z, 2$
	ld	hl, #0x8000
	ld	de, #0x8000
	jp	e0_put
	; догнать W4: пока T = S + (2·W4 + 1)·512 ≤ QW — S = T, W4++
2$:	ld	hl, (_sh_w4)
	add	hl, hl
	inc	hl			; 2·W4 + 1 (< 4096)
	add	hl, hl			; ·2: байты 1, 2 слагаемого ·512
	ld	a, (_sh_sw)
	ld	(e0_t), a
	ld	a, (_sh_sw + 1)
	add	a, l
	ld	(e0_t + 1), a
	ld	a, (_sh_sw + 2)
	adc	a, h
	ld	(e0_t + 2), a
	ld	a, (_sh_sw + 3)
	adc	a, #0
	ld	(e0_t + 3), a
	ld	hl, #_sh_qw		; QW − T < 0 — стоп
	ld	de, #e0_t
	call	cmp32
	jp	m, 3$
	ld	hl, (e0_t)
	ld	(_sh_sw), hl
	ld	hl, (e0_t + 2)
	ld	(_sh_sw + 2), hl
	ld	hl, (_sh_w4)
	inc	hl
	ld	(_sh_w4), hl
	jr	2$
	; пока S > QW — W4−−, S −= (2·W4 + 1)·512
3$:	ld	hl, #_sh_qw
	ld	de, #_sh_sw
	call	cmp32
	jp	p, 5$
	ld	hl, (_sh_w4)
	ld	a, h
	or	a, l
	jr	z, 5$
	dec	hl
	ld	(_sh_w4), hl
	add	hl, hl
	inc	hl
	add	hl, hl
	ld	a, (_sh_sw + 1)
	sub	a, l
	ld	(_sh_sw + 1), a
	ld	a, (_sh_sw + 2)
	sbc	a, h
	ld	(_sh_sw + 2), a
	ld	a, (_sh_sw + 3)
	sbc	a, #0
	ld	(_sh_sw + 3), a
	jr	3$
	; W (пары 8.8) = min(W4, 511) · 64; X = xc ∓ W с насыщением
5$:	ld	hl, (_sh_w4)
	ld	a, h
	cp	a, #2
	jr	c, 51$
	ld	hl, #511
51$:	add	hl, hl
	add	hl, hl
	add	hl, hl
	add	hl, hl
	add	hl, hl
	add	hl, hl
	ld	b, h
	ld	c, l			; BC = W
	call	e0_xc			; HL = xc
	push	hl
	or	a, a
	sbc	hl, bc
	jp	po, 6$
	ld	hl, #0x8001		; переполнение — −32767
6$:	ex	de, hl			; DE = левая
	pop	hl
	or	a, a
	adc	hl, bc
	jp	po, 7$
	ld	hl, #0x7FFF
7$:	ex	de, hl			; HL — левая, DE — правая
e0_put:	; запись: HL — левая, DE — правая
	push	de
	ex	de, hl
	ld	hl, (e0_j)
	ld	a, h
	add	a, #>SH_TLL
	ld	h, a
	ld	(hl), e
	inc	h
	inc	h
	ld	(hl), d			; SH_TLH = SH_TLL + #200
	inc	h
	inc	h
	pop	de
	ld	(hl), e			; SH_TRL = SH_TLL + #400
	inc	h
	inc	h
	ld	(hl), d
	ld	hl, #_sh_qw		; следующий v: QW += dq, dq += ddq, xc += dxc
	ld	de, #_sh_dq
	call	add32
	ld	hl, #_sh_dq
	ld	de, #_sh_ddq
	call	add32
	ld	hl, #_sh_xc
	ld	de, #_sh_dxc
	call	add32
	ld	hl, (e0_j)
	inc	hl
	ld	(e0_j), hl
	pop	bc
	dec	bc
	ld	a, b
	or	a, c
	jp	nz, e0_loop
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

;; Флаги (S) разности (HL) − (DE), 32 бита (без переполнения: оба в ±2^30)
cmp32:
	ld	a, (de)
	ld	c, a
	ld	a, (hl)
	sub	a, c
	inc	hl
	inc	de
	ld	a, (de)
	ld	c, a
	ld	a, (hl)
	sbc	a, c
	inc	hl
	inc	de
	ld	a, (de)
	ld	c, a
	ld	a, (hl)
	sbc	a, c
	inc	hl
	inc	de
	ld	a, (de)
	ld	c, a
	ld	a, (hl)
	sbc	a, c
	ret

;; HL = _sh_xc >> 8 (пары 8.8) с насыщением до ±32767; BC сохраняется
e0_xc:
	ld	a, (_sh_xc + 2)
	rla				; CY — знак байта 2
	ld	a, (_sh_xc + 3)
	adc	a, #0			; 0 — байт 3 продолжает знак
	jr	nz, 1$
	ld	hl, (_sh_xc + 1)
	ret
1$:	ld	a, (_sh_xc + 3)
	bit	7, a
	ld	hl, #0x7FFF
	ret	z
	ld	hl, #0x8001
	ret

;; ================================================================ строки

;; Уровень по t16 (HL): A = lut[clamp(t16 / 16 + 128)]
lvl_of:
	ld	de, #0x0800
	add	hl, de			; t16 + 2048: 0..4095 — внутри
	bit	7, h
	jr	nz, 1$
	ld	a, h
	cp	a, #0x10
	jr	nc, 2$
	ld	a, l			; (H << 4) | (L >> 4)
	rrca
	rrca
	rrca
	rrca
	and	a, #0x0F
	ld	e, a
	ld	a, h
	rlca
	rlca
	rlca
	rlca
	and	a, #0xF0
	or	a, e
	ld	e, a
	ld	d, #0
	ld	hl, (_sh_lut)
	add	hl, de
	ld	a, (hl)
	ret
1$:	ld	hl, (_sh_lut)
	ld	a, (hl)
	ret
2$:	ld	hl, (_sh_lut)
	ld	de, #255
	add	hl, de
	ld	a, (hl)
	ret

;; HL = tx[A]
tx_at:
	ld	l, a
	ld	h, #>SH_TX
	ld	e, (hl)
	set	7, l
	ld	d, (hl)
	ex	de, hl
	ret

;; HL += tz[A] (t16)
tz_add:
	ld	e, a
	ld	d, #>SH_TZL
	ld	a, (de)
	add	a, l
	ld	l, a
	inc	d
	ld	a, (de)
	adc	a, h
	ld	h, a
	ret

_sh_rows::
	push	ix
	push	iy
	xor	a, a
	ld	(sr_y), a
	ld	iy, #_sh_row		; IY — строка таблицы: pl, pr, zf, zl, rh
	ld	de, (_sh_zi)
	add	iy, de
	ld	bc, #0x27AF		; DMANum = 1: две пачки (L, затем O через 256)
10$:	in	a, (c)
	jp	m, 10$
	ld	b, #0x28
	ld	a, #1
	out	(c), a
row_loop:
	ld	a, 0 (iy)
	ld	(sr_pl), a
	ld	c, a
	ld	a, 1 (iy)
	ld	(sr_pr), a
	cp	a, c
	jp	c, row_skip		; строки диска нет
	ld	hl, (_sh_ty + 2)
	ld	(sr_tyy), hl
	ld	a, (sr_pl)		; lf = lut(tx[pl] + txh + ty + tz[zf])
	call	tx_at
	ld	de, (_sh_txh)
	add	hl, de
	ld	de, (sr_tyy)
	add	hl, de
	ld	a, 2 (iy)
	call	tz_add
	call	lvl_of
	ld	(sr_lf), a
	ld	a, (sr_pr)		; ll = lut(tx[pr] + ty + tz[zl])
	call	tx_at
	ld	de, (sr_tyy)
	add	hl, de
	ld	a, 3 (iy)
	call	tz_add
	call	lvl_of
	ld	(sr_ll), a
	xor	a, a
	ld	(sr_n), a
	ld	(sr_nla), a
	ld	a, (_sh_cross)
	or	a, a
	jr	z, 5$
	ld	a, (_sh_dl)		; пересечения: L по порядку, потом R
	ld	(sr_sd), a
	ld	hl, #LV_L
	ld	(cs_vb), hl
	ld	hl, #LV_ORDL
	ld	a, #>SH_TLL
	call	cross_side
	ld	a, (sr_n)
	ld	(sr_nla), a
	ld	a, (_sh_dl)
	neg
	ld	(sr_sd), a
	ld	hl, #LV_R
	ld	(cs_vb), hl
	ld	hl, #LV_ORDR
	ld	a, #>SH_TRL
	call	cross_side
5$:	call	build
	jr	row_next
row_skip:
	xor	a, a
	ld	(sr_flg), a
row_next:
	ld	a, (sr_y)		; флаг строки
	ld	l, a
	ld	h, #>SH_FLG
	ld	a, (sr_flg)
	ld	(hl), a
	ld	a, (_sh_cross)		; накопители уровней: acc += 256 + SF
	or	a, a
	jr	z, 7$
	ld	ix, #LV_A0
	ld	a, (_sh_nlev)
	ld	b, a
6$:	ld	a, 0 (ix)
	add	a, 48 (ix)
	ld	0 (ix), a
	ld	a, 16 (ix)
	adc	a, #1
	ld	16 (ix), a
	ld	a, 32 (ix)
	adc	a, #0
	ld	32 (ix), a
	inc	ix
	djnz	6$
7$:	ld	hl, #_sh_ty		; ty += dty
	ld	de, #_sh_dty
	call	add32
	ld	de, #5
	add	iy, de
	ld	a, (sr_y)
	inc	a
	ld	(sr_y), a
	cp	a, #200
	jp	c, row_loop
	ld	bc, #0x27AF		; дождаться DMA, DMANum = 0
8$:	in	a, (c)
	jp	m, 8$
	ld	b, #0x28
	xor	a, a
	out	(c), a
	pop	iy
	pop	ix
	ret

;; Пересечения стороны: HL — порядок уровней, cs_vb — видимость стороны (5 массивов по 16:
;; lo мл., lo ст., hi мл., hi ст., inv), A — страница таблицы E0 стороны (младшие; старшие — +2)
cross_side:
	ld	(cs_tp), a
	ld	a, (_sh_nlev)
	ld	b, a
1$:	push	bc
	push	hl
	ld	e, (hl)			; уровень i
	ld	d, #0
	ld	(cs_i), de
	ld	ix, (cs_vb)
	add	ix, de
	ld	a, 64 (ix)		; 2 — сторона уровня вне окна или не видна (globe_sh.c)
	cp	a, #2
	jp	z, 9$
	ld	hl, #LV_A0 + 16		; j = A2:A1 [i]
	add	hl, de
	ld	c, (hl)
	ld	a, l
	add	a, #16
	ld	l, a
	ld	b, (hl)			; BC = j
	; видимость: in = lo ≤ j ≤ hi; видно, если in != inv
	ld	e, #0
	ld	a, c
	sub	a, 0 (ix)
	ld	a, b
	sbc	a, 16 (ix)
	jr	c, 2$			; j < lo
	ld	a, 32 (ix)
	sub	a, c
	ld	a, 48 (ix)
	sbc	a, b
	jr	c, 2$			; j > hi
	inc	e
2$:	ld	a, e
	cp	a, 64 (ix)
	jr	z, 9$			; не видно
	ld	a, (cs_tp)		; t = E0[j]
	add	a, b
	ld	h, a
	ld	l, c
	ld	e, (hl)
	inc	h
	inc	h
	ld	d, (hl)
	ld	a, d
	cp	a, #0x80
	jr	nz, 3$
	ld	a, e
	or	a, a
	jr	z, 9$			; #8000 — нет
3$:	ld	hl, (cs_i)		; масштаб a_k: t −= t >> SC
	ld	bc, #LV_SC
	add	hl, bc
	ld	a, (hl)
	or	a, a
	jr	z, 5$
	ld	b, a
	ld	h, d
	ld	l, e
4$:	sra	h
	rr	l
	djnz	4$
	ex	de, hl
	or	a, a
	sbc	hl, de
	ex	de, hl
5$:	ld	hl, (cs_i)		; x = cx + t -> пара: старший байт (< 0 — 0, ≥ 128 — 128)
	ld	bc, #LV_CXL
	add	hl, bc
	ld	c, (hl)
	ld	a, l
	add	a, #16
	ld	l, a
	ld	h, (hl)
	ld	l, c
	or	a, a
	adc	hl, de
	jp	pe, 7$			; переполнение: по знаку t
	bit	7, h
	jr	nz, 6$
	ld	a, h
	cp	a, #128
	jr	c, 8$
	ld	a, #128
	jr	8$
6$:	xor	a, a
	jr	8$
7$:	bit	7, d
	ld	a, #0
	jr	nz, 8$
	ld	a, #128
8$:	ld	c, a			; запись пересечения
	ld	a, (sr_n)
	ld	l, a
	ld	h, #>SH_CP
	ld	(hl), c
	add	a, #<SH_CD - <SH_CP
	ld	l, a
	ld	a, (sr_sd)
	ld	(hl), a
	ld	hl, #sr_n
	inc	(hl)
9$:	pop	hl
	pop	bc
	inc	hl
	dec	b
	jp	nz, 1$
	ret

;; Интервалы строки и DMA. Пересечения (SH_CP/CD, sr_n, из них L — sr_nla): монотонно,
;; активные (pl, pr]; зумы 0–1 — поправки lf -> lm -> ll; иначе после последнего — ll.
build:
	xor	a, a
	ld	(sr_na), a
	ld	(sr_nl0), a
	ld	d, a			; D — наибольшая пара (монотонно)
	ld	a, (sr_n)
	or	a, a
	jr	z, 4$
	ld	b, a
	ld	c, #0			; C — индекс
1$:	ld	l, c
	ld	h, #>SH_CP
	ld	a, (hl)
	cp	a, d
	jr	nc, 2$
	ld	a, d
2$:	ld	d, a
	ld	e, a			; пара
	ld	a, (sr_pl)
	cp	a, e
	jr	nc, 3$			; ≤ pl
	ld	a, (sr_pr)
	cp	a, e
	jr	c, 3$			; > pr
	ld	a, l
	add	a, #<SH_CD - <SH_CP
	ld	l, a
	ld	a, (hl)			; смена
	push	af
	ld	a, (sr_na)
	add	a, #<SH_AP
	ld	l, a
	ld	h, #>SH_AP
	ld	(hl), e
	add	a, #<SH_AD - <SH_AP
	ld	l, a
	pop	af
	ld	(hl), a
	ld	hl, #sr_na
	inc	(hl)
	ld	a, c			; сторона L — индекс < nla
	ld	hl, #sr_nla
	cp	a, (hl)
	jr	nc, 3$
	ld	a, (sr_na)
	ld	(sr_nl0), a
3$:	inc	c
	djnz	1$
4$:	ld	a, (_sh_limb)
	or	a, a
	jp	z, bd_tail
	ld	a, (_sh_cross)
	or	a, a
	jp	z, bd_tail
	ld	a, 4 (iy)		; экстремум строки: lm = lut(ta[rh] + ty)
	ld	l, a
	ld	h, #>SH_TAL
	ld	e, (hl)
	inc	h
	ld	d, (hl)
	ld	hl, (sr_tyy)
	add	hl, de
	call	lvl_of
	ld	(sr_lm), a
	ld	bc, #0			; суммы смен: B — L, C — R
	ld	a, (sr_na)
	or	a, a
	jr	z, 7$
	ld	e, a
	ld	d, #0
	ld	hl, #SH_AD
5$:	ld	a, d			; индекс ≥ nl0 — R
	push	hl
	ld	hl, #sr_nl0
	cp	a, (hl)
	pop	hl
	ld	a, (hl)
	jr	nc, 6$
	add	a, b
	ld	b, a
	jr	61$
6$:	add	a, c
	ld	c, a
61$:	inc	l
	inc	d
	dec	e
	jr	nz, 5$
7$:	ld	a, (sr_lf)		; exl = lm − lf − sl
	ld	e, a
	ld	a, (sr_lm)
	sub	a, e
	sub	a, b
	jr	z, 9$
	ld	b, a
	ld	a, (sr_nl0)
	or	a, a
	jr	z, 8$
	ld	a, (SH_AD)		; первому L
	add	a, b
	ld	(SH_AD), a
	jr	9$
8$:	ld	a, (sr_pl)		; нет L: (pl + 1, exl) в начало
	ld	e, a
	ld	a, (sr_pr)
	cp	a, e
	jr	z, 9$
	push	bc
	ld	a, (sr_na)
	or	a, a
	jr	z, 82$
	ld	c, a
	ld	b, #0
	ld	hl, #SH_AP - 1
	add	hl, bc
	ld	d, h
	ld	e, l
	inc	de
	push	bc
	lddr
	pop	bc
	ld	hl, #SH_AD - 1
	add	hl, bc
	ld	d, h
	ld	e, l
	inc	de
	lddr
82$:	pop	bc
	ld	a, (sr_pl)
	inc	a
	ld	(SH_AP), a
	ld	a, b
	ld	(SH_AD), a
	ld	hl, #sr_na
	inc	(hl)
	ld	a, #1
	ld	(sr_nl0), a
9$:	ld	a, (sr_lm)		; exr = ll − lm − sr
	ld	e, a
	ld	a, (sr_ll)
	sub	a, e
	sub	a, c
	jr	z, bd_iv
	ld	c, a
	ld	a, (sr_nl0)
	ld	e, a
	ld	a, (sr_na)
	cp	a, e
	jr	z, 10$			; нет R
	dec	a			; последнему
	add	a, #<SH_AD
	ld	l, a
	ld	h, #>SH_AD
	ld	a, (hl)
	add	a, c
	ld	(hl), a
	jr	bd_iv
10$:	ld	a, (sr_na)		; (pr, exr) в конец
	add	a, #<SH_AP
	ld	l, a
	ld	h, #>SH_AP
	ld	a, (sr_pr)
	ld	(hl), a
	ld	a, l
	add	a, #<SH_AD - <SH_AP
	ld	l, a
	ld	(hl), c
	ld	hl, #sr_na
	inc	(hl)
	jr	bd_iv
bd_tail:				; после последнего активного — ll
	ld	a, (sr_na)
	or	a, a
	jr	z, bd_iv
	dec	a
	add	a, #<SH_AD
	ld	l, a
	ld	h, #>SH_AD
	ld	(hl), #127
bd_iv:					; интервалы: [pl, …) — lf, дальше смены
	xor	a, a
	ld	(sr_ni), a
	ld	(sr_flg), a
	ld	a, (sr_lf)
	ld	c, a			; C — уровень (текущий)
	ld	b, a			; B — уровень интервала
	ld	a, (sr_pl)
	ld	d, a			; D — начало интервала
	ld	a, (sr_na)
	or	a, a
	jr	z, 16$
	ld	e, #0			; E — индекс
11$:	ld	l, e
	ld	h, #>SH_AD
	ld	a, l
	add	a, #<SH_AD
	ld	l, a
	ld	a, (hl)
	cp	a, #127
	jr	nz, 12$
	ld	a, (sr_ll)
	jr	13$
12$:	add	a, c			; уровень + смена, в [0, top]
	bit	7, a
	jr	z, 121$
	xor	a, a
121$:	ld	hl, #_sh_top
	cp	a, (hl)
	jr	c, 13$
	ld	a, (hl)
13$:	ld	c, a
	cp	a, b
	jr	z, 15$			; уровень интервала не меняется
	ld	a, e
	add	a, #<SH_AP
	ld	l, a
	ld	h, #>SH_AP
	ld	a, (hl)			; пара смены
	cp	a, d
	jr	z, 14$			; в начале интервала — только уровень
	jr	c, 14$
	push	af
	call	iv_put			; [D, …) уровня B
	pop	af
	ld	d, a
14$:	ld	b, c
15$:	inc	e
	ld	a, (sr_na)
	cp	a, e
	jr	nz, 11$
16$:	call	iv_put			; последний [D, pr]
	ld	a, (sr_y)		; отладка: 12 байт строки
	ld	l, a
	ld	h, #0
	add	hl, hl
	add	hl, hl
	ld	d, h
	ld	e, l
	add	hl, hl
	add	hl, de
	ld	de, #SH_DBG
	add	hl, de
	ld	a, (sr_ni)
	ld	(hl), a
	inc	hl
	ld	a, (sr_lf)
	ld	(hl), a
	inc	hl
	ex	de, hl
	ld	a, (sr_ni)
	cp	a, #6
	jr	c, 17$
	ld	a, #5
17$:	ld	b, a
	ld	hl, #SH_IVP
18$:	ld	a, (hl)
	ld	(de), a
	inc	de
	push	hl
	ld	a, l
	add	a, #<SH_IVL - <SH_IVP
	ld	l, a
	ld	a, (hl)
	pop	hl
	ld	(de), a
	inc	de
	inc	l
	djnz	18$
	ld	a, (sr_flg)
	or	a, a
	ret	z
	jp	iv_dma

;; Интервал [D, …) уровня B -> список; уровень > 0 — строка с тенью
iv_put:
	ld	a, (sr_ni)
	add	a, #<SH_IVP
	ld	l, a
	ld	h, #>SH_IVP
	ld	(hl), d
	add	a, #<SH_IVL - <SH_IVP
	ld	l, a
	ld	(hl), b
	ld	hl, #sr_ni
	inc	(hl)
	ld	a, b
	or	a, a
	ret	z
	ld	a, #1
	ld	(sr_flg), a
	ret

;; 2D DMA интервалов строки: источник — узор уровня (L, O через 256) со сдвигом строки,
;; приёмник — строка заднего буфера (x, x + 256)
iv_dma:
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
	ld	de, #_sh_shift
	ld	a, (sr_y)
	ld	l, a
	ld	h, #0
	add	hl, de
	ld	a, (hl)
	ld	(sr_sh), a
	xor	a, a
	ld	(sr_ii), a
1$:	ld	a, (sr_ii)
	add	a, #<SH_IVP
	ld	l, a
	ld	h, #>SH_IVP
	ld	d, (hl)			; p0
	ld	a, (sr_ii)
	inc	a
	ld	hl, #sr_ni
	cp	a, (hl)
	ld	a, (sr_pr)
	inc	a			; конец — pr + 1 (inc не трогает CY)
	jr	nc, 2$
	ld	a, (sr_ii)
	add	a, #<SH_IVP + 1
	ld	l, a
	ld	h, #>SH_IVP
	ld	a, (hl)			; p1 — начало следующего
2$:	sub	a, d			; слов
	jr	z, 5$
	jr	c, 5$
	dec	a
	ld	e, a			; DMALen
	ld	a, (sr_ii)
	add	a, #<SH_IVL
	ld	l, a
	ld	h, #>SH_IVL
	ld	a, (_sh_base)
	add	a, (hl)
	add	a, a			; строка узора L = 2 · узор (страницы по 256)
	ld	h, a			; H — SAH
	ld	a, d
	add	a, a
	ld	l, a			; L — DAL = 2 · p0
	ld	bc, #0x27AF
3$:	in	a, (c)
	jp	m, 3$
	ld	a, (sr_sh)
	add	a, l
	ld	b, #0x1A
	out	(c), a			; SAL
	inc	b
	out	(c), h			; SAH
	ld	a, (_sh_page)
	inc	b
	out	(c), a			; SAX
	inc	b
	out	(c), l			; DAL
	ld	a, (sr_dah)
	inc	b
	out	(c), a			; DAH
	ld	a, (sr_dax)
	inc	b
	out	(c), a			; DAX
	ld	b, #0x26
	out	(c), e			; DMALen
	inc	b
	ld	a, #0x31		; RAM -> RAM, S_ALGN | D_ALGN
	out	(c), a
5$:	ld	hl, #sr_ii
	inc	(hl)
	ld	a, (sr_ni)
	cp	a, (hl)
	jr	nz, 1$
	ret
