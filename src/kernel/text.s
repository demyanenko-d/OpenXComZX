;; Строки и вывод текста (Win0, ассемблер; был text.c). Шрифты — в своей странице
;; пула (заполняет text_init, boot.c): при рисовании страница шрифта подключается в
;; Win2 (банк вызывающего потом восстанавливается), экран — через Win3 (gfx_map).
;; Входная строка сначала копируется в tx_s (Win1) — литералы и константы банка
;; вызывающего пропадают из Win2 вместе с его банком.
;; Пиксель глифа — уровень 0..5; цвет = color + уровень*mul (+ 2*(mid - уровень) при
;; инверсии), как PaletteShift в OpenXcom Text::draw. Быстрый путь — glyph.s.
;; Формат шрифта (09 §7): cell_w, cell_h, spacing(i8), first, n(u16); n x {w, off16};
;; глифы — cell_h строк по (w+1)/2 байт. Строки (STR): u16 n, n x 3-байтное смещение
;; (#FFFFFF — нет), строки с нулём.
;; ABI SDCC --sdcccall 1 (src/kernel/pages.s). tbox_t: x0 y2 w4 h6 font8 color9
;; color2 10 flags11.

	.module text
	.globl	_text_draw
	.globl	_text_height
	.globl	_text_width
	.globl	_font_height
	.globl	_str_get
	.globl	_str_copy
	.globl	_str_fmt
	.globl	_str_plural
	.globl	_fmt_num
	.globl	_tx_font_page
	.globl	_tx_font_off
	.globl	_tx_font_cw
	.globl	_tx_font_ch
	.globl	_tx_font_sp
	.globl	_tx_str_base
	.globl	_tx_str_n
	.globl	_tx_skip
	.globl	_glyph_run
	.globl	_gr_dst
	.globl	_gr_src
	.globl	_gr_bpr
	.globl	_gr_rows
	.globl	_gr_col
	.globl	_gr_inv
	.globl	_gfx_map
	.globl	_pg_win3
	.globl	_pg_map3
	.globl	_get_bank
	.globl	_set_bank
	.globl	_far_read

PAGE2_PORT	= 0x12AF
PAGE3_PORT	= 0x13AF
SCREEN_W	= 320
SCREEN_H	= 200
MAX_LINES	= 24
TXS_MAX		= 1023
FNT_BIG		= 0
FNT_SMALL	= 1
FNT_GEO_BIG	= 2
FNT_GEO_SMALL	= 3
TX_CENTER	= 0x01
TX_RIGHT	= 0x02
TX_MIDDLE	= 0x04
TX_BOTTOM	= 0x08
TX_WRAP		= 0x10
TX_CONTRAST	= 0x20
TX_INVERT	= 0x40

	.area	_DATA
_tx_font_page::	.ds	1		; страница со всеми шрифтами (text_init)
_tx_font_off::	.ds	8		; u16 x 4 — смещение шрифта в странице
_tx_font_cw::	.ds	4
_tx_font_ch::	.ds	4
_tx_font_sp::	.ds	4		; i8
_tx_str_base::	.ds	4		; far_t ресурса STRINGS
_tx_str_n::	.ds	2
_tx_skip::	.ds	1		; text_draw: пропустить первые строки раскладки (один вызов)
strbuf:		.ds	1024		; str_get
tx_s:		.ds	TXS_MAX + 1	; копия выводимой строки
tx_ls:		.ds	2 * MAX_LINES	; начало, конец, ширина, шрифт строк раскладки
tx_le:		.ds	2 * MAX_LINES
tx_lw:		.ds	2 * MAX_LINES
tx_lf:		.ds	MAX_LINES
tx_lut:		.ds	16
tx_fbase:	.ds	2		; #8000 + смещение текущего шрифта
tx_f:		.ds	1		; текущий шрифт
tx_box:		.ds	2		; tbox_t *
tx_n:		.ds	2		; длина tx_s
tx_bank:	.ds	1
tx_old3:	.ds	1
tx_nl:		.ds	1
tx_maxw:	.ds	2
tx_th:		.ds	2
tx_x:		.ds	2
tx_y:		.ds	2
tx_l:		.ds	1		; номер строки при выводе
tx_k:		.ds	2		; позиция в tx_s
tx_ke:		.ds	2		; конец строки
tx_color:	.ds	1
tx_mul:		.ds	1
tx_mid:		.ds	1
tx_lutok:	.ds	1
box_x:		.ds	2		; рамка: x, x+w, y, y+h
box_r:		.ds	2
box_y:		.ds	2
box_b:		.ds	2
ly_i:		.ds	2		; раскладка
ly_start:	.ds	2
ly_lastsp:	.ds	2
ly_w:		.ds	2
ly_wsp:		.ds	2
g_x:		.ds	2		; глиф
g_y:		.ds	2
g_w:		.ds	1
g_h:		.ds	1
g_bpr:		.ds	1
g_left:		.ds	1
g_src:		.ds	2
g_px:		.ds	2
g_dst:		.ds	2
g_xx:		.ds	1
fn_u:		.ds	4		; fmt_num
fn_sep:		.ds	1
fn_started:	.ds	1
fn_p:		.ds	2
fn_out:		.ds	2
sf_a0:		.ds	2		; str_fmt
sf_a1:		.ds	2
sf_lim:		.ds	2
sp_n:		.ds	4		; str_plural
sp_out:		.ds	2
sp_one:		.ds	2
tx_num:		.ds	12
sc_max:		.ds	2		; str_copy
sc_dst:		.ds	2
sc_id:		.ds	2
sc_e:		.ds	3
sc_p:		.ds	4
sc_pg:		.ds	1
sc_old:		.ds	1

	.area	_CODE

tx_empty:	.db	0

;; ================================================================ общие

;; CF = 1, если HL < DE (со знаком). Портит A, HL.
lt_s:
	ld	a, h
	xor	d
	jp	p, 1$
	ld	a, h
	rla
	ret
1$:	or	a
	sbc	hl, de
	ret

;; HL = HL / 2 со знаком, к нулю (как C)
div2s:
	bit	7, h
	jr	z, 1$
	inc	hl
1$:	sra	h
	rr	l
	ret

;; Копия строки DE -> tx_s (до TXS_MAX символов), tx_n = длина
copy_s:
	ld	hl, #tx_s
	ld	bc, #0
1$:	ld	a, (de)
	or	a
	jr	z, 2$
	ld	(hl), a
	inc	hl
	inc	de
	inc	bc
	ld	a, b
	cp	#0x03			; TXS_MAX = #3FF
	jr	nz, 1$
	ld	a, c
	cp	#0xFF
	jr	nz, 1$
2$:	ld	(hl), #0
	ld	(tx_n), bc
	ret

;; uint8_t font_height(uint8_t f /*A*/) — высота строки (высота + spacing)
_font_height::
	ld	e, a
	ld	d, #0
	ld	hl, #_tx_font_ch
	add	hl, de
	ld	a, (hl)
	ld	hl, #_tx_font_sp
	add	hl, de
	add	a, (hl)
	ret

;; Подключить шрифт A в Win2: tx_f, tx_fbase. Портит BC, DE, HL.
map_font:
	ld	(tx_f), a
	add	a, a
	ld	e, a
	ld	d, #0
	ld	hl, #_tx_font_off
	add	hl, de
	ld	e, (hl)
	inc	hl
	ld	d, (hl)
	ld	hl, #0x8000
	add	hl, de
	ld	(tx_fbase), hl
	ld	a, (_tx_font_page)
	ld	bc, #PAGE2_PORT
	out	(c), a
	ret

;; A = символ -> HL = запись глифа {w, off16} (fbase + 6 + idx*3). Портит DE.
glyph_entry:
	cp	#0x80
	jr	c, 1$
	sub	#33			; c - #80 + 95
	jr	2$
1$:	sub	#0x20
2$:	ld	l, a
	ld	h, #0
	ld	e, l
	ld	d, h
	add	hl, hl
	add	hl, de
	ld	de, (tx_fbase)
	add	hl, de
	ld	de, #6
	add	hl, de
	ret

;; A = поле шрифта tx_f из таблицы HL (cw / sp)
font_field:
	ld	a, (tx_f)
	ld	e, a
	ld	d, #0
	add	hl, de
	ld	a, (hl)
	ret

;; A = символ -> A = шаг (текущий шрифт tx_f). Портит DE, HL.
char_adv:
	cp	#' '
	jr	nz, 1$
	ld	hl, #_tx_font_cw
	call	font_field
	srl	a
	ret
1$:	cp	#0x04			; неразрывный пробел — четверть ячейки
	jr	nz, 2$
	ld	hl, #_tx_font_cw
	call	font_field
	srl	a
	srl	a
	ret
2$:	cp	#0x20
	jr	nc, 3$
	xor	a
	ret
3$:	call	glyph_entry
	ld	a, (hl)
	push	af
	ld	hl, #_tx_font_sp
	call	font_field
	ld	e, a
	pop	af
	add	a, e
	ret

;; ================================================================ ширина, высота

;; int16_t text_width(uint8_t f /*A*/, const char *s /*DE*/) — до конца строки
;; или перевода строки
_text_width::
	push	af
	call	copy_s
	call	_get_bank
	ld	(tx_bank), a
	pop	af
	call	map_font
	ld	hl, #tx_s
	ld	bc, #0
1$:	ld	a, (hl)
	or	a
	jr	z, 2$
	cp	#0x0A
	jr	z, 2$
	cp	#0x02
	jr	z, 2$
	push	hl
	push	bc
	call	char_adv
	pop	bc
	pop	hl
	add	a, c
	ld	c, a
	ld	a, b
	adc	a, #0
	ld	b, a
	inc	hl
	jr	1$
2$:	push	bc
	ld	a, (tx_bank)
	call	_set_bank
	pop	de
	ret

;; Строка раскладки: HL = начало, DE = конец, BC = ширина; шрифт — tx_f.
;; Если строк меньше MAX_LINES — записать; всегда maxw = max(maxw, ширина).
add_line:
	ld	a, (tx_nl)
	cp	#MAX_LINES
	jr	nc, al_max
	push	bc
	push	de
	push	hl
	ld	l, a
	ld	h, #0
	add	hl, hl
	ex	de, hl			; DE = nl * 2
	ld	hl, #tx_ls
	add	hl, de
	pop	bc
	ld	(hl), c
	inc	hl
	ld	(hl), b
	ld	hl, #tx_le
	add	hl, de
	pop	bc
	ld	(hl), c
	inc	hl
	ld	(hl), b
	ld	hl, #tx_lw
	add	hl, de
	pop	bc
	ld	(hl), c
	inc	hl
	ld	(hl), b
	push	bc
	ld	a, (tx_nl)
	ld	e, a
	ld	d, #0
	ld	hl, #tx_lf
	add	hl, de
	ld	a, (tx_f)
	ld	(hl), a
	ld	a, (tx_nl)
	inc	a
	ld	(tx_nl), a
	pop	bc
al_max:
	ld	hl, (tx_maxw)
	or	a
	sbc	hl, bc
	ret	nc
	ld	(tx_maxw), bc
	ret

;; Разбивка tx_s на строки (Text::processText): переводы строк, #02 (переход на
;; мелкий), перенос по словам при TX_WRAP. A = шрифт. -> tx_nl, tx_maxw, tx_l*.
layout:
	call	map_font
	xor	a
	ld	(tx_nl), a
	ld	hl, #0
	ld	(tx_maxw), hl
	ld	(ly_i), hl
	ld	(ly_start), hl
	ld	(ly_w), hl
	ld	(ly_wsp), hl
	dec	hl
	ld	(ly_lastsp), hl
ly_loop:
	ld	hl, (ly_i)
	ld	de, #tx_s
	add	hl, de
	ld	a, (hl)
	or	a
	jp	z, ly_eol
	cp	#0x0A
	jp	z, ly_eol
	cp	#0x02
	jp	z, ly_eol
	cp	#' '
	jr	nz, 1$
	ld	hl, (ly_i)
	ld	(ly_lastsp), hl
	ld	hl, (ly_w)
	ld	(ly_wsp), hl
	ld	a, #' '
1$:	call	char_adv
	ld	e, a
	ld	d, #0
	ld	hl, (ly_w)
	add	hl, de
	ld	(ly_w), hl
	ld	de, (tx_box)		; перенос: TX_WRAP, w > b->w, был пробел, есть место
	ld	hl, #11
	add	hl, de
	ld	a, (hl)
	and	#TX_WRAP
	jr	z, ly_next
	ld	hl, #4
	add	hl, de
	ld	e, (hl)
	inc	hl
	ld	d, (hl)
	ld	hl, (ly_w)
	or	a
	sbc	hl, de
	jr	c, ly_next
	jr	z, ly_next
	ld	hl, (ly_lastsp)
	ld	a, h
	and	l
	inc	a
	jr	z, ly_next
	ld	a, (tx_nl)
	cp	#MAX_LINES
	jr	nc, ly_next
	ld	hl, (ly_start)
	ld	de, (ly_lastsp)
	ld	bc, (ly_wsp)
	call	add_line
	ld	hl, (ly_lastsp)
	ld	(ly_i), hl
	inc	hl
	ld	(ly_start), hl
	ld	hl, #0
	ld	(ly_w), hl
	dec	hl
	ld	(ly_lastsp), hl
ly_next:
	ld	hl, (ly_i)
	inc	hl
	ld	(ly_i), hl
	jp	ly_loop
ly_eol:					; A = 0 / #0A / #02
	push	af
	ld	hl, (ly_start)
	ld	de, (ly_i)
	ld	bc, (ly_w)
	call	add_line
	pop	af
	or	a
	ret	z
	cp	#0x02
	jr	nz, 3$
	ld	a, (tx_f)
	cp	#FNT_GEO_BIG
	ld	a, #FNT_GEO_SMALL
	jr	z, 2$
	ld	a, #FNT_SMALL
2$:	call	map_font
3$:	ld	hl, (ly_i)
	inc	hl
	ld	(ly_i), hl
	ld	(ly_start), hl
	ld	hl, #0
	ld	(ly_w), hl
	dec	hl
	ld	(ly_lastsp), hl
	jp	ly_loop

;; tx_th = сумма высот строк раскладки
calc_th:
	ld	hl, #0
	ld	(tx_th), hl
	ld	a, (tx_nl)
	or	a
	ret	z
	ld	b, a
	ld	hl, #tx_lf
1$:	ld	a, (hl)
	push	hl
	push	bc
	call	_font_height
	ld	hl, (tx_th)
	ld	e, a
	ld	d, #0
	add	hl, de
	ld	(tx_th), hl
	pop	bc
	pop	hl
	inc	hl
	djnz	1$
	ret

;; Text::setText: крупный текст, который не влез, — мелким (кроме строк на '.')
fit_check:
	ld	hl, (tx_box)
	ld	de, #8
	add	hl, de
	ld	a, (hl)
	or	a			; FNT_BIG
	ret	nz
	ld	hl, (tx_n)
	ld	a, h
	or	l
	ret	z
	ld	de, #tx_s - 1
	add	hl, de
	ld	a, (hl)
	cp	#'.'
	ret	z
	ld	hl, (tx_box)
	ld	de, #4
	add	hl, de
	ld	e, (hl)
	inc	hl
	ld	d, (hl)			; DE = w
	inc	hl
	ld	c, (hl)
	inc	hl
	ld	b, (hl)			; BC = h
	ld	hl, (tx_maxw)
	or	a
	sbc	hl, de
	jr	z, 1$
	jr	nc, 2$			; maxw > w
1$:	ld	hl, (tx_th)
	or	a
	sbc	hl, bc
	ret	c
	ret	z			; th <= h — влезает
2$:	ld	a, #FNT_SMALL
	call	layout
	jp	calc_th

;; Раскладка tx_s в рамке tx_box: шрифт рамки, при нужде — мелкий; -> tx_th
layout_box:
	ld	hl, (tx_box)
	ld	de, #8
	add	hl, de
	ld	a, (hl)
	call	layout
	call	calc_th
	jp	fit_check

;; int16_t text_height(const tbox_t *b /*HL*/, const char *s /*DE*/)
_text_height::
	ld	(tx_box), hl
	call	copy_s
	call	_get_bank
	ld	(tx_bank), a
	call	layout_box
	ld	a, (tx_bank)
	call	_set_bank
	ld	de, (tx_th)
	ret

;; ================================================================ вывод

;; Цвет текста: tx_color (A), mul, mid; таблица lut — лениво (медленный путь)
set_lut:
	ld	(tx_color), a
	ld	(_gr_col), a
	ld	a, (tx_mid)
	or	a
	jr	z, 1$
	ld	a, #1
1$:	ld	(_gr_inv), a
	xor	a
	ld	(tx_lutok), a
	ret

make_lut:				; lut[v] = color + v*mul + (mid ? 2*(mid - v) : 0)
	ld	hl, #tx_lut + 1
	ld	c, #1
1$:	ld	a, (tx_mul)
	ld	b, a
	xor	a
2$:	add	a, c
	djnz	2$			; A = v * mul
	ld	b, a
	ld	a, (tx_color)
	add	a, b
	ld	b, a
	ld	a, (tx_mid)
	or	a
	jr	z, 3$
	sub	c
	add	a, a
	add	a, b
	ld	b, a
3$:	ld	(hl), b
	inc	hl
	inc	c
	ld	a, c
	cp	#16
	jr	nz, 1$
	ld	a, #1
	ld	(tx_lutok), a
	ret

;; void text_draw(const tbox_t *b /*HL*/, const char *s /*DE*/)
_text_draw::
	push	ix			; IX — указатель кадра вызывающего (сохранять)
	ld	(tx_box), hl
	call	copy_s
	call	_get_bank
	ld	(tx_bank), a
	call	_pg_win3
	ld	(tx_old3), a
	ld	hl, (tx_box)		; рамка: box_x/r/y/b
	ld	e, (hl)
	inc	hl
	ld	d, (hl)
	inc	hl
	ld	(box_x), de
	ld	c, (hl)
	inc	hl
	ld	b, (hl)
	inc	hl
	ld	(box_y), bc
	ld	a, (hl)
	inc	hl
	push	hl
	ld	h, (hl)
	ld	l, a
	add	hl, de
	ld	(box_r), hl
	pop	hl
	inc	hl
	ld	a, (hl)
	inc	hl
	ld	h, (hl)
	ld	l, a
	add	hl, bc
	ld	(box_b), hl
	call	layout_box
	;; y по вертикали: TX_MIDDLE — (h - th + 1) / 2, TX_BOTTOM — h - th
	ld	hl, (box_y)
	ld	(tx_y), hl
	ld	ix, (tx_box)
	ld	a, 11 (ix)
	ld	b, a
	and	#TX_MIDDLE | TX_BOTTOM
	jr	z, td_col
	ld	l, 6 (ix)
	ld	h, 7 (ix)
	ld	de, (tx_th)
	or	a
	sbc	hl, de			; h - th
	ld	a, b
	and	#TX_MIDDLE
	jr	z, 1$
	inc	hl
	call	div2s
1$:	ld	de, (tx_y)
	add	hl, de
	ld	(tx_y), hl
td_col:
	ld	a, #1			; mul: TX_CONTRAST — 3
	bit	5, b
	jr	z, 2$
	ld	a, #3
2$:	ld	(tx_mul), a
	xor	a			; mid: TX_INVERT — 3
	bit	6, b
	jr	z, 3$
	ld	a, #3
3$:	ld	(tx_mid), a
	ld	a, 9 (ix)
	call	set_lut
	ld	a, (_tx_skip)		; прокрутка (Text::setScrollable): первые строки — мимо
	ld	(tx_l), a
td_line:
	ld	a, (tx_nl)
	ld	b, a
	ld	a, (tx_l)
	cp	b
	jp	nc, td_done
	ld	e, a
	ld	d, #0
	ld	hl, #tx_lf
	add	hl, de
	ld	a, (hl)			; шрифт строки
	push	af
	call	_font_height
	ld	c, a
	pop	af
	ld	b, a			; B = шрифт, C = высота строки
	ld	a, (tx_l)		; не первая и не влезает — остальное за краем
	or	a
	jr	z, 4$
	push	bc
	ld	hl, (tx_y)
	ld	b, #0
	add	hl, bc
	ex	de, hl			; DE = y + fh
	ld	hl, (box_b)
	call	lt_s			; box_b < y + fh ?
	pop	bc
	jp	c, td_done
4$:	push	bc
	ld	a, b
	call	map_font
	;; x по горизонтали
	ld	a, (tx_l)
	add	a, a
	ld	e, a
	ld	d, #0
	ld	hl, #tx_lw
	add	hl, de
	ld	c, (hl)
	inc	hl
	ld	b, (hl)			; BC = ширина строки
	push	de
	ld	ix, (tx_box)
	ld	a, 11 (ix)
	ld	l, 4 (ix)
	ld	h, 5 (ix)		; HL = w
	bit	0, a
	jr	z, 5$
	push	hl			; TX_CENTER: (w + sp - lw + 1) / 2
	ld	hl, #_tx_font_sp
	call	font_field
	pop	hl
	ld	e, a
	rlca
	sbc	a, a
	ld	d, a			; DE = sp со знаком
	add	hl, de
	or	a
	sbc	hl, bc
	inc	hl
	call	div2s
	jr	6$
5$:	bit	1, a
	jr	z, 7$
	dec	hl			; TX_RIGHT: w - 1 - lw
	or	a
	sbc	hl, bc
	jr	6$
7$:	ld	hl, #0
6$:	ld	de, (box_x)
	add	hl, de
	ld	(tx_x), hl
	pop	de			; DE = l * 2
	ld	hl, #tx_ls
	add	hl, de
	ld	c, (hl)
	inc	hl
	ld	b, (hl)
	ld	(tx_k), bc
	ld	hl, #tx_le
	add	hl, de
	ld	c, (hl)
	inc	hl
	ld	b, (hl)
	ld	(tx_ke), bc
td_char:
	ld	hl, (tx_k)
	ld	de, (tx_ke)
	or	a
	sbc	hl, de
	jr	nc, td_eol
	ld	hl, (tx_k)
	ld	de, #tx_s
	add	hl, de
	ld	a, (hl)
	cp	#0x01			; {ALT}: второй цвет и обратно
	jr	nz, 8$
	ld	ix, (tx_box)
	ld	a, (tx_color)
	cp	9 (ix)
	ld	a, 10 (ix)
	jr	z, 9$
	ld	a, 9 (ix)
9$:	call	set_lut
	jr	td_nextc
8$:	push	af
	cp	#0x21			; пробел и управляющие — без глифа
	jr	c, 10$
	cp	#0x04
	jr	z, 10$
	call	draw_glyph
10$:	pop	af
	call	char_adv
	ld	e, a
	ld	d, #0
	ld	hl, (tx_x)
	add	hl, de
	ld	(tx_x), hl
td_nextc:
	ld	hl, (tx_k)
	inc	hl
	ld	(tx_k), hl
	jr	td_char
td_eol:
	pop	bc			; C = высота строки
	ld	hl, (tx_y)
	ld	b, #0
	add	hl, bc
	ld	(tx_y), hl
	ld	hl, #tx_l
	inc	(hl)
	jp	td_line
td_done:
	pop	ix
	xor	a
	ld	(_tx_skip), a		; только на этот вызов
	ld	a, (tx_old3)
	call	_pg_map3
	ld	a, (tx_bank)
	jp	_set_bank

;; Глиф A в (tx_x, tx_y) с отсечением рамкой tx_box и экраном. Целиком внутри и
;; mul = 1 — glyph_run по кускам в одной странице экрана; иначе — по точкам.
draw_glyph:
	call	glyph_entry
	ld	a, (hl)
	ld	(g_w), a
	inc	a
	srl	a
	ld	(g_bpr), a
	inc	hl
	ld	e, (hl)
	inc	hl
	ld	d, (hl)			; DE = смещение глифа
	ld	hl, (tx_fbase)
	push	hl
	inc	hl
	ld	a, (hl)
	ld	(g_h), a
	inc	hl
	inc	hl
	inc	hl
	ld	c, (hl)			; n (младший байт)
	ld	b, #0
	pop	hl
	add	hl, de
	add	hl, bc
	add	hl, bc
	add	hl, bc
	ld	de, #6
	add	hl, de
	ld	(g_src), hl		; fbase + 6 + n*3 + off
	ld	hl, (tx_x)
	ld	(g_x), hl
	ld	hl, (tx_y)
	ld	(g_y), hl
	ld	a, (tx_mul)
	dec	a
	jp	nz, dg_slow
	;; быстрый путь: x >= box_x, x >= 0, x + w <= box_r, x + w <= 320, то же по y
	ld	hl, (g_x)
	bit	7, h
	jp	nz, dg_slow
	ld	de, (box_x)
	call	lt_s
	jp	c, dg_slow
	ld	hl, (g_x)
	ld	a, (g_w)
	ld	e, a
	ld	d, #0
	add	hl, de
	ex	de, hl			; DE = x + w
	push	de
	ld	hl, (box_r)
	call	lt_s
	pop	de
	jp	c, dg_slow
	ld	hl, #SCREEN_W
	call	lt_s
	jp	c, dg_slow
	ld	hl, (g_y)
	bit	7, h
	jp	nz, dg_slow
	ld	de, (box_y)
	call	lt_s
	jp	c, dg_slow
	ld	hl, (g_y)
	ld	a, (g_h)
	ld	e, a
	ld	d, #0
	add	hl, de
	ex	de, hl			; DE = y + h
	push	de
	ld	hl, (box_b)
	call	lt_s
	pop	de
	jr	c, dg_slow
	ld	hl, #SCREEN_H
	call	lt_s
	jr	c, dg_slow
	ld	hl, (g_src)
	ld	(_gr_src), hl
	ld	a, (g_bpr)
	ld	(_gr_bpr), a
	ld	a, (g_h)
	ld	(g_left), a
dg_fast:
	ld	a, (g_left)
	or	a
	ret	z
	ld	a, (g_y)		; строк до конца страницы экрана: 32 - (y & 31)
	and	#31
	ld	b, a
	ld	a, #32
	sub	b
	ld	b, a
	ld	a, (g_left)
	cp	b
	jr	nc, 1$
	ld	b, a
1$:	ld	a, b
	ld	(_gr_rows), a
	push	bc
	ld	hl, (g_x)
	ld	de, (g_y)
	call	_gfx_map
	ld	(_gr_dst), de
	call	_glyph_run
	pop	bc
	ld	a, (g_left)
	sub	b
	ld	(g_left), a
	ld	hl, (g_y)
	ld	e, b
	ld	d, #0
	add	hl, de
	ld	(g_y), hl
	jr	dg_fast

dg_slow:				; по точкам с отсечением (контраст, край рамки/экрана)
	ld	a, (tx_lutok)
	or	a
	call	z, make_lut
	ld	a, (g_h)
	ld	(g_left), a
ds_row:
	ld	a, (g_left)
	or	a
	ret	z
	;; строка видна: y >= box_y, y < box_b, 0 <= y < 200
	ld	hl, (g_y)
	ld	a, h
	or	a
	jp	nz, ds_nextrow		; y < 0 или y >= 256
	ld	a, l
	cp	#SCREEN_H
	jp	nc, ds_nextrow
	ld	de, (box_y)
	call	lt_s
	jp	c, ds_nextrow		; y < box_y
	ld	hl, (g_y)		; y < box_b: иначе (box_b < y или y == box_b) — мимо
	ex	de, hl
	ld	hl, (box_b)
	call	lt_s
	jp	c, ds_nextrow
	ld	hl, (g_y)
	ld	de, (box_b)
	or	a
	sbc	hl, de
	jp	z, ds_nextrow
	ld	hl, (g_x)
	ld	de, (g_y)
	call	_gfx_map
	ld	(g_dst), de
	ld	hl, (g_x)
	ld	(g_px), hl
	xor	a
	ld	(g_xx), a
ds_px:
	ld	a, (g_w)
	ld	b, a
	ld	a, (g_xx)
	cp	b
	jr	nc, ds_nextrow
	ld	e, a			; уровень: чётная точка — старший полубайт
	srl	e
	ld	d, #0
	ld	hl, (g_src)
	add	hl, de
	ld	c, (hl)
	bit	0, a
	jr	nz, 1$
	srl	c
	srl	c
	srl	c
	srl	c
1$:	ld	a, c
	and	#0x0F
	jr	z, ds_nextpx
	ld	c, a
	;; px в рамке и на экране: box_x <= px < box_r, 0 <= px < 320
	ld	hl, (g_px)
	bit	7, h
	jr	nz, ds_nextpx
	ld	de, #SCREEN_W
	push	hl
	or	a
	sbc	hl, de
	pop	hl
	jr	nc, ds_nextpx
	ld	de, (box_x)
	push	bc
	call	lt_s
	pop	bc
	jr	c, ds_nextpx
	ld	hl, (g_px)
	ld	de, (box_r)
	push	bc
	call	lt_s			; px < box_r
	pop	bc
	jr	nc, ds_nextpx
	ld	b, #0
	ld	hl, #tx_lut
	add	hl, bc
	ld	c, (hl)
	ld	a, (g_xx)
	ld	e, a
	ld	d, #0
	ld	hl, (g_dst)
	add	hl, de
	ld	(hl), c
ds_nextpx:
	ld	hl, (g_px)
	inc	hl
	ld	(g_px), hl
	ld	hl, #g_xx
	inc	(hl)
	jr	ds_px
ds_nextrow:
	ld	hl, (g_src)
	ld	a, (g_bpr)
	ld	e, a
	ld	d, #0
	add	hl, de
	ld	(g_src), hl
	ld	hl, (g_y)
	inc	hl
	ld	(g_y), hl
	ld	hl, #g_left
	dec	(hl)
	jp	ds_row

;; ================================================================ строки

;; far_t (sc_p) += HL (16 бит)
sc_add16:
	ld	de, (sc_p)
	add	hl, de
	ld	(sc_p), hl
	ret	nc
	ld	hl, (sc_p + 2)
	inc	hl
	ld	(sc_p + 2), hl
	ret

;; void str_copy(uint16_t id /*HL*/, char *dst /*DE*/, uint16_t max) — строка по
;; номеру в dst (не больше max - 1 символов); нет строки — "#номер"
_str_copy::
	pop	iy
	pop	bc
	push	iy
	ld	(sc_max), bc
	ld	(sc_dst), de
	ld	(sc_id), hl
	ld	de, (_tx_str_n)
	or	a
	sbc	hl, de
	jp	nc, sc_missing
	;; запись: str_base + 2 + id*3
	ld	hl, (_tx_str_base)
	ld	(sc_p), hl
	ld	hl, (_tx_str_base + 2)
	ld	(sc_p + 2), hl
	ld	hl, (sc_id)
	ld	d, h
	ld	e, l
	add	hl, hl
	add	hl, de
	inc	hl
	inc	hl
	call	sc_add16
	ld	hl, #3
	push	hl
	ld	hl, #sc_e
	push	hl
	ld	de, (sc_p)
	ld	hl, (sc_p + 2)
	call	_far_read
	ld	a, (sc_e)
	ld	hl, #sc_e + 1
	and	(hl)
	inc	hl
	and	(hl)
	inc	a
	jp	z, sc_missing
	;; строка: str_base + 2 + n*3 + смещение (24 бита)
	ld	hl, (_tx_str_base)
	ld	(sc_p), hl
	ld	hl, (_tx_str_base + 2)
	ld	(sc_p + 2), hl
	ld	hl, (_tx_str_n)
	ld	d, h
	ld	e, l
	add	hl, hl
	add	hl, de
	inc	hl
	inc	hl
	call	sc_add16
	ld	hl, (sc_e)
	call	sc_add16
	ld	a, (sc_e + 2)
	ld	hl, #sc_p + 2
	add	a, (hl)
	ld	(hl), a
	inc	hl
	ld	a, #0
	adc	a, (hl)
	ld	(hl), a
	;; страница и указатель в Win3
	ld	de, (sc_p)
	ld	hl, (sc_p + 2)
	ld	a, d
	rlca
	rlca
	and	#3
	ld	b, a
	ld	a, l
	add	a, a
	add	a, a
	or	b
	ld	(sc_pg), a
	ld	a, d
	and	#0x3F
	or	#0xC0
	ld	h, a
	ld	l, e
	push	hl
	call	_pg_win3
	ld	(sc_old), a
	ld	a, (sc_pg)
	ld	bc, #PAGE3_PORT
	out	(c), a
	pop	hl
	ld	de, (sc_dst)
	ld	bc, (sc_max)
	ld	a, b
	or	c
	jr	z, sc_end
	dec	bc
sc_loop:
	ld	a, b
	or	c
	jr	z, sc_end
	ld	a, (hl)
	or	a
	jr	z, sc_end
	ld	(de), a
	inc	de
	dec	bc
	inc	hl
	ld	a, h
	or	l
	jr	nz, sc_loop
	ld	h, #0xC0		; конец окна — следующая страница
	ld	a, (sc_pg)
	inc	a
	ld	(sc_pg), a
	push	bc
	ld	bc, #PAGE3_PORT
	out	(c), a
	pop	bc
	jr	sc_loop
sc_end:
	xor	a
	ld	(de), a
	ld	a, (sc_old)
	ld	bc, #PAGE3_PORT
	out	(c), a
	ret
sc_missing:
	ld	hl, (sc_dst)
	ld	(hl), #'#'
	inc	hl
	xor	a
	push	af
	inc	sp
	ld	de, #0
	push	de
	ld	de, (sc_id)
	push	de
	call	_fmt_num
	ret

;; const char *str_get(uint16_t id /*HL*/) — строка в общий буфер (до следующего вызова)
_str_get::
	ld	de, #1024
	push	de
	ld	de, #strbuf
	call	_str_copy
	ld	de, #strbuf
	ret

;; char *fmt_num(char *out /*HL*/, int32_t v, char sep) — десятичное число, sep —
;; разделитель тысяч (0 — нет). Вычитанием степеней 10.
_fmt_num::
	pop	iy
	pop	de
	pop	bc			; BC:DE = v
	dec	sp
	pop	af			; A = sep
	push	iy
	ld	(fn_sep), a
	ld	(fn_out), hl
	bit	7, b
	jr	z, 1$
	ld	(hl), #'-'
	inc	hl
	xor	a
	sub	e
	ld	e, a
	ld	a, #0
	sbc	a, d
	ld	d, a
	ld	a, #0
	sbc	a, c
	ld	c, a
	ld	a, #0
	sbc	a, b
	ld	b, a
1$:	ld	(fn_p), hl
	ld	(fn_u), de
	ld	(fn_u + 2), bc
	xor	a
	ld	(fn_started), a
	ld	iy, #pow10
	ld	b, #9
fn_loop:
	push	bc
	ld	c, #'0'
fn_sub:
	ld	hl, (fn_u)
	ld	e, 0 (iy)
	ld	d, 1 (iy)
	or	a
	sbc	hl, de
	push	hl
	ld	hl, (fn_u + 2)
	ld	e, 2 (iy)
	ld	d, 3 (iy)
	sbc	hl, de
	jr	c, fn_less
	ld	(fn_u + 2), hl
	pop	hl
	ld	(fn_u), hl
	inc	c
	jr	fn_sub
fn_less:
	pop	hl
	ld	a, c
	cp	#'0'
	jr	nz, fn_emit
	ld	a, (fn_started)
	or	a
	jr	z, fn_skip
fn_emit:
	ld	hl, (fn_p)
	ld	(hl), c
	inc	hl
	ld	a, #1
	ld	(fn_started), a
	pop	bc			; разделитель — когда осталось 9, 6, 3 цифры
	push	bc
	ld	a, (fn_sep)
	or	a
	jr	z, 2$
	ld	a, b
	cp	#9
	jr	z, 3$
	cp	#6
	jr	z, 3$
	cp	#3
	jr	nz, 2$
3$:	ld	a, (fn_sep)
	ld	(hl), a
	inc	hl
2$:	ld	(fn_p), hl
fn_skip:
	ld	de, #4
	add	iy, de
	pop	bc
	djnz	fn_loop
	ld	hl, (fn_p)
	ld	a, (fn_u)
	add	a, #'0'
	ld	(hl), a
	inc	hl
	ld	(hl), #0
	ld	de, (fn_out)
	ret

pow10:
	.dw	0xCA00, 0x3B9A		; 1 000 000 000
	.dw	0xE100, 0x05F5		; 100 000 000
	.dw	0x9680, 0x0098		; 10 000 000
	.dw	0x4240, 0x000F		; 1 000 000
	.dw	0x86A0, 0x0001		; 100 000
	.dw	0x2710, 0x0000		; 10 000
	.dw	0x03E8, 0x0000		; 1 000
	.dw	0x0064, 0x0000		; 100
	.dw	0x000A, 0x0000		; 10

;; void str_fmt(char *out /*HL*/, const char *pat /*DE*/, const char *a0, const char *a1)
;; подстановка {0} (#10 или {N} #03) и {1} (#11); out не больше 255 символов
_str_fmt::
	pop	iy
	pop	bc
	ld	(sf_a0), bc
	pop	bc
	ld	(sf_a1), bc
	push	iy
	push	hl
	ld	bc, #255
	add	hl, bc
	ld	(sf_lim), hl
	pop	hl
sf_loop:
	ld	a, (de)
	or	a
	jr	z, sf_done
	call	chk_lim
	jr	c, sf_done
	ld	bc, (sf_a0)
	cp	#0x10
	jr	z, sf_arg
	cp	#0x03
	jr	z, sf_arg
	ld	bc, (sf_a1)
	cp	#0x11
	jr	z, sf_arg
sf_lit:
	ld	(hl), a
	inc	hl
	inc	de
	jr	sf_loop
sf_arg:
	push	af			; аргумент NULL — сам символ шаблона (как C)
	ld	a, b
	or	c
	jr	nz, 2$
	pop	af
	jr	sf_lit
2$:	pop	af
	ld	a, (bc)
	or	a
	jr	z, 1$
	call	chk_lim
	jr	c, 1$
	ld	(hl), a
	inc	hl
	inc	bc
	jr	sf_arg
1$:	inc	de
	jr	sf_loop
sf_done:
	ld	(hl), #0
	ret

;; CF = 1, если HL >= sf_lim
chk_lim:
	push	de
	push	hl
	ex	de, hl
	ld	hl, (sf_lim)
	scf
	sbc	hl, de
	pop	hl
	pop	de
	ret

;; void str_plural(char *out /*HL*/, uint16_t one /*DE*/, int32_t n) — Language::
;; getString(id, n): форма «one» (id) для 1, иначе «other» (id + 1); {N} — n
_str_plural::
	pop	iy
	pop	bc
	ld	(sp_n), bc
	pop	bc
	ld	(sp_n + 2), bc
	push	iy
	ld	(sp_out), hl
	ld	(sp_one), de
	xor	a
	push	af
	inc	sp
	ld	hl, (sp_n + 2)
	push	hl
	ld	hl, (sp_n)
	push	hl
	ld	hl, #tx_num
	call	_fmt_num
	ld	hl, (sp_one)
	ld	a, (sp_n)
	dec	a
	ld	b, a
	ld	a, (sp_n + 1)
	or	b
	ld	b, a
	ld	a, (sp_n + 2)
	or	b
	ld	b, a
	ld	a, (sp_n + 3)
	or	b
	jr	z, 1$			; n == 1
	inc	hl
1$:	call	_str_get
	ld	hl, #tx_empty
	push	hl
	ld	hl, #tx_num
	push	hl
	ld	hl, (sp_out)
	call	_str_fmt
	ret
