;; Ресурсы из пакетов *.PAK (Win0, ассемблер; был res.c). Формат пакета —
;; project_docs/09_converter.md §5: заголовок 16 байт (+6 — число записей), записи
;; по 16 байт с +16: id(2) тип(1) флаги(1) сектор(2) размер(4) a(2) b(2) c(2).
;; Вшитые пакеты — с начала своих страниц (tmp/build/packs.s: _pack_page[], _pack_count),
;; каталог — в первой странице пакета. Нет во вшитых — sdres_find (SD, банк 12).
;; Таблицы правил: rtab_t {far base; u16 n, size, tail}, запись i — base + 8 + i*size
;; (таблица <= 16 КБ — произведение i*size 16-битное).
;; ABI SDCC --sdcccall 1 (см. pages.s, far.s); __banked-вызов: аргументы в стеке,
;; снимает вызывающий. far_read/far_write/far_fill/far_copy держат адрес возврата
;; в IY — через их вызов IY не сохраняется (хвостовой вызов jp вместо call).

	.module res
	.globl	_res_find
	.globl	_res_game
	.globl	_rtab_open
	.globl	_rtab_get
	.globl	_rtab_word
	.globl	_rtab_tail
	.globl	_far_read
	.globl	_far_word
	.globl	_far_byte
	.globl	_pack_page
	.globl	_pack_count
	.globl	_sdres_find
	.globl	b_sdres_find
	.globl	___sdcc_bcall_ehl

PAGE3_PORT	= 0x13AF
RCACHE		= 8
RES_SIZE	= 15			; sizeof(res_t): phys 4, size 4, a b c 6, type 1

	.area	_DATA
rc_id:		.ds	2 * RCACHE	; кэш последних найденных (фон окна и шрифты — на каждой перерисовке)
rc_res:		.ds	RES_SIZE * RCACHE
rc_next:	.ds	1
rf_id:		.ds	2
rf_dst:		.ds	2
rf_tmp:		.ds	RES_SIZE
rt_t:		.ds	2		; rtab_t вызывающего
rt_word:	.ds	2

	.area	_CODE

;; ---------------------------------------------------------------- поиск

;; uint8_t res_find(uint16_t id /*HL*/, res_t *r /*DE*/) — 1: найден
_res_find::
	ld	a, h
	or	l
	ret	z			; id 0 — нет (как sdres_find)
	ld	(rf_id), hl
	ld	(rf_dst), de
	ld	de, #rc_id		; кэш
	ld	c, #0
1$:	ld	a, (de)
	inc	de
	cp	l
	jr	nz, 2$
	ld	a, (de)
	cp	h
	jr	z, rf_hit
2$:	inc	de
	inc	c
	ld	a, c
	cp	#RCACHE
	jr	nz, 1$
	call	res_scan		; вшитые пакеты -> rf_tmp
	or	a
	jr	z, rf_sd
	ld	a, (rc_next)		; в кэш: слот rc_next
	ld	c, a
	inc	a
	and	#RCACHE - 1
	ld	(rc_next), a
	ld	l, c
	ld	h, #0
	add	hl, hl
	ld	de, #rc_id
	add	hl, de
	ld	de, (rf_id)
	ld	(hl), e
	inc	hl
	ld	(hl), d
	call	slot_addr		; HL = rc_res[c]
	ex	de, hl
	ld	hl, #rf_tmp
	ld	bc, #RES_SIZE
	ldir
	ld	hl, #rf_tmp
	jr	rf_copy
rf_hit:
	call	slot_addr
rf_copy:				; HL = res_t -> *r
	ld	de, (rf_dst)
	ld	bc, #RES_SIZE
	ldir
	ld	a, #1
	ret
rf_sd:					; с SD — без кэша (слот SD переиспользуется)
	ld	hl, (rf_dst)
	push	hl
	ld	hl, (rf_id)
	push	hl
	ld	e, #b_sdres_find
	ld	hl, #_sdres_find
	call	___sdcc_bcall_ehl
	pop	bc
	pop	bc
	ret

;; HL = &rc_res[C]
slot_addr:
	ld	a, c
	add	a, a
	add	a, a
	add	a, a
	add	a, a
	sub	c			; C * 15
	ld	l, a
	ld	h, #0
	ld	de, #rc_res
	add	hl, de
	ret

;; Вшитые пакеты: запись с id = rf_id -> rf_tmp. A = 1 — найдена.
res_scan:
	ld	a, (_pack_count)
	or	a
	ret	z
	ld	b, a
	ld	hl, #_pack_page
rs_pack:
	push	bc
	push	hl
	ld	c, (hl)			; C = страница пакета
	ld	a, c
	call	map3			; каталог — в Win3
	ld	hl, (0xC006)		; число записей
	ld	b, h
	ld	a, l			; BA = n
	ld	hl, #0xC010
	ld	de, (rf_id)
rs_ent:
	ld	(rs_cnt), a
	ld	a, b
	ld	(rs_cnt + 1), a
	ld	a, (rs_cnt)
	or	b
	jr	z, rs_next
	ld	a, (hl)
	cp	e
	jr	nz, rs_skip
	inc	hl
	ld	a, (hl)
	dec	hl
	cp	d
	jr	z, rs_found
rs_skip:
	ld	a, #16
	add	a, l
	ld	l, a
	ld	a, h
	adc	a, #0
	ld	h, a
	ld	a, (rs_cnt)		; n--
	sub	#1
	ld	(rs_cnt), a
	ld	a, (rs_cnt + 1)
	sbc	a, #0
	ld	b, a
	ld	a, (rs_cnt)
	jr	rs_ent
rs_next:
	pop	hl
	pop	bc
	inc	hl
	djnz	rs_pack
	call	unmap3
	xor	a
	ret
rs_found:				; HL = запись, C = страница пакета
	inc	hl
	inc	hl
	ld	a, (hl)
	ld	(rf_tmp + 14), a	; тип
	inc	hl
	inc	hl
	ld	e, (hl)			; сектор
	inc	hl
	ld	d, (hl)
	inc	hl
	push	hl
	;; phys = (page << 14) + (sector << 9)
	ld	a, e
	add	a, a
	ld	(rf_tmp + 1), a
	ld	a, d
	rla
	ld	(rf_tmp + 2), a
	ld	a, #0
	rla
	ld	(rf_tmp + 3), a
	xor	a
	ld	(rf_tmp), a
	ld	a, c
	srl	a
	srl	a
	ld	b, a			; page >> 2
	ld	a, c
	and	#3
	rrca
	rrca
	ld	hl, #rf_tmp + 1
	add	a, (hl)
	ld	(hl), a
	inc	hl
	ld	a, b
	adc	a, (hl)
	ld	(hl), a
	inc	hl
	ld	a, #0
	adc	a, (hl)
	ld	(hl), a
	pop	hl			; размер, a, b, c — 10 байт подряд
	ld	de, #rf_tmp + 4
	ld	bc, #10
	ldir
	pop	hl
	pop	bc
	call	unmap3
	ld	a, #1
	ret

	.area	_DATA
rs_cnt:	.ds	2
rs_old:	.ds	1
	.area	_CODE

;; Win3 = A (прежняя — в rs_old), вернуть прежнюю. Теневой регистр не меняется.
	.globl	_pg_win3
	.globl	win3_real
map3:
	push	bc
	push	af
	call	_pg_win3
	ld	(rs_old), a
	pop	af
	ld	(win3_real), a			; фактическая страница Win3 — для прерывания курсора
	ld	bc, #PAGE3_PORT
	out	(c), a
	pop	bc
	ret
unmap3:
	ld	a, (rs_old)
	ld	(win3_real), a
	ld	bc, #PAGE3_PORT
	out	(c), a
	ret

;; uint8_t res_game(void) — байт +5 заголовка первого пакета: 1 UFO, 2 TFTD
_res_game::
	ld	a, (_pack_page)
	call	map3
	ld	a, (0xC005)
	push	af
	call	unmap3
	pop	af
	ret

;; ---------------------------------------------------------------- таблицы правил

;; uint8_t rtab_open(uint16_t id /*HL*/, rtab_t *t /*DE*/)
_rtab_open::
	ld	(rt_t), de
	ld	de, #rt_res
	call	_res_find
	or	a
	jr	nz, 1$
	ld	hl, (rt_t)		; t->n = 0
	inc	hl
	inc	hl
	inc	hl
	inc	hl
	ld	(hl), a
	inc	hl
	ld	(hl), a
	ret
1$:	ld	hl, (rt_t)		; t->base = r.phys
	ex	de, hl
	ld	hl, #rt_res
	ld	bc, #4
	ldir				; DE = &t->n
	ld	hl, #6			; n, size, tail — первые 6 байт таблицы
	push	hl
	push	de
	ld	de, (rt_res)
	ld	hl, (rt_res + 2)
	call	_far_read
	ld	a, #1
	ret

	.area	_DATA
rt_res:	.ds	RES_SIZE
rt_addr:	.ds	4
	.area	_CODE

;; rt_addr = t->base + 8 + i * t->size (+ A); HL = t, DE = i
rec_addr:
	push	af
	push	hl
	ld	bc, #6
	add	hl, bc
	ld	c, (hl)
	inc	hl
	ld	b, (hl)			; BC = size
	call	mul16			; HL = i * size (DE = i)
	pop	de			; DE = t
	pop	af
	ld	c, a
	ld	b, #0
	add	hl, bc
	ld	bc, #8
	add	hl, bc			; HL = 8 + i*size + A
	ex	de, hl			; DE = смещение, HL = t
	ld	a, (hl)			; rt_addr = base + смещение
	add	a, e
	ld	(rt_addr), a
	inc	hl
	ld	a, (hl)
	adc	a, d
	ld	(rt_addr + 1), a
	inc	hl
	ld	a, (hl)
	adc	a, #0
	ld	(rt_addr + 2), a
	inc	hl
	ld	a, (hl)
	adc	a, #0
	ld	(rt_addr + 3), a
	ret

;; HL = DE * BC (младшие 16 бит)
mul16:
	ld	hl, #0
	ld	a, #16
1$:	add	hl, hl
	rl	e
	rl	d
	jr	nc, 2$
	add	hl, bc
2$:	dec	a
	jr	nz, 1$
	ret

;; void rtab_get(const rtab_t *t /*HL*/, uint16_t i /*DE*/, void *dst /*стек*/)
_rtab_get::
	push	hl
	xor	a
	call	rec_addr
	pop	hl
	ld	bc, #6			; size
	add	hl, bc
	ld	c, (hl)
	inc	hl
	ld	b, (hl)
	pop	iy			; адрес возврата
	pop	hl			; dst
	push	bc			; len = size
	push	hl			; dst
	push	iy			; far_read вернётся сразу к вызывающему (IY она портит)
	ld	de, (rt_addr)
	ld	hl, (rt_addr + 2)
	jp	_far_read

;; uint16_t rtab_word(const rtab_t *t /*HL*/, uint16_t i /*DE*/, uint8_t off /*стек*/)
_rtab_word::
	pop	iy
	dec	sp
	pop	af			; A = off
	push	iy
	call	rec_addr
	ld	de, (rt_addr)
	ld	hl, (rt_addr + 2)
	jp	_far_word		; DE = слово

;; void rtab_tail(const rtab_t *t /*HL*/, uint16_t off /*DE*/, void *dst, uint16_t bytes)
_rtab_tail::
	push	hl			; t
	ld	bc, #8			; tail
	add	hl, bc
	ld	c, (hl)
	inc	hl
	ld	b, (hl)
	ex	de, hl			; HL = off
	add	hl, bc
	ex	de, hl			; DE = tail + off
	pop	hl			; HL = &t->base
	ld	a, (hl)
	add	a, e
	ld	e, a
	inc	hl
	ld	a, (hl)
	adc	a, d
	ld	d, a
	inc	hl
	ld	a, (hl)
	adc	a, #0
	ld	c, a
	inc	hl
	ld	a, (hl)
	adc	a, #0
	ld	h, a
	ld	l, c			; DE:HL = base + tail + off; dst, bytes — в стеке как для far_read
	jp	_far_read
