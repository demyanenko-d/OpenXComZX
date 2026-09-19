;; Стартовый код и ядро OpenXComZX (sdasz80, SDCC 4.5, --sdcccall 1).
;;
;; Раскладка — src/inc/memmap.h, project_docs/08_ui_port_plan.md §4:
;;   Win0 #0000-#3FFF  стр. #04 ядро: RST, общий код (_CODE, _HOME...) с #0100,
;;                     стек вниз от #3E00, векторы IM2 #3EFB-#3F00
;;   Win1 #4000-#7FFF  стр. #05 данные: _DATA.._HEAP; заглушка входа #7FE0
;;   Win2 #8000-#BFFF  банки кода (__banked, set_bank/get_bank — bank.s)
;;   Win3 #C000-#FFFF  дальние данные (без кэша)
;;
;; Загрузчик SPG ставит Win1 = стр. 5, Win2 = стр. 2, в Win0 — ПЗУ, и прыгает
;; на заглушку #7FE0 (PC из заголовка) с запрещёнными прерываниями.
;; Этот файл линкуется первым: он задаёт порядок областей.

	.module crt0
	.globl	_boot
	.globl	b_boot
	.globl	___sdcc_bcall_ehl
	.globl	input_isr
	.globl	mus_isr
	.globl	_mus_state
	.globl	_frames
	.globl	_input_on

KERNEL_PAGE	= 0x04
IM2_I		= 0x3E
STACK_TOP	= 0x3E00

;; ---------------------------------------------------------------------
;; Абсолютные адреса: RST, векторы IM2, заглушка входа
;; ---------------------------------------------------------------------
	.area	_HEADER (ABS)

	.org	0x0000
	;; RST 0 / переход по нулевому указателю — авария
	di
	jp	panic

	.org	0x0038
	;; RST 38 (IM1 не используется)
	ei
	reti

	;; Векторы IM2: I = #3E, на шине #FB (DMA), #FD (LINE), #FF (FRAME).
	.org	0x3EFB
	.dw	isr_nop		; #3EFB DMA (не включено: INTMask = только кадр; очередь dmaq убрана)
	.dw	isr_nop		; #3EFD LINE (в TSConf приходит на каждой строке — не используем)
	.dw	isr_frame	; #3EFF FRAME (байты #3EFF-#3F00)

	;; Заглушка входа SPG — в конце страницы данных (Win1, стр. #05):
	;; подключить ядро в Win0 и перейти в него. 17 байт: #7FE0-#7FF0.
	.org	0x7FE0
	di
	ld	bc, #0x10AF		; Page0 = страница ядра
	ld	a, #KERNEL_PAGE
	out	(c), a
	ld	b, #0x21		; MemConfig = W0_RAM | W0_MAP_N | W0_WE
	ld	a, #0x0E
	out	(c), a
	jp	_start

;; ---------------------------------------------------------------------
;; Порядок областей. Области без явной базы цепляются к предыдущей:
;;   _CODE (--code-loc #0100) … _GSFINAL — общий код в Win0,
;;   _DATA (--data-loc #4000) … _KDATA   — данные в Win1.
;; Банки BANKn получают базу через -Wl-bBANKn=0xn8000.
;; ---------------------------------------------------------------------
	.area	_CODE
	.area	_HOME
	.area	_INITIALIZER
	.area	_GSINIT
	.area	_GSFINAL
	.area	_DATA
	.area	_INITIALIZED
	.area	_BSEG
	.area	_BSS
	.area	_HEAP
	.area	_KDATA
	.area	_DMABUF			; база -Wl-b_DMABUF=0x7C00 (чётная), dmabuf.s
	.area	_MUSIC			; база -Wl-b_MUSIC=0x2800 (Win0), music_s.s

;; ---------------------------------------------------------------------
;; Ядро (общий код, Win0)
;; ---------------------------------------------------------------------
	.area	_CODE

_start::
	di
	ld	sp, #STACK_TOP

	;; обнулить _DATA, _BSS, _KDATA; скопировать _INITIALIZER -> _INITIALIZED
	ld	hl, #s__DATA
	ld	bc, #l__DATA
	call	zero_mem
	ld	hl, #s__BSS
	ld	bc, #l__BSS
	call	zero_mem
	ld	hl, #s__KDATA
	ld	bc, #l__KDATA
	call	zero_mem
	ld	bc, #l__INITIALIZER
	ld	a, b
	or	a, c
	jr	z, init_done
	ld	de, #s__INITIALIZED
	ld	hl, #s__INITIALIZER
	ldir
init_done:
	call	gsinit

	ld	a, #IM2_I
	ld	i, a
	im	2

	;; запуск — банк 11 (src/kernel/boot.c): инициализация, затем ui_run
	ld	e, #b_boot
	ld	hl, #_boot
	call	___sdcc_bcall_ehl

	;; boot вернулся — выйти с кодом 0 (в эмуляторе), иначе стоять
	ld	bc, #0xFAAF
	xor	a
	out	(c), a
halt_forever:
	di
	halt
	jr	halt_forever

;; HL = адрес, BC = длина
zero_mem:
	ld	a, b
	or	a, c
	ret	z
	ld	(hl), #0
	dec	bc
	ld	a, b
	or	a, c
	ret	z
	ld	e, l
	ld	d, h
	inc	de
	ldir
	ret

;; Авария: сообщение в отладочный порт эмулятора, выход с кодом #EE.
panic:
	ld	hl, #panic_msg
	ld	bc, #0xF8AF
panic_loop:
	ld	a, (hl)
	or	a
	jr	z, panic_exit
	out	(c), a
	inc	hl
	jr	panic_loop
panic_exit:
	ld	b, #0xFA
	ld	a, #0xEE
	out	(c), a
panic_halt:
	halt
	jr	panic_halt
panic_msg:
	.ascii	"\nPANIC: RST 0\n"
	.db	0

;; Кадровое прерывание: счётчик кадров, защёлка ввода (input.s). Win2 и Win3 не
;; трогаются; C не вызывается — IX/IY не портятся.
;;
;; Сплит-экран боя (17_battle_render.md §3): в TSConf INT_LINE приходит на КАЖДОЙ строке, а
;; прерывание на заданной строке — это кадровое (VSInt). Поэтому сплит делает одно прерывание,
;; дважды за кадр переставляющее себя: на строке разреза ставит смещения панели, в гашении —
;; смещения карты (и там же обычная работа кадра).
isr_frame:
	push	af
	push	bc
	push	de
	push	hl
	ld	a, (_split_on)
	or	a
	jr	z, frame_work
	ld	a, (_split_phase)
	or	a
	jr	nz, frame_work		; фаза 1: гашение — смещения карты и работа кадра
	ld	a, (_split_gx)		; фаза 0: строка разреза — смещения панели
	ld	bc, #0x02AF
	out	(c), a
	ld	a, (_split_gy)
	ld	bc, #0x04AF
	out	(c), a
	xor	a, a
	ld	bc, #0x03AF
	out	(c), a
	ld	bc, #0x05AF
	out	(c), a
	inc	a
	ld	(_split_phase), a
	ld	hl, (_vblank_line)	; следующее прерывание — до начала картинки
	call	set_vsint
	ld	b, #20			; переждать сигнал INT: иначе он застанет нас сразу после
split_wait:				; reti и обработчик отработает второй раз, вернув карту
	nop
	djnz	split_wait
	jr	frame_done
frame_work:
	ld	hl, (_frames)
	inc	hl
	ld	(_frames), hl
	ld	a, (_input_on)
	or	a
	call	nz, input_isr
	ld	a, (_mus_state)
	dec	a
	call	z, mus_isr		; 1 — играет: отдать чипу кадр музыки
	ld	a, (_split_on)
	or	a
	jr	z, frame_done
	call	set_map_offs		; бой: вернуть смещения карты до начала картинки
	xor	a, a
	ld	(_split_phase), a
	ld	hl, (_split_line)	; следующее прерывание — на строке разреза
	call	set_vsint
frame_done:
	pop	hl
	pop	de
	pop	bc
	pop	af
	ei
	reti

;; HL = строка прерывания (0..319)
set_vsint:
	ld	bc, #0x23AF		; VSIntL
	out	(c), l
	ld	bc, #0x24AF		; VSIntH
	out	(c), h
	ret

	.area	_DATA
_frames::	.ds	2		; кадров с запуска (ui.c, newgame.c)
_input_on::	.ds	1		; 1 — защёлка ввода включена (ставит boot после input_init)
_split_gx::	.ds	1		; сплит боя: смещения панели
_split_gy::	.ds	1
_map_gx::	.ds	2		; смещения карты
_map_gy::	.ds	2
_split_line::	.ds	2		; строка разреза
_vblank_line::	.ds	2		; строка возврата смещений карты (до картинки)
_split_phase::	.ds	1		; 0 — следующее прерывание на строке разреза, 1 — в гашении
_split_on::	.ds	1		; 1 — сплит включён (бой)
	.area	_CODE

;; Смещения карты боя в начале кадра (9 бит на регистр)
set_map_offs:
	ld	hl, (_map_gx)
	ld	bc, #0x02AF		; GXOffsL
	out	(c), l
	ld	bc, #0x03AF		; GXOffsH
	out	(c), h
	ld	hl, (_map_gy)
	ld	bc, #0x04AF		; GYOffsL
	out	(c), l
	ld	bc, #0x05AF		; GYOffsH
	out	(c), h
	ret

isr_nop:
	ei
	reti

	.area	_GSINIT
gsinit:
	.area	_GSFINAL
	ret
