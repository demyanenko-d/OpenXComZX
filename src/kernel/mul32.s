;; Замена модуля __mulsint2slong библиотеки SDCC 4.5 (lib/src/z80/__mulsint2slong.s):
;; там знак множителей проверяется по биту 7 МЛАДШЕГО байта (bit 7,l / bit 7,e),
;; и (int32_t)a * b считается неверно, если младший байт >= #80 (10 * 182 = -653220).
;; Здесь — старший байт (h, d). Оба символа модуля определены тут, поэтому
;; библиотечный модуль компоновщик не подключает. project_docs/findings_log.md.
;;
;; int32_t __mulsint2slong(int16_t hl, int16_t de)   sdcccall(1): результат HLDE
;; uint32_t __muluint2ulong(uint16_t hl, uint16_t de)

	.module mul32
	.optsdcc -mz80 sdcccall(1)

	.globl	___mulsint2slong
	.globl	___muluint2ulong

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
	ld	a, b			; результат = -результат
	sub	a, e
	ld	e, a
	ld	a, b
	sbc	a, d
	ld	d, a
	ld	a, b
	sbc	a, l
	ld	l, a
	ld	a, b
	sbc	a, h
	ld	h, a
	ret

;; 16x16 -> 32 (сдвиг и сложение): HL:IY — результат, DE — множимое
___muluint2ulong:
	ld	iy, #0
	ld	b, #16
mu_loop:
	add	iy, iy
	adc	hl, hl
	jr	nc, mu_skip
	add	iy, de
	jr	nc, mu_skip
	inc	hl
mu_skip:
	djnz	mu_loop
	push	iy
	pop	de
	ret
