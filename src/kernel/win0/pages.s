;; Менеджер страниц (Win0, ассемблер; был pages.c): битовая карта динамического
;; пула #B0-#EF, окно Win3 с теневым регистром, адрес точки экрана.
;; ABI SDCC --sdcccall 1: 1-й байт — A, 2-й байт — L; 16 бит — HL, затем DE;
;; результат: байт — A, слово — DE. Сохранять только IX.

	.module pages
	.globl	_pg_alloc
	.globl	_pg_free
	.globl	_pg_free_count
	.globl	_pg_map3
	.globl	win3_real
	.globl	_pg_win3
	.globl	_gfx_map

POOL_FIRST	= 0xB0
POOL_LAST	= 0xEF
POOL_N		= 64
SCREEN_PAGE	= 0x10
PAGE3_PORT	= 0x13AF

	.area	_DATA
_pg_pool_map::
pool_map:	.ds	8		; 1 — занята (бит i — страница POOL_FIRST + i); _pg_pool_map — метка для сценариев (peek)
win3_page:	.ds	1		; страница Win3 для pg_win3: логическая, её восстанавливают вызывающие
win3_real::	.ds	1		; фактическая (порт): её подменяет и возвращает прерывание курсора,
				;   поэтому её обновляют и места, где Win3 переключают напрямую
pa_n:		.ds	1
pa_align:	.ds	1

	.area	_CODE

;; HL = адрес байта карты, A = маска бита для индекса пула в A (0..63)
bit_addr:
	ld	c, a
	and	#7
	ld	b, a
	ld	a, #1
	inc	b
	jr	2$
1$:	add	a, a
2$:	djnz	1$
	ld	b, a			; маска
	ld	a, c
	rrca
	rrca
	rrca
	and	#7
	ld	hl, #pool_map
	add	a, l
	ld	l, a
	ld	a, h
	adc	a, #0
	ld	h, a
	ld	a, b
	ret

;; uint8_t pg_alloc(uint8_t n /*A*/, uint8_t align /*L*/)
;; n подряд страниц пула, первая кратна align; PG_NONE (#FF) — нет места.
_pg_alloc::
	or	a
	jr	z, pa_none
	cp	#POOL_N + 1
	jr	nc, pa_none
	ld	(pa_n), a
	ld	a, l
	or	a
	jr	nz, 1$
	inc	a
1$:	ld	(pa_align), a
	;; page = (POOL_FIRST + align - 1) & ~(align - 1)
	ld	b, a
	dec	b			; B = align - 1
	ld	a, #POOL_FIRST
	add	a, b
	ld	c, a
	ld	a, b
	cpl
	and	c
	ld	d, a			; D = page
pa_try:
	;; page + n - 1 <= POOL_LAST
	ld	a, (pa_n)
	dec	a
	add	a, d
	jr	c, pa_none
	cp	#POOL_LAST + 1
	jr	nc, pa_none
	;; все n свободны?
	ld	a, (pa_n)
	ld	e, a			; E = осталось проверить
	ld	a, d
	sub	#POOL_FIRST		; индекс в пуле
pa_chk:
	push	af
	push	de
	call	bit_addr
	and	(hl)
	pop	de
	jr	nz, pa_busy
	pop	af
	inc	a
	dec	e
	jr	nz, pa_chk
	;; занять
	ld	a, (pa_n)
	ld	e, a
	ld	a, d
	sub	#POOL_FIRST
pa_set:
	push	af
	push	de
	call	bit_addr
	or	(hl)
	ld	(hl), a
	pop	de
	pop	af
	inc	a
	dec	e
	jr	nz, pa_set
	ld	a, d
	ret
pa_busy:
	pop	af
	ld	a, (pa_align)
	add	a, d
	jr	c, pa_none
	ld	d, a
	jr	pa_try
pa_none:
	ld	a, #0xFF
	ret

;; void pg_free(uint8_t first /*A*/, uint8_t n /*L*/)
_pg_free::
	ld	e, l
	inc	e
	ld	d, a
pf_loop:
	dec	e
	ret	z
	ld	a, d
	inc	d
	cp	#POOL_FIRST
	jr	c, pf_loop
	cp	#POOL_LAST + 1
	jr	nc, pf_loop
	sub	#POOL_FIRST
	push	de
	call	bit_addr
	cpl
	and	(hl)
	ld	(hl), a
	pop	de
	jr	pf_loop

;; uint8_t pg_free_count(void)
_pg_free_count::
	ld	hl, #pool_map
	ld	d, #8
	ld	e, #0			; занято
1$:	ld	a, (hl)
	inc	hl
	ld	b, #8
2$:	rrca
	jr	nc, 3$
	inc	e
3$:	djnz	2$
	dec	d
	jr	nz, 1$
	ld	a, #POOL_N
	sub	e
	ret

;; uint8_t pg_map3(uint8_t page /*A*/) — подключить в Win3, вернуть прежнюю
_pg_map3::
	ld	hl, #win3_page
	ld	e, (hl)
	ld	(hl), a
	ld	(win3_real), a			; раньше порта: прерывание вернёт уже новую страницу
	ld	bc, #PAGE3_PORT
	out	(c), a
	ld	a, e
	ret

;; uint8_t pg_win3(void)
_pg_win3::
	ld	a, (win3_page)
	ret

;; uint8_t *gfx_map(int16_t x /*HL*/, int16_t y /*DE*/) — страница строки y в Win3
;; (НЕ восстанавливается), адрес #C000 + (y & 31) * 512 + x (строка 512 байт).
_gfx_map::
	ld	a, e
	and	#0x1F
	add	a, a
	add	a, #0xC0
	ld	b, a
	ld	c, #0
	add	hl, bc
	push	hl
	ld	a, e			; страница = SCREEN_PAGE + (y >> 5)
	rlca
	rlca
	rlca
	and	#7
	ld	c, a
	ld	a, d
	add	a, a
	add	a, a
	add	a, a
	or	c
	add	a, #SCREEN_PAGE
	call	_pg_map3
	pop	de
	ret
