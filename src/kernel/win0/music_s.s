;; Плеер музыки: вывод одного кадра записей в чип из кадрового прерывания
;; (project_docs/20_music_player.md §4-§6). В прерывании делается минимум: поток уже
;; разобран главным циклом (src/kernel/music.c) и лежит в кольце готовыми записями.
;;
;; Кольцо — тот же формат, что в MUSIC.PAK, но с двумя гарантиями от главного цикла:
;;   1) запись не пересекает конец кольца (иначе цикл кладёт #80 и начинает сначала);
;;   2) #FF в кольцо не попадает — конец трека и повтор разбирает цикл.
;; Поэтому ISR видит ровно три случая: #00-#7F — запись кадра, #80 — в начало кольца,
;; #81-#FE — пауза. Ни разбора длин, ни проверок границ, ни кольцевой арифметики.
;;
;; Кольцо и переменные — в Win0 (страница ядра стоит всегда), поэтому ISR не переключает
;; ни одного окна памяти и не мешает ни курсору, ни сплит-экрану боя.

	.module music_s
	.globl	_mus_ring
	.globl	_mus_rd
	.globl	_mus_pause
	.globl	_mus_played
	.globl	_mus_pushed
	.globl	_mus_state
	.globl	_mus_under
	.globl	_mus_out
	.globl	mus_isr
	.globl	_mus_out_opl3
	.globl	_mus_out_ay

RING_SIZE	= 4096

	.area	_CODE

;; Отдать чипу записи одного кадра. Портит AF, BC, DE, HL (в isr_frame они сохранены).
mus_isr::
	ld	a, (_mus_pause)
	or	a
	jr	z, mi_fetch
	dec	a			; идёт пауза — просто уменьшить счётчик
	ld	(_mus_pause), a
	jr	mi_tick

mi_fetch:
	ld	a, (_mus_pushed)
	ld	hl, #_mus_played
	sub	a, (hl)
	jr	nz, mi_go
	ld	hl, #_mus_under		; недобор: цикл не успел положить кадр — молчим,
	inc	(hl)			;   чип держит последнюю ноту (лучше щелчка)
	ret	nz
	dec	(hl)			; счётчик насыщается на #FF
	ret
mi_go:
	ld	hl, (_mus_rd)
mi_next:
	ld	a, (hl)
	cp	a, #0x80
	jr	c, mi_frame		; #00-#7F — запись кадра
	jr	nz, mi_wait		; #81-#FE — пауза
	ld	hl, #_mus_ring		; #80 — в начало кольца
	jr	mi_next

mi_wait:
	sub	a, #0x81		; текущий кадр паузы съедаем сейчас
	ld	(_mus_pause), a
	inc	hl
	jr	mi_store

mi_frame:
	inc	hl
	or	a
	jr	z, mi_bank1
	ld	e, a
	ld	bc, #0xFFC4		; банк 0: адрес #C4, данные #C5
	call	mus_call
mi_bank1:
	ld	a, (hl)
	inc	hl
	or	a
	jr	z, mi_store
	ld	e, a
	ld	bc, #0xFFC6		; банк 1: адрес #C6, данные #C7
	call	mus_call
mi_store:
	ld	(_mus_rd), hl
mi_tick:
	ld	hl, #_mus_played
	inc	(hl)
	ret

;; Косвенный вызов процедуры вывода: режим звука выбирается в настройках (11_sound.md),
;; формат записи для всех режимов одинаков.
mus_call:
	ld	a, (_mus_out)
	ld	d, a
	ld	a, (_mus_out + 1)
	or	a, d
	ret	z
	push	hl
	ld	hl, (_mus_out)
	ex	(sp), hl
	ret				; переход по адресу процедуры, возврат к вызывающему

;; E пар (регистр, значение) из (HL) в порт BC (адрес) / BC+1 (данные).
;; Пауза, которую требует чип между адресом и данными, набирается полезной работой;
;; OPLW ниже — добивка нулевыми тактами, подбирается на железе.
_mus_out_opl3::
mo_l:
	ld	a, (hl)
	inc	hl
	out	(c), a			; адрес
	ld	a, (hl)
	inc	hl
	set	0, c			; #C4 -> #C5 (флаги не портит)
	dec	e
	nop				; OPLW
	nop
	out	(c), a			; данные
	res	0, c
	jr	nz, mo_l
	ret

;; То же для AY-3-8910: адрес в #FFFD, данные в #BFFD (02 §7). Поток для AY готовит
;; конвертер (OxzConv/Core/MusicAy.cs) — здесь только выгрузка пар.
_mus_out_ay::
mao_l:
	ld	a, (hl)
	inc	hl
	ld	bc, #0xFFFD		; выбор регистра
	out	(c), a
	ld	a, (hl)
	inc	hl
	ld	b, #0xBF		; #BFFD — данные
	out	(c), a
	dec	e
	jr	nz, mao_l
	ret

	.area	_MUSIC			; база -Wl-b_MUSIC=0x2800 (tools/build.ps1)
_mus_ring::	.ds	RING_SIZE	; кольцо записей
_mus_rd::	.ds	2		; указатель чтения — только ISR
_mus_pause::	.ds	1		; осталось кадров паузы — только ISR
_mus_played::	.ds	1		; кадров сыграно, по модулю 256 — пишет ISR
_mus_pushed::	.ds	1		; кадров положено, по модулю 256 — пишет главный цикл
_mus_state::	.ds	1		; 0 выкл, 1 играет, 2 пауза
_mus_out::	.ds	2		; адрес процедуры вывода (режим звука)
_mus_under::	.ds	1		; недоборы (диагностика, насыщается)
