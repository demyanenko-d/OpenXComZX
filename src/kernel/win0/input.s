;; Ввод, резидентная часть (Win0, ассемблер; был input.c): защёлка нажатий кнопок
;; мыши и клавиш в кадровом прерывании, пока их не заберёт input_poll (cursor.c,
;; банк 11) — долгая перерисовка между опросами их не теряет. Портит AF, BC, DE, HL.
;; Матрица ZX: полуряды #FEFE..#7FFE, биты 0..4. CAPS SHIFT — модификатор:
;; CS+буква — заглавная, CS+0 — KEY_DEL, CS+SPACE (BREAK) — KEY_ESC, CS+5..8 — стрелки.
;; SYMBOL SHIFT: SS+K — '+', SS+J — '-'. Стрелки и +/- при удержании повторяются
;; (поворот и зум глобуса), остальные клавиши — одно нажатие.

	.module input
	.globl	input_isr
	.globl	win3_real
	.globl	_cursor_x
	.globl	_cursor_y
	.globl	_cursor_off
	.globl	_cur_pal
	.globl	_mouse_buttons
	.globl	_cursor_sync
	.globl	_cur_lock
	.globl	_in_prev_btn
	.globl	_in_prev_keys
	.globl	_in_btn_latch
	.globl	_in_key_latch
	.globl	_in_key_rep
	.globl	_in_key_reps

;; Курсор мыши — аппаратный спрайт TSU 0, и позиция ему ставится здесь же, в кадровом
;; прерывании: главный цикл занят кадром глобуса или блитами по несколько кадров, и курсор
;; при обновлении из цикла залипал. Список спрайтов — один спрайт, дальше в S-file нули
;; (конец списка), поэтому ни шина, ни TSU от этого не нагружены. Запись в S-file идёт через
;; окно FMAddr и попадает ещё и в ОЗУ под окном, поэтому в Win3 на это время подставляется
;; SCRATCH_PAGE, а потом возвращается страница вызывающего (теневая win3_page — она
;; поддерживается в согласии с портом во всех местах, где Win3 переключают напрямую).
MOUSE_X		= 0xFBDF		; Kempston: абсолютные счётчики, y растёт вверх
MOUSE_Y		= 0xFFDF
FMADDR_PORT	= 0x15AF
FMADDR_C000	= 0x1C			; FM_EN | A[15:12] = #C — окно 4 КБ на #C000
SFILE_W0	= 0xC200		; спрайт 0: три слова
SFILE_W1	= 0xC202
SFILE_W2	= 0xC204
SCRATCH_PAGE	= 0x0F
PAGE3_PORT	= 0x13AF
SCREEN_W	= 320
SCREEN_H	= 200

KEY_ESC		= 27
KEY_ENTER	= 13
KEY_SPACE	= 32
KEY_DEL		= 8
KEY_LEFT	= 0x1C			; + 1 вниз, + 2 вверх, + 3 вправо (input.h)
KEY_WHEEL_UP	= 0x10			; колесо: щелчок от себя / на себя (зум глобуса)
KEY_WHEEL_DOWN	= 0x11
REP_DELAY	= 12			; автоповтор стрелок и +/-: первый через 12 кадров,
REP_RATE	= 2			;   дальше каждые 2 (защёлка — одно нажатие до опроса)

	.area	_DATA
_in_prev_btn::	.ds	1		; кнопки прошлого кадра
_in_prev_keys::	.ds	1
_in_btn_latch::	.ds	1		; нажатия до опроса
_in_key_latch::	.ds	1
_in_key_rep::	.ds	1		; 1 — защёлка от автоповтора (клавишу могли уже отпустить)
_in_key_reps::	.ds	1		; накоплено нажатий до опроса (за долгую перерисовку)
rk_cs:		.ds	1
rk_ss:		.ds	1
in_rep:		.ds	1		; кадров до автоповтора удерживаемой клавиши
_cursor_x::	.dw	160		; курсор экрана (сценарии ставят напрямую: pokew _cursor_x)
_cursor_y::	.dw	100
_cursor_off::	.ds	1		; 1 — курсор скрыт (заставки)
_cur_pal::	.ds	1		; группа палитры спрайтов курсора (ставит cursor_color)
_mouse_buttons::	.ds	1	; кнопки сейчас: бит 0 L, 1 R
_cur_lock::	.ds	1		; 1 — S-file занят основным кодом (input_init): спрайт не трогать
mo_px:		.ds	1		; показания мыши прошлого кадра
mo_py:		.ds	1
wh_prev:	.ds	1		; счётчик колеса прошлого кадра (биты 7:4 порта кнопок)
cur_pg:		.ds	1		; страница Win3 вызывающего на время записи S-file

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

;; void cursor_sync(void) — запомнить показания мыши, не двигая курсор (input_init)
_cursor_sync::
	ld	bc, #MOUSE_X
	in	a, (c)
	ld	(mo_px), a
	ld	bc, #MOUSE_Y
	in	a, (c)
	ld	(mo_py), a
	ld	bc, #0xFADF			; счётчик колеса — тоже, иначе первый кадр даст ложный щелчок
	in	a, (c)
	and	#0xF0
	ld	(wh_prev), a
	ret

;; cursor_x += A (знаковое), клип 0..SCREEN_W-1
cur_addx:
	ld	hl, #_cursor_x
	ld	de, #SCREEN_W - 1
	jr	cur_add

;; cursor_y += A, клип 0..SCREEN_H-1
cur_addy:
	ld	hl, #_cursor_y
	ld	de, #SCREEN_H - 1
;; HL — адрес координаты, DE — предел, A — сдвиг со знаком
cur_add:
	or	a, a
	ret	z
	ld	c, a				; BC — сдвиг с расширением знака
	ld	b, #0
	bit	7, a
	jr	z, 1$
	ld	b, #0xFF
1$:	push	hl
	ld	a, (hl)
	inc	hl
	ld	h, (hl)
	ld	l, a
	add	hl, bc
	bit	7, h				; ушёл влево/вверх
	jr	z, 2$
	ld	hl, #0
	jr	4$
2$:	ld	a, h				; HL > DE — прижать к пределу
	cp	a, d
	jr	c, 4$
	jr	nz, 3$
	ld	a, l
	cp	a, e
	jr	c, 4$
3$:	ld	h, d
	ld	l, e
4$:	pop	de
	ex	de, hl
	ld	(hl), e
	inc	hl
	ld	(hl), d
	ret

;; Спрайт 0 в S-file: Y | видимость, X, палитра 15 (дальше в списке нули — конец).
;; Страницу Win3 подменяем через фактическую теневую (win3_real), а не pg_map3: логическую
;; страницу (pg_win3) вызывающие используют для сохранения и восстановления — её трогать нельзя
cur_spr:
	ld	a, (win3_real)
	ld	(cur_pg), a
	ld	a, #SCRATCH_PAGE
	ld	(win3_real), a
	ld	bc, #PAGE3_PORT
	out	(c), a
	ld	bc, #FMADDR_PORT
	ld	a, #FMADDR_C000
	out	(c), a
	ld	hl, (_cursor_y)
	ld	a, h
	and	#1				; y & #1FF, бит 9 — высота 16
	or	a, #0x02
	ld	h, a
	set	6, h				; бит 14 — последний спрайт слоя (список кончился)
	ld	a, (_cursor_off)
	or	a, a
	jr	nz, 1$
	set	5, h				; бит 13 — спрайт виден
1$:	ld	(SFILE_W0), hl
	ld	hl, (_cursor_x)
	ld	a, h
	and	#1
	or	a, #0x02			; бит 9
	ld	h, a
	ld	(SFILE_W1), hl
	ld	a, (_cur_pal)			; группа палитры (cursor.c): CRAM cur_pal · 16 + пиксель
	rlca
	rlca
	rlca
	rlca
	and	a, #0xF0
	ld	h, a
	ld	l, #0				; тайл 0 — стрелка
	ld	(SFILE_W2), hl
	ld	bc, #FMADDR_PORT
	xor	a, a
	out	(c), a
	ld	a, (cur_pg)
	ld	(win3_real), a
	ld	bc, #PAGE3_PORT
	out	(c), a
	ret

;; Курсор: сдвиг по показаниям мыши и спрайт на новое место
cur_isr:
	ld	a, (_cur_lock)
	or	a, a
	ret	nz
	ld	bc, #MOUSE_X
	in	a, (c)
	ld	e, a
	ld	hl, #mo_px
	sub	a, (hl)
	ld	(hl), e
	call	cur_addx
	ld	bc, #MOUSE_Y
	in	a, (c)
	ld	e, a
	ld	hl, #mo_py
	ld	a, (hl)
	sub	a, e				; y растёт вверх — сдвиг вниз
	ld	(hl), e
	call	cur_addy
	jp	cur_spr

;; A — сырой байт порта кнопок: щелчки колеса (счётчик 7:4, 4 бита со сносом) в защёлку клавиш,
;; числом шагов — in_key_reps (быстрая прокрутка за один кадр даёт несколько шагов зума)
wheel:
	and	#0xF0
	ld	e, a				; E — счётчик сейчас
	ld	a, (wh_prev)
	ld	d, a				; D — он же в прошлом кадре
	ld	a, e
	ld	(wh_prev), a
	sub	a, d				; разница в старших битах
	ret	z
	rlca					; -> младшие 4 бита (со сносом)
	rlca
	rlca
	rlca
	and	#0x0F
	ld	d, a				; D — щелчков (1..15 со сносом счётчика)
	cp	a, #8				; 1..7 — от себя (приблизить), 8..15 — на себя
	jr	nc, 2$
	ld	a, #KEY_WHEEL_UP
	jr	1$
2$:	ld	a, #16				; на себя: щелчков 16 − разница
	sub	a, d
	ld	d, a
	ld	a, #KEY_WHEEL_DOWN
1$:	ld	(_in_key_latch), a
	xor	a, a
	ld	(_in_key_rep), a		; настоящее нажатие (не автоповтор)
	ld	a, d
	ld	(_in_key_reps), a
	ret

;; Из кадрового прерывания (crt0.s)
input_isr:
	call	cur_isr
	ld	bc, #0xFADF		; кнопки Kempston, активны нулём; биты 7:4 — счётчик колеса
	in	a, (c)
	ld	d, a
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
	ld	a, d
	call	wheel
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
	inc	a
	ld	(_in_key_reps), a
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
	ld	a, (_in_key_reps)	; копим пропущенные: за долгую перерисовку защёлка взводится
	cp	a, #8			; до 14 раз, а опрос забирал одно — отсюда медленный поворот
	ret	nc
	inc	a
	ld	(_in_key_reps), a
	ret
