;; Дальняя память и копирование DMA (Win0, ассемблер; был far.c).
;; far_t — 22-битный физический адрес (страница << 14 | смещение), как у DMA.
;; ABI SDCC --sdcccall 1: far_t (32 бита) — DE (младшее слово) + HL (старшее);
;; остальные аргументы — в стеке, снимает вызываемая функция; результат: байт — A,
;; слово — DE, 32 бита — DE + HL. Сохранять только IX.
;; Win3 на время копирования переключается напрямую (порт), теневой регистр
;; pages.s не меняется — в конце в порт возвращается прежняя страница.

	.module far
	.globl	_far_read
	.globl	_far_write
	.globl	_far_fill
	.globl	_far_byte
	.globl	_far_word
	.globl	_far_copy
	.globl	_near_phys
	.globl	_pg_win3
	.globl	win3_real

PAGE3_PORT	= 0x13AF
KERNEL_PAGE	= 0x04
DATA_PAGE	= 0x05

	.area	_DATA
f_len:	.ds	2			; осталось байт
f_pg:	.ds	1			; текущая страница источника/приёмника
f_val:	.ds	1			; байт заполнения
fc_dst:	.ds	4
fc_src:	.ds	4
fc_len:	.ds	4
fc_done:	.ds	4

	.area	_CODE

;; far_t (DE, HL) -> f_pg = страница, HL = #C000 + смещение
far_setup:
	ld	a, d
	rlca
	rlca
	and	#3
	ld	b, a
	ld	a, l
	add	a, a
	add	a, a
	or	b
	ld	(f_pg), a
	ld	a, d
	and	#0x3F
	or	#0xC0
	ld	h, a
	ld	l, e
	ret

;; Кусок до конца окна: HL = дальний указатель (#C000..#FFFF); подключает f_pg,
;; BC = n = min(f_len, #10000 - HL), f_len -= n. HL сохраняется.
f_chunk:
	ld	a, (f_pg)
	ld	(win3_real), a			; фактическая страница Win3 — для прерывания курсора
	ld	bc, #PAGE3_PORT
	out	(c), a
	ld	bc, (f_len)
	push	hl
	xor	a
	sub	l
	ld	l, a
	ld	a, #0
	sbc	a, h
	ld	h, a			; HL = доступно в окне (1..#4000)
	or	a
	sbc	hl, bc
	jr	nc, 1$			; хватает — n = f_len
	add	hl, bc
	ld	b, h
	ld	c, l			; n = до конца окна
1$:	ld	hl, (f_len)
	or	a
	sbc	hl, bc
	ld	(f_len), hl
	pop	hl
	ret

;; После куска: указатель дошёл до конца окна (HL = 0) — следующая страница
f_next:
	ld	a, h
	or	l
	ret	nz
	ld	h, #0xC0
	ld	a, (f_pg)
	inc	a
	ld	(f_pg), a
	ret

;; Вернуть в Win3 страницу из теневого регистра и выйти (IY — адрес возврата)
f_exit:
	call	_pg_win3
	ld	(win3_real), a
	ld	bc, #PAGE3_PORT
	out	(c), a
	jp	(iy)

;; void far_read(far_t src, void *dst, uint16_t len)
_far_read::
	call	far_setup
	pop	iy
	pop	de			; dst
	pop	bc
	ld	(f_len), bc
fr_loop:
	ld	a, b
	or	c
	jr	z, f_exit
	call	f_chunk
	ldir
	call	f_next
	ld	bc, (f_len)
	jr	fr_loop

;; void far_write(far_t dst, const void *src, uint16_t len)
_far_write::
	call	far_setup
	pop	iy
	pop	de			; src (ближний)
	pop	bc
	ld	(f_len), bc
fw_loop:
	ld	a, b
	or	c
	jr	z, f_exit
	call	f_chunk
	ex	de, hl			; HL = ближний, DE = дальний
	ldir
	ex	de, hl
	call	f_next
	ld	bc, (f_len)
	jr	fw_loop

;; void far_fill(far_t dst, uint8_t v, uint16_t len)
_far_fill::
	call	far_setup
	pop	iy
	dec	sp			; байт v
	pop	af
	ld	(f_val), a
	pop	bc
	ld	(f_len), bc
ff_loop:
	ld	a, b
	or	c
	jr	z, f_exit
	call	f_chunk			; BC = n >= 1
	ld	a, (f_val)		; первый байт, остальные — LDIR со сдвигом на 1
	ld	(hl), a
	ld	d, h
	ld	e, l
	inc	de
	dec	bc
	ld	a, b
	or	c
	jr	z, 1$
	ldir
1$:	ex	de, hl			; HL = начало + n
	call	f_next
	ld	bc, (f_len)
	jr	ff_loop

;; uint8_t far_byte(far_t src)
_far_byte::
	call	far_setup
	ld	a, (f_pg)
	ld	(win3_real), a
	ld	bc, #PAGE3_PORT
	out	(c), a
	ld	e, (hl)
	call	_pg_win3
	ld	(win3_real), a
	out	(c), a
	ld	a, e
	ret

;; uint16_t far_word(far_t src) — слово может переходить через границу страницы
_far_word::
	ld	bc, #2
	push	bc
	ld	bc, #fw_tmp
	push	bc
	call	_far_read
	ld	de, (fw_tmp)
	ret

	.area	_DATA
fw_tmp:	.ds	2
	.area	_CODE

;; far_t near_phys(const void *p /*HL*/) — Win1 -> DATA_PAGE, иначе KERNEL_PAGE
_near_phys::
	ld	a, h
	and	#0x3F
	ld	d, a
	ld	e, l			; DE = смещение
	ld	a, h
	and	#0x40
	ld	a, #KERNEL_PAGE
	jr	z, 1$
	ld	a, #DATA_PAGE
1$:	;; far = page << 14 | off: биты 14-15 — в D, 16-21 — в L
	ld	l, a
	srl	l
	rr	b			; B7 = бит 0 страницы
	srl	l
	rr	b			; B7 = бит 1, B6 = бит 0
	ld	a, b
	and	#0xC0
	or	d
	ld	d, a
	ld	h, #0
	ret

;; void far_copy(far_t dst, far_t src, uint32_t len) — DMA RAM->RAM, len
;; округляется вверх до чётного; до 128 КБ за запуск (256 пачек по 512 байт).
_far_copy::
	pop	iy
	ld	(fc_dst), de
	ld	(fc_dst + 2), hl
	pop	de
	pop	hl
	ld	(fc_src), de
	ld	(fc_src + 2), hl
	pop	de
	pop	hl
	inc	de			; (len + 1) & ~1
	ld	a, d
	or	e
	jr	nz, 1$
	inc	hl
1$:	res	0, e
	ld	(fc_len), de
	ld	(fc_len + 2), hl
fc_loop:
	ld	hl, (fc_len)
	ld	a, h
	or	l
	ld	hl, (fc_len + 2)
	or	h
	or	l
	jp	z, fc_end
	xor	a
	ld	(fc_done), a
	ld	(fc_done + 3), a
	ld	a, h			; len >= #20000 — полный запуск
	or	a
	jr	nz, fc_max
	ld	a, l
	cp	#2
	jr	nc, fc_max
	ld	de, (fc_len)		; words = len >> 1 (17 бит -> 16)
	srl	l
	rr	d
	rr	e
	ld	a, d
	or	a
	jr	z, fc_small
	;; пачки по 256 слов: bursts = words >> 8 = D, done = D * 512
	ld	b, #255
	ld	c, d
	dec	c
	ld	a, d
	add	a, a
	ld	(fc_done + 1), a
	ld	a, #0
	adc	a, a
	ld	(fc_done + 2), a
	jr	fc_go
fc_small:				; одна пачка из E слов, done = E * 2
	ld	b, e
	dec	b
	ld	c, #0
	ld	a, e
	add	a, a
	ld	(fc_done), a
	ld	a, #0
	adc	a, a
	ld	(fc_done + 1), a
	xor	a
	ld	(fc_done + 2), a
	jr	fc_go
fc_max:
	ld	b, #255
	ld	c, #255
	xor	a
	ld	(fc_done + 1), a
	ld	a, #2
	ld	(fc_done + 2), a
fc_go:
	push	bc			; B = LEN, C = NUM
	call	dma_wait
	ld	de, (fc_src)
	ld	hl, (fc_src + 2)
	ld	a, e
	ld	bc, #0x1AAF		; SAL
	out	(c), a
	ld	a, d
	and	#0x3F
	inc	b			; #1BAF SAH
	out	(c), a
	call	page_of
	inc	b			; #1CAF SAX
	out	(c), a
	ld	de, (fc_dst)
	ld	hl, (fc_dst + 2)
	ld	a, e
	inc	b			; #1DAF DAL
	out	(c), a
	ld	a, d
	and	#0x3F
	inc	b			; #1EAF DAH
	out	(c), a
	call	page_of
	inc	b			; #1FAF DAX
	out	(c), a
	pop	de			; D = LEN, E = NUM
	ld	a, d
	ld	b, #0x26		; LEN
	out	(c), a
	ld	a, e
	ld	b, #0x28		; NUM
	out	(c), a
	ld	a, #0x01		; DMACtrl: RAM -> RAM, пуск
	ld	b, #0x27
	out	(c), a
	call	dma_wait
	ld	hl, #fc_src		; src += done, dst += done, len -= done
	call	add_done
	ld	hl, #fc_dst
	call	add_done
	ld	hl, #fc_len
	ld	de, #fc_done
	ld	b, #4
	or	a
2$:	ld	a, (de)
	ld	c, a
	ld	a, (hl)
	sbc	a, c
	ld	(hl), a
	inc	hl
	inc	de
	djnz	2$
	jp	fc_loop
fc_end:
	jp	(iy)

;; (HL) += fc_done, 4 байта
add_done:
	ld	de, #fc_done
	ld	b, #4
	or	a
1$:	ld	a, (de)
	adc	a, (hl)
	ld	(hl), a
	inc	hl
	inc	de
	djnz	1$
	ret

;; A = страница far_t (DE, HL): (L << 2) | (D >> 6); BC не трогать
page_of:
	ld	a, d
	rlca
	rlca
	and	#3
	ld	e, a
	ld	a, l
	add	a, a
	add	a, a
	or	e
	ret

dma_wait:
	push	bc
	ld	bc, #0x27AF
1$:	in	a, (c)
	rlca
	jr	c, 1$
	pop	bc
	ret
