;; Ввод, резидентная часть (Win0, ассемблер; был input.c): защёлка нажатий кнопок
;; мыши и клавиш в кадровом прерывании, пока их не заберёт input_poll (cursor.c,
;; банк 11) — долгая перерисовка между опросами их не теряет. Портит AF, BC, DE, HL.
;; Матрица ZX: полуряды #FEFE..#7FFE, биты 0..4. CAPS SHIFT — модификатор:
;; CS+буква — заглавная, CS+0 — KEY_DEL, CS+SPACE (BREAK) — KEY_ESC, CS+5..8 — стрелки.
;; SYMBOL SHIFT: SS+K — '+', SS+J — '-'. Стрелки и +/- при удержании повторяются
;; (поворот и зум глобуса), остальные клавиши — одно нажатие.

	.module input
	.globl	input_isr
	.globl	_in_prev_btn
	.globl	_in_prev_keys
	.globl	_in_btn_latch
	.globl	_in_key_latch
	.globl	_in_key_rep

KEY_ESC		= 27
KEY_ENTER	= 13
KEY_SPACE	= 32
KEY_DEL		= 8
KEY_LEFT	= 0x1C			; + 1 вниз, + 2 вверх, + 3 вправо (input.h)
REP_DELAY	= 12			; автоповтор стрелок и +/-: первый через 12 кадров,
REP_RATE	= 2			;   дальше каждые 2 (защёлка — одно нажатие до опроса)

	.area	_DATA
_in_prev_btn::	.ds	1		; кнопки прошлого кадра
_in_prev_keys::	.ds	1
_in_btn_latch::	.ds	1		; нажатия до опроса
_in_key_latch::	.ds	1
_in_key_rep::	.ds	1		; 1 — защёлка от автоповтора (клавишу могли уже отпустить)
rk_cs:		.ds	1
rk_ss:		.ds	1
in_rep:		.ds	1		; кадров до автоповтора удерживаемой клавиши

	.area	_CODE

keymap:
	.db	0, 'z', 'x', 'c', 'v'
	.db	'a', 's', 'd', 'f', 'g'
	.db	'q', 'w', 'e', 'r', 't'
	.db	'1', '2', '3', '4', '5'
	.db	'0', '9', '8', '7', '6'
	.db	'p', 'o', 'i', 'u', 'y'
	.db	KEY_ENTER, 'l', 'k', 'j', 'h'
	.db	KEY_SPACE, 0, 'm', 'n', 'b'

;; A = первая нажатая клавиша (с учётом CAPS SHIFT) или 0
read_key:
	ld	bc, #0x7FFE		; SYMBOL SHIFT — бит 1 полуряда #7FFE
	in	a, (c)
	cpl
	and	#2
	ld	(rk_ss), a
	ld	bc, #0xFEFE
	in	a, (c)
	cpl
	and	#1
	ld	(rk_cs), a
	ld	hl, #keymap
	ld	d, #8
rk_row:
	in	a, (c)
	cpl
	and	#0x1F
	ld	e, a
	push	de
	ld	d, #5
rk_bit:
	srl	e
	jr	nc, rk_next
	ld	a, (hl)
	or	a
	jr	nz, rk_found
rk_next:
	inc	hl
	dec	d
	jr	nz, rk_bit
	pop	de
	rlc	b			; следующий полуряд: FE, FD, FB, F7, EF, DF, BF, 7F
	dec	d
	jr	nz, rk_row
	xor	a
	ret
rk_found:
	pop	de
	ld	e, a
	ld	a, (rk_ss)		; SS+K — '+', SS+J — '-' (зум глобуса); прочие SS+ — как без SS
	or	a
	jr	z, 5$
	ld	a, e
	cp	#'k'
	ld	a, #'+'
	ret	z
	ld	a, e
	cp	#'j'
	ld	a, #'-'
	ret	z
5$:	ld	a, (rk_cs)
	or	a
	ld	a, e
	ret	z
	cp	#KEY_SPACE
	jr	nz, 1$
	ld	a, #KEY_ESC
	ret
1$:	cp	#'0'
	jr	nz, 2$
	ld	a, #KEY_DEL
	ret
2$:	cp	#'5'			; CS+5..8 — стрелки (влево, вниз, вверх, вправо)
	jr	c, 3$
	cp	#'8' + 1
	jr	nc, 3$
	sub	#'5' - KEY_LEFT
	ret
3$:	cp	#'a'
	ret	c
	cp	#'z' + 1
	ret	nc
	sub	#32
	ret

;; Из кадрового прерывания (crt0.s)
input_isr:
	ld	bc, #0xFADF		; кнопки Kempston, активны нулём
	in	a, (c)
	cpl
	and	#3
	ld	e, a
	ld	a, (_in_prev_btn)
	cpl
	and	e			; нажатые в этом кадре
	ld	hl, #_in_btn_latch
	or	(hl)
	ld	(hl), a
	ld	a, e
	ld	(_in_prev_btn), a
	call	read_key
	ld	hl, #_in_prev_keys
	cp	(hl)
	jr	z, 1$
	ld	(hl), a			; новая клавиша (или отпущена)
	ld	b, a
	ld	a, #REP_DELAY
	ld	(in_rep), a
	ld	a, b
	or	a
	ret	z
	ld	(_in_key_latch), a
	xor	a, a
	ld	(_in_key_rep), a	; настоящее нажатие
	ret
1$:	or	a			; держится: автоповтор стрелок и +/- (поворот и зум глобуса)
	ret	z
	ld	b, a
	cp	#KEY_LEFT
	jr	c, 2$
	cp	#KEY_LEFT + 4
	jr	c, 3$
2$:	cp	#'+'
	jr	z, 3$
	cp	#'-'
	ret	nz
3$:	ld	hl, #in_rep
	dec	(hl)
	ret	nz
	ld	(hl), #REP_RATE
	ld	a, b
	ld	(_in_key_latch), a
	ld	a, #1
	ld	(_in_key_rep), a	; автоповтор: если клавишу отпустят до опроса — не считать
	ret
