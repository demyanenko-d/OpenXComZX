;; 32-битное деление на ассемблере вместо модулей библиотеки SDCC 4.5 _divulong, _modulong,
;; _divslong, _modslong (C-код: 32 шага по 32 бита, ~10 800 тактов на деление, perf/logic.md П7).
;; Все символы модулей определены тут — библиотечные компоновщик не подключает.
;;
;; sdcccall(1): x — HLDE (HL — старшее слово), y — в стеке (младшее слово ближе), стек чистит
;; вызывающий; результат — HLDE. IX сохраняется. Теневые регистры портятся: прерывание их не
;; трогает (crt0.s isr_frame), SDCC — тоже.
;;
;; Ядро udiv делит столбиком по байтам и сам выбирает путь по величине делителя:
;;   y < 256    — остаток в A, ~46 тактов на бит;
;;   y < 32768  — остаток в HL, развёрнуто по 8 шагов, ~53 такта на бит;
;;   y < 65536  — то же с проверкой 17-го бита, ~73 такта на бит;
;;   y >= 65536 — частное < 65536: 16 шагов по 32 бита (~145 тактов), y >= 2^24 — 8.
;; Старшие нули делимого не считаются: старшее слово 0 — пропуск, байт меньше делителя при
;; нулевом остатке — сразу в остаток. Типичное x / 100 при x < 65536 — ~8 шагов вместо 32.
;; Деление на 0: частное — все единицы, остаток — x (как у библиотеки), без зависания.

	.module div32
	.optsdcc -mz80 sdcccall(1)

	.globl	__divulong
	.globl	__modulong
	.globl	__divslong
	.globl	__modslong
	.globl	neg32
	.globl	sl_go			; внутренние метки — глобальные ради профилировщика (07 §6)
	.globl	udiv
	.globl	ud16
	.globl	ud32
	.globl	wpass8
	.globl	bpass16
	.globl	bp16_big

	.area	_CODE

;; uint32_t _modulong(uint32_t x, uint32_t y)
__modulong:
	exx
	pop	bc			; адрес возврата
	pop	de			; y -> HL':DE'
	pop	hl
	push	hl
	push	de
	push	bc
	exx
	call	udiv
	exx				; остаток
	ret

;; int32_t _modslong(int32_t x, int32_t y): знак остатка — знак x (усечение к нулю, как в C)
__modslong:
	ld	a, #1			; бит 0 — нужен остаток
	jr	sl_go

;; int32_t _divslong(int32_t x, int32_t y)
__divslong:
	xor	a
sl_go:
	exx
	pop	bc
	pop	de
	pop	hl
	push	hl
	push	de
	push	bc
	bit	7, h
	jr	z, 1$
	call	neg32			; y = |y|
	bit	0, a
	jr	nz, 1$			; у остатка знак y не важен
	xor	#2			; бит 1 — сменить знак результата
1$:	exx
	bit	7, h
	jr	z, 2$
	call	neg32			; x = |x|
	xor	#2
2$:	push	af
	call	udiv
	pop	af
	rra
	jr	nc, 3$
	exx				; остаток
3$:	rra
	ret	nc
	;; дальше — neg32

;; HL:DE = -HL:DE. A сохраняется, BC портится
neg32:
	ld	b, h
	ld	c, l
	ld	hl, #0
	or	a
	sbc	hl, de
	ex	de, hl
	ld	hl, #0
	sbc	hl, bc
	ret

;; uint32_t _divulong(uint32_t x, uint32_t y)
__divulong:
	exx
	pop	bc
	pop	de
	pop	hl
	push	hl
	push	de
	push	bc
	exx
	;; дальше — udiv

;; HL:DE = x, HL':DE' = y -> HL:DE = x / y, HL':DE' = x % y
udiv:
	exx
	ld	a, h
	or	l
	jr	nz, ud32
	or	d
	jr	nz, ud16
	ld	a, e
	exx
	;; ---- y < 256
	ld	c, a
	ld	a, h
	or	l
	jr	z, 1$			; старшее слово 0: частное 0, остаток 0 (A = 0)
	xor	a
	call	wpass8
1$:	ex	de, hl
	call	wpass8
	ex	de, hl
	exx
	ld	hl, #0
	ld	d, h
	ld	e, a
	exx
	ret

;; ---- 256 <= y < 65536 (на входе — теневой набор, DE = y): частное < 2^24
ud16:
	push	de
	exx
	ex	(sp), hl		; HL = y, [sp] = x_hi
	ex	de, hl			; DE = y, HL = x_lo
	ex	(sp), hl		; HL = x_hi, [sp] = x_lo
	ld	c, #0			; байт 2 частного
	and	a
	sbc	hl, de
	add	hl, de			; CF: x_hi < y
	jr	c, 1$
	ld	a, l			; x_hi / y < 256: остаток сразу = старший байт x_hi
	ld	l, h
	ld	h, c
	call	bpass16
	ld	c, a
1$:	ex	(sp), hl		; HL = x_lo, [sp] = остаток
	ld	a, h			; байт 1 делимого
	ld	h, l			; байт 0 — в стек
	ex	(sp), hl
	ld	b, a
	inc	h
	dec	h
	jr	nz, 2$			; остаток >= 256
	ld	h, l			; остаток:байт 1 меньше y — байт 1 частного 0
	ld	l, a
	and	a
	sbc	hl, de
	add	hl, de
	ld	a, #0
	jr	c, 3$
	ld	l, h			; нет — вернуть остаток
	ld	h, a
	ld	a, b
2$:	call	bpass16
3$:	ex	(sp), hl		; [sp] = остаток, H = байт 0 делимого
	ld	l, a			; L = байт 1 частного
	ld	a, h
	ex	(sp), hl
	call	bpass16
	ex	(sp), hl
	ld	e, a
	ld	d, l
	exx
	pop	de			; остаток
	ld	hl, #0
	exx
	ld	l, c
	ld	h, #0
	ret

;; ---- y >= 65536 (теневой набор: HL = y_hi, DE = y_lo): частное < 65536, 16 шагов.
;; Остаток: HL (младшее слово) и HL' (старшее), y: DE и DE', делимое/частное — A:C
ud32:
	push	de
	ex	de, hl			; DE' = y_hi
	ld	hl, #0			; HL' = 0
	ld	a, d
	or	a			; y >= 2^24: частное < 256
	exx
	jr	nz, 6$
	ld	a, d
	ld	c, e			; A:C = x_lo, остаток = x_hi < y
	pop	de			; DE = y_lo
	ld	b, #16
1$:	sla	c
	rla				; бит делимого -> CF, бит частного 0
	adc	hl, hl
	exx
	adc	hl, hl
	jr	c, 4$			; 33-й бит: остаток заведомо >= y
	exx
	sbc	hl, de
	exx
	sbc	hl, de
	jr	nc, 3$
	exx				; меньше — вернуть
	add	hl, de
	exx
	adc	hl, de
	exx
	djnz	1$
	jr	5$
3$:	exx
	inc	c
	djnz	1$
	jr	5$
4$:	exx
	and	a
	sbc	hl, de
	exx
	sbc	hl, de
	exx
	inc	c
	djnz	1$
5$:	push	hl
	ld	d, a
	ld	e, c
	exx
	pop	de			; остаток: HL':DE'
	exx
	ld	hl, #0
	ret
6$:	ld	a, h			; остаток = x >> 8 (< 2^24 <= y), 8 шагов по младшему байту
	exx
	ld	l, a			; HL' = старший байт x
	exx
	ld	h, l
	ld	l, d			; HL = байты 2:1 x
	ld	a, e			; A:C = байт 0 : 0 — после 8 сдвигов A = 0, C = частное
	ld	c, #0
	pop	de
	ld	b, #8
	jr	1$

;; Слово делимого на байт: A = остаток (< C), HL = слово -> HL = частное, A = остаток. B, F
wpass8:
	ld	b, #8			; по два шага на djnz
	or	a
	jr	nz, 2$
	ld	a, h
	cp	c
	jr	nc, 1$
	ld	h, l			; остаток 0 и старший байт < C: он и есть остаток, 8 шагов
	ld	l, #0
	ld	b, #4
	jr	2$
1$:	xor	a
2$:	add	hl, hl
	rla
	jr	c, 3$			; 9-й бит (C >= 128)
	cp	c
	jr	c, 4$
3$:	sub	c
	inc	l
4$:	add	hl, hl
	rla
	jr	c, 5$
	cp	c
	jr	c, 6$
5$:	sub	c
	inc	l
6$:	djnz	2$
	ret

;; Байт делимого на слово: HL = остаток (< DE), A = байт -> A = байт частного, HL = остаток.
;; Бит частного — CF после ccf (библиотечный приём divunsigned.s). B, F; C не трогается.
;; Делитель < 32768: 2 * остаток + 1 < 65536, 17-го бита не бывает — 8 шагов подряд без
;; проверки и djnz (~53 такта на бит вместо ~73)
bpass16:
	bit	7, d
	jr	nz, bp16_big
	rla
	adc	hl, hl
	sbc	hl, de
	jr	nc, 1$
	add	hl, de
1$:	ccf
	rla
	adc	hl, hl
	sbc	hl, de
	jr	nc, 2$
	add	hl, de
2$:	ccf
	rla
	adc	hl, hl
	sbc	hl, de
	jr	nc, 3$
	add	hl, de
3$:	ccf
	rla
	adc	hl, hl
	sbc	hl, de
	jr	nc, 4$
	add	hl, de
4$:	ccf
	rla
	adc	hl, hl
	sbc	hl, de
	jr	nc, 5$
	add	hl, de
5$:	ccf
	rla
	adc	hl, hl
	sbc	hl, de
	jr	nc, 6$
	add	hl, de
6$:	ccf
	rla
	adc	hl, hl
	sbc	hl, de
	jr	nc, 7$
	add	hl, de
7$:	ccf
	rla
	adc	hl, hl
	sbc	hl, de
	jr	nc, 8$
	add	hl, de
8$:	ccf
	rla
	ret

;; Делитель >= 32768: после сдвига возможен 17-й бит
bp16_big:
	ld	b, #8
1$:	rla
	adc	hl, hl
	jr	c, 4$
	sbc	hl, de
	jr	nc, 2$
	add	hl, de			; вернуть (CF = 1)
2$:	ccf
3$:	djnz	1$
	rla
	ret
4$:	ccf				; 17-й бит: вычесть, заём будет всегда — это бит частного 1
	sbc	hl, de
	jr	3$
