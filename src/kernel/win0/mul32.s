;; Замена модуля __mulsint2slong библиотеки SDCC 4.5 (lib/src/z80/__mulsint2slong.s):
;; там знак множителей проверяется по биту 7 МЛАДШЕГО байта (bit 7,l / bit 7,e),
;; и (int32_t)a * b считается неверно, если младший байт >= #80 (10 * 182 = -653220).
;; Здесь — старший байт (h, d). Оба символа модуля определены тут, поэтому
;; библиотечный модуль компоновщик не подключает. project_docs/findings_log.md.
;; Здесь же __mullong вместо C-модуля _mullong библиотеки (~2600 тактов, perf/logic.md П7).
;;
;; int32_t __mulsint2slong(int16_t hl, int16_t de)   sdcccall(1): результат HLDE
;; uint32_t __muluint2ulong(uint16_t hl, uint16_t de)
;; uint32_t _mullong(uint32_t HLDE, uint32_t в стеке) — младшие 32 бита произведения
;;
;; 16 x 16 считается по байтам множителя (umul16): байт — 8 шагов «сдвиг и сложение», где
;; A — и множитель, и старший байт произведения. Множитель < 256 (у любого из двух) — один
;; проход, ~350 тактов; иначе два, ~720 (прежний цикл по 16 битам через IY — ~1150).

	.module mul32
	.optsdcc -mz80 sdcccall(1)

	.globl	___mulsint2slong
	.globl	___muluint2ulong
	.globl	__mullong
	.globl	__mul16
	.globl	neg32
	.globl	umul16			; внутренние метки — глобальные ради профилировщика (07 §6)
	.globl	mul8
	.globl	ml_sign
	.globl	ml_full
	.globl	ml_abs

	.area	_CODE

___mulsint2slong:
	ld	bc, #0			; c бит 0 — результат отрицательный; b = 0
	bit	#7, h
	jr	z, hl_pos
	ld	a, b
	sub	a, l
	ld	l, a
	ld	a, b
	sbc	a, h
	ld	h, a
	inc	c
hl_pos:
	bit	#7, d
	jr	z, de_pos
	ld	a, b
	sub	a, e
	ld	e, a
	ld	a, b
	sbc	a, d
	ld	d, a
	inc	c
de_pos:
	push	bc
	call	___muluint2ulong
	pop	bc
	bit	#0, c
	ret	z
	jp	neg32

___muluint2ulong:
	ld	b, h
	ld	c, l
	;; дальше — umul16

;; DE * BC -> HL:DE (HL — старшее слово). AF, BC портятся
umul16:
	ld	a, b
	or	a
	jr	z, 2$			; множитель < 256
	ld	a, d
	or	a
	jr	nz, 1$
	ld	a, e			; множимое < 256 — поменять местами
	ld	d, b
	ld	e, c
	jr	3$
1$:	ld	a, b			; два прохода: DE * старший байт, DE * младший
	call	mul8			; A:HL (C не трогается)
	push	hl
	ld	b, a
	ld	a, c
	ld	c, b			; C = байт 2 первого произведения
	call	mul8
	ld	b, l			; байт 0 результата
	ld	l, h
	ld	h, a
	pop	de
	add	hl, de			; байты 2:1
	ld	a, c
	adc	a, #0			; байт 3
	ld	d, l
	ld	e, b
	ld	l, h
	ld	h, a
	ret
2$:	ld	a, c
3$:	call	mul8
	ex	de, hl
	ld	l, a
	ld	h, #0
	ret

;; A * DE -> A:HL (24 бита). Младшие биты A по мере сдвига копят старший байт произведения:
;; частичное произведение после k шагов < 2^(16+k), до битов множителя перенос не доходит.
;; Восемь шагов развёрнуто (без djnz: ~37 тактов на бит вместо ~50). F портятся; B, C, DE — нет
mul8:
	ld	hl, #0
	add	hl, hl
	rla
	jr	nc, 1$
	add	hl, de
	adc	a, #0
1$:	add	hl, hl
	rla
	jr	nc, 2$
	add	hl, de
	adc	a, #0
2$:	add	hl, hl
	rla
	jr	nc, 3$
	add	hl, de
	adc	a, #0
3$:	add	hl, hl
	rla
	jr	nc, 4$
	add	hl, de
	adc	a, #0
4$:	add	hl, hl
	rla
	jr	nc, 5$
	add	hl, de
	adc	a, #0
5$:	add	hl, hl
	rla
	jr	nc, 6$
	add	hl, de
	adc	a, #0
6$:	add	hl, hl
	rla
	jr	nc, 7$
	add	hl, de
	adc	a, #0
7$:	add	hl, hl
	rla
	jr	nc, 8$
	add	hl, de
	adc	a, #0
8$:	ret

;; uint32_t _mullong(uint32_t a, uint32_t b). Малые отрицательные (-32768..-1, старшее слово
;; #FFFF) берутся по модулю со сменой знака результата — тогда хватает 16 x 16. Иначе
;; a_lo * b_lo + ((a_hi * b_lo + a_lo * b_hi) << 16) — перекрёстные через __mul16 библиотеки
__mullong:
	exx
	pop	bc			; адрес возврата
	pop	de			; b -> HL':DE'
	pop	hl
	push	hl
	push	de
	push	bc
	xor	a			; бит 0 — сменить знак результата
	call	ml_abs
	exx
	call	ml_abs
	push	af
	ld	a, h
	or	l
	exx
	or	h
	or	l
	exx
	jr	nz, ml_full
	exx
	push	de
	exx
	pop	bc
	call	umul16			; a_lo * b_lo
ml_sign:
	pop	af
	rra
	ret	nc
	jp	neg32
ml_full:
	push	de			; [a_lo]
	exx
	push	de			; [b_lo]
	push	hl			; [b_hi]
	exx
	ex	de, hl			; DE = a_hi, HL = a_lo
	pop	bc			; BC = b_hi
	push	de			; [a_hi]
	ex	de, hl			; DE = a_lo
	call	__mul16			; DE = a_lo * b_hi (16 бит)
	pop	hl			; a_hi
	pop	bc			; b_lo
	push	bc
	push	de			; [t] [b_lo] [a_lo] [знак]
	ex	de, hl
	call	__mul16			; DE = a_hi * b_lo
	pop	hl
	add	hl, de			; t
	pop	bc			; b_lo
	pop	de			; a_lo
	push	hl
	call	umul16
	pop	bc
	add	hl, bc
	jr	ml_sign

;; HL:DE в -32768..-1 -> HL = 0, DE = модуль, A ^= 1. B портится
ml_abs:
	ld	b, a
	ld	a, h
	and	l
	inc	a
	ld	a, b
	ret	nz
	bit	7, d
	ret	z
	ld	hl, #0
	or	a
	sbc	hl, de
	ex	de, hl
	ld	hl, #0
	xor	#1
	ret
