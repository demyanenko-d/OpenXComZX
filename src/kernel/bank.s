;; Джампер кода между страницами: помощники для банковых вызовов SDCC.
;;
;; Вызов __banked-функции SDCC компилирует в
;;     ld e, #<банк>  /  ld hl, #<адрес>  /  call ___sdcc_bcall_ehl
;; Библиотечный трамплин (lib/src/z80/__sdcc_bcall.s) сохраняет банк
;; вызывающего (get_bank), включает банк цели (set_bank), вызывает функцию и
;; восстанавливает банк, не портя результат (A / DE / HL).
;;
;; Банк N — код, слинкованный по виртуальному адресу N*#10000 + #8000
;; (-Wl-bBANKn=0xn8000), живёт в физической странице _bank_page[N]
;; (таблица генерируется сборкой: tmp/build/banks.s) и подключается в Win2.
;; Банк 0 — «нет банка» (то, что стоит в Win2 при старте).
;;
;; Контракт трамплина: set_bank — A = банк, BC/DE/HL сохранить;
;; get_bank — A = текущий банк, прочие регистры сохранить.

	.module bank
	.globl	set_bank
	.globl	get_bank
	.globl	_bank_page

	.area	_KDATA
_cur_bank::	.ds	1

	.area	_CODE

;; Из C (--sdcccall 1: байт-аргумент и результат в A) — те же точки входа:
;; void set_bank(uint8_t), uint8_t get_bank(void).
_set_bank::
set_bank::
	ld	(_cur_bank), a
	push	hl
	push	bc
	ld	hl, #_bank_page
	ld	c, a
	ld	b, #0
	add	hl, bc
	ld	a, (hl)
	ld	bc, #0x12AF		; Page2
	out	(c), a
	pop	bc
	pop	hl
	ret

_get_bank::
get_bank::
	ld	a, (_cur_bank)
	ret
