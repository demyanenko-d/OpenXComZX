;; Вывод строк глифа 4 бит/точка на экран 256c (быстрый путь text.c).
;;
;; void glyph_run(void)  — параметры в переменных (вызывает draw_glyph):
;;   _gr_dst  — адрес первой строки на экране (Win3, страница уже подключена)
;;   _gr_src  — строки глифа (шрифт в Win2), после вызова — следующая строка
;;   _gr_bpr  — байт на строку глифа ((w + 1) / 2)
;;   _gr_rows — строк (все в одной странице экрана: строка экрана — 512 байт)
;;   _gr_col  — цвет; _gr_inv — 1: инверсия (нажатая кнопка)
;; Точка = цвет + уровень (обычный текст) или цвет + 6 - уровень (инверсия),
;; уровень 0 — прозрачно. Код самомодифицирующийся (страница ядра в ОЗУ;
;; запись CPU сбрасывает строку кэша).

	.module glyph
	.globl	_glyph_run

	.area	_DATA
_gr_dst::	.ds	2
_gr_src::	.ds	2
_gr_bpr::	.ds	1
_gr_rows::	.ds	1
_gr_col::	.ds	1
_gr_inv::	.ds	1

	.area	_CODE

_glyph_run::
	;; настроить операцию над уровнем: add a,#c / nop / nop  или  sub a,#c+6 / neg
	ld	a, (_gr_col)
	ld	d, a
	ld	e, #0xC6		; ADD A,n
	ld	bc, #0x0000		; NOP NOP
	ld	a, (_gr_inv)
	or	a, a
	jr	z, gr_setop
	ld	a, d
	add	a, #6
	ld	d, a
	ld	e, #0xD6		; SUB n
	ld	bc, #0x44ED		; NEG (ED 44)
gr_setop:
	ld	a, e
	ld	(gr_p1), a
	ld	(gr_p2), a
	ld	a, d
	ld	(gr_p1 + 1), a
	ld	(gr_p2 + 1), a
	ld	(gr_p1 + 2), bc
	ld	(gr_p2 + 2), bc

	ld	hl, (_gr_dst)
	ld	de, (_gr_src)
	ld	a, (_gr_rows)
gr_row:
	push	af
	push	hl
	ld	a, (_gr_bpr)
	ld	b, a
gr_byte:
	ld	a, (de)
	inc	de
	or	a, a
	jr	z, gr_zero		; обе точки прозрачные
	ld	c, a
	rrca
	rrca
	rrca
	rrca
	and	a, #0x0F
	jr	z, gr_skip1
gr_p1:
	add	a, #0
	nop
	nop
	ld	(hl), a
gr_skip1:
	inc	hl
	ld	a, c
	and	a, #0x0F
	jr	z, gr_skip2
gr_p2:
	add	a, #0
	nop
	nop
	ld	(hl), a
gr_skip2:
	inc	hl
	djnz	gr_byte
	jr	gr_next
gr_zero:
	inc	hl
	inc	hl
	djnz	gr_byte
gr_next:
	pop	hl
	ld	bc, #512
	add	hl, bc
	pop	af
	dec	a
	jr	nz, gr_row
	ld	(_gr_src), de
	ret
