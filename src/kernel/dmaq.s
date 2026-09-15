;; НЕ СОБИРАЕТСЯ (2026-09-15): очередь не использовалась, кольцо 4 КБ занимало Win1 и
;; лежало не с границы 256. Вернуть — с кольцом в странице пула (14_todo.md §1.3).
;;
;; Очередь команд DMA («display list»), продвигаемая прерыванием DMA.
;;
;; Производитель (основной код) кладёт 9-байтные команды в кольцо и уходит
;; считать своё. По окончании передачи DMA выставляет INT (вектор #FB),
;; isr_dma запускает следующую команду девятью OUTI. Пока очередь не пуста,
;; DMA работает без участия основного кода.
;;
;; Правило: при работающей очереди нельзя трогать регистры DMA напрямую —
;; все передачи только через dmaq_push.
;;
;; Сейчас очередь не используется: gfx.c, far.c и sd.s пишут регистры DMA
;; сами с опросом DMAStatus, прерывание DMA не включено (dmaq_wait зависнет).
;;
;; Формат команды (порядок под OUTI, B уменьшается перед выводом):
;;   +0 DAX  -> #1FAF      +3 SAX -> #1CAF      +6 LEN  -> #26AF
;;   +1 DAH  -> #1EAF      +4 SAH -> #1BAF      +7 NUM  -> #28AF
;;   +2 DAL  -> #1DAF      +5 SAL -> #1AAF      +8 CTRL -> #27AF (запуск)
;; Кольцо: 256 записей по 16 байт (4 КБ) в странице данных (область _KDATA,
;; Win1 — всегда подключена, доступна из ISR). Код — в общем коде (Win0).

	.module dmaq
	.globl	isr_dma
	.globl	_dmaq_push
	.globl	_dmaq_busy
	.globl	_dmaq_wait

DMAQ_REC = 16

	.area	_KDATA
dmaq_ring:	.ds	256 * DMAQ_REC	; должно начинаться с адреса, кратного 256
dmaq_head:	.ds	1		; сюда пишет производитель
dmaq_tail:	.ds	1		; следующая к запуску
dmaq_run:	.ds	1		; 1 — DMA работает по команде из очереди

	.area	_CODE

;; HL = адрес записи с индексом A
rec_addr:
	ld	l, a
	ld	h, #0
	add	hl, hl
	add	hl, hl
	add	hl, hl
	add	hl, hl
	ld	bc, #dmaq_ring
	add	hl, bc
	ret

;; Запустить команду tail, если очередь не пуста; иначе run = 0.
;; Портит AF, BC, HL. Вызывать при запрещённых прерываниях.
dmaq_start_next:
	ld	a, (dmaq_tail)
	ld	hl, #dmaq_head
	cp	(hl)
	jr	z, 1$
	call	rec_addr
	inc	a
	ld	(dmaq_tail), a
	ld	bc, #0x20AF
	outi			; DAX -> #1FAF
	outi			; DAH -> #1EAF
	outi			; DAL -> #1DAF
	outi			; SAX -> #1CAF
	outi			; SAH -> #1BAF
	outi			; SAL -> #1AAF
	ld	b, #0x27
	outi			; LEN -> #26AF
	ld	b, #0x29
	outi			; NUM -> #28AF
	ld	b, #0x28
	outi			; CTRL -> #27AF, пуск
	ld	a, #1
	ld	(dmaq_run), a
	ret
1$:
	xor	a
	ld	(dmaq_run), a
	ret

;; Прерывание DMA: передача закончилась — запустить следующую.
isr_dma:
	push	af
	push	bc
	push	hl
	call	dmaq_start_next
	pop	hl
	pop	bc
	pop	af
	ei
	reti

;; void dmaq_push(const dma_cmd_t *cmd)   — HL = cmd (sdcccall 1)
;; Ждёт, если кольцо полно. Вызывать при разрешённых прерываниях.
_dmaq_push::
	ex	de, hl			; DE = команда
1$:
	ld	a, (dmaq_tail)
	ld	b, a
	ld	a, (dmaq_head)
	inc	a
	cp	b
	jr	z, 1$			; полно — ждём, пока ISR освободит место
	dec	a
	push	de
	call	rec_addr		; HL = запись head
	pop	de
	ex	de, hl			; HL = команда, DE = запись
	ld	bc, #9
	ldir
	di
	ld	a, (dmaq_head)
	inc	a
	ld	(dmaq_head), a
	ld	a, (dmaq_run)
	or	a
	call	z, dmaq_start_next	; DMA простаивает — пнуть
	ei
	ret

;; uint8_t dmaq_busy(void) — A != 0, пока очередь не пуста или DMA работает
_dmaq_busy::
	ld	a, (dmaq_run)
	ret

;; void dmaq_wait(void)
_dmaq_wait::
	ld	a, (dmaq_run)
	or	a
	jr	nz, _dmaq_wait
	ret
