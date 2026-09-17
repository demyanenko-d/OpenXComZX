;; Отладочный вывод в консоль эмулятора (Win0, ассемблер; был dbg.c).
;; Порты #F8AF — символ, #F9AF — байт hex (project_docs/07_debug_tools.md);
;; на железе запись игнорируется. ABI — pages.s.

	.module dbg
	.globl	_dbg_puts
	.globl	_dbg_dec
	.globl	_dbg_hex8

	.area	_DATA
dd_buf:	.ds	11

	.area	_CODE

;; void dbg_puts(const char *s /*HL*/)
_dbg_puts::
	ld	bc, #0xF8AF
1$:	ld	a, (hl)
	or	a
	ret	z
	out	(c), a
	inc	hl
	jr	1$

;; void dbg_hex8(uint8_t v /*A*/)
_dbg_hex8::
	ld	bc, #0xF9AF
	out	(c), a
	ret

;; void dbg_dec(uint32_t v /*DE — младшее, HL — старшее*/)
_dbg_dec::
	ld	bc, #dd_buf + 10
	xor	a
	ld	(bc), a
1$:	push	bc
	call	div10
	pop	bc
	add	a, #'0'
	dec	bc
	ld	(bc), a
	ld	a, h
	or	l
	or	d
	or	e
	jr	nz, 1$
	ld	h, b
	ld	l, c
	jr	_dbg_puts

;; HL:DE /= 10, A = остаток (сдвиг-вычитание, 32 шага)
div10:
	xor	a
	ld	b, #32
1$:	sla	e
	rl	d
	rl	l
	rl	h
	rla
	cp	#10
	jr	c, 2$
	sub	#10
	inc	e
2$:	djnz	1$
	ret
