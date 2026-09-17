;; Резидентные помощники описаний экранов (Win0, ассемблер; был scrutil.c):
;; принимают указатели на константы банка вызывающего (таблицу экранов, ширины
;; колонок) — поэтому не в банке. ABI — src/kernel/win0/pages.s; 8-битные аргументы
;; после 16-битного — в стеке по байту, снимает вызываемая функция.
;; scr_t = { u8 id; sdef_t s (6: ui, palui, bg(2), flags, n); const wdef_t *w } — 9 байт,
;; wdef_t — 16 байт.

	.module scrutil
	.globl	_scr_find
	.globl	_list_cols_ext

	.area	_DATA
sf_s:	.ds	2
sf_w:	.ds	2
lc_n:	.ds	1
lc_w:	.ds	2
lc_al:	.ds	2

	.area	_CODE

;; uint8_t scr_find(const scr_t *t /*HL*/, uint8_t n, uint8_t id, sdef_t *s, wdef_t *w)
;; найти экран id в таблице из n записей: s = описание, w = копия виджетов; 1 — есть
_scr_find::
	pop	iy			; адрес возврата
	pop	bc			; C = n, B = id
	pop	de
	ld	(sf_s), de
	pop	de
	ld	(sf_w), de
	push	iy
	ld	a, c
	or	a
	jr	z, sf_no
sf_loop:
	ld	a, (hl)
	cp	b
	jr	z, sf_hit
	ld	de, #9
	add	hl, de
	dec	c
	jr	nz, sf_loop
sf_no:
	xor	a
	ret
sf_hit:
	inc	hl			; &t[i].s
	ld	de, (sf_s)
	push	hl
	ld	bc, #6
	ldir
	pop	hl
	ld	bc, #5
	add	hl, bc
	ld	a, (hl)			; s.n — виджетов
	inc	hl
	ld	e, (hl)
	inc	hl
	ld	d, (hl)			; DE = t[i].w
	ld	l, a
	ld	h, #0
	add	hl, hl
	add	hl, hl
	add	hl, hl
	add	hl, hl			; n * 16
	ld	b, h
	ld	c, l
	ex	de, hl
	ld	de, (sf_w)
	ld	a, b
	or	c
	jr	z, 1$
	ldir
1$:	ld	a, #1
	ret

;; void list_cols_ext(char *buf /*HL*/, uint8_t n, const uint8_t *w, const uint8_t *align,
;;                    uint8_t arrows, uint8_t horiz)
;; buf = n, ширины[n], выравнивания[n] (align = 0 — нули), arrows, horiz
_list_cols_ext::
	pop	iy
	dec	sp
	pop	af			; A = n
	ld	(lc_n), a
	pop	de
	ld	(lc_w), de
	pop	de
	ld	(lc_al), de
	pop	de			; E = arrows, D = horiz
	push	iy
	push	de
	ld	(hl), a
	inc	hl
	or	a
	jr	z, 3$
	ld	c, a
	ld	b, #0
	ex	de, hl
	ld	hl, (lc_w)
	ldir
	ex	de, hl			; HL = buf + 1 + n
	ld	a, (lc_n)
	ld	b, a
	ld	de, (lc_al)
	ld	a, d
	or	e
	jr	z, 2$
1$:	ld	a, (de)
	ld	(hl), a
	inc	de
	inc	hl
	djnz	1$
	jr	3$
2$:	ld	(hl), #0
	inc	hl
	djnz	2$
3$:	pop	de
	ld	(hl), e
	inc	hl
	ld	(hl), d
	ret
