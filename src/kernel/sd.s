;; SD-карта через Z-Controller (общий код): инициализация, чтение и запись
;; сектора. Последовательности команд — REF/frontend/z80_fw/sd.s и драйвер
;; TS-BIOS (REF/tsbios/src/booter.asm, pff/diskio.c); порты — 02 §8:
;;   #57  запись — выдать байт; чтение — такт и байт MISO
;;   #77  бит 1 — CS# карты (#01 выбрана, #03 отпущена)
;;
;; Код лежит в банке 12 (область _BANK12), вызывается только из fat.c того же банка.
;; Вызов из C (sdcccall 1): аргументы в глобальных переменных, результат в A
;; (0 — успех):
;;   _sd_lba  u32 номер сектора
;;   _sd_ptr  u16 адрес CPU буфера 512 байт (страницу в Win3 подключает вызывающий)
;;   _sd_phys u32 физический адрес приёмника (чётный) для sd_rdm
;;   _sd_cnt  u16 секторов для sd_rdm
;;   uint8_t sd_init(void); uint8_t sd_rd(void); uint8_t sd_wr(void);
;;   uint8_t sd_rdm(void) — _sd_cnt секторов подряд с _sd_lba (CMD18), каждый
;;     сектор — DMA SPI->RAM (#02, 256 слов) прямо по физическому адресу (без
;;     окон CPU; адрес DMA идёт через страницы). 02 §8.

	.module sd
	.globl	_sd_init
	.globl	_sd_rd
	.globl	_sd_rdm
	.globl	_sd_wr
	.globl	_sd_lba
	.globl	_sd_ptr
	.globl	_sd_phys
	.globl	_sd_cnt
	.globl	_sd_type

SD_DAT	.equ	0x57
SD_CTL	.equ	0x77
CT_BLOCK .equ	0x08

	.area	_DATA
_sd_lba::	.ds	4
_sd_ptr::	.ds	2
_sd_type::	.ds	1		; 0 — нет карты, CT_BLOCK — блочная адресация, 4 — байтовая
_sd_phys::	.ds	4
_sd_cnt::	.ds	2
sd_arg:		.ds	4		; аргумент команды, старший байт первым

	;; код — в банке 12 вместе с fat.c (единственный вызывающий, вызовы обычные)
	.area	_BANK12

cs_on:	ld	a, #0x01
	out	(SD_CTL), a
	ret

cs_off:	ld	a, #0x03
	out	(SD_CTL), a
	ret

release:
	call	cs_off
	ld	a, #0xFF
	out	(SD_DAT), a
	ret

skip:	ld	a, #0xFF		; B холостых тактов
skip_l:	out	(SD_DAT), a
	djnz	skip_l
	ret

arg_zero:
	ld	hl, #sd_arg
	xor	a
	ld	(hl), a
	inc	hl
	ld	(hl), a
	inc	hl
	ld	(hl), a
	inc	hl
	ld	(hl), a
	ret

;; C — команда (с битом #40), sd_arg — аргумент. Выход: A = R1, CY=1 — нет ответа.
;; cmd_k — без переключения CS и с пропуском байта-заполнителя перед R1 (CMD12
;; посреди CMD18, как FatFs send_cmd).
cmd:	call	cs_off
	ld	a, #0xFF
	out	(SD_DAT), a
	call	cs_on
	ld	a, #0xFF
	out	(SD_DAT), a
	call	cmd_s
	jr	cmd_r
cmd_k:	call	cmd_s
	in	a, (SD_DAT)
	jr	cmd_r
cmd_s:	ld	a, c
	out	(SD_DAT), a
	ld	hl, #sd_arg
	ld	b, #4
cmd_a:	ld	a, (hl)
	out	(SD_DAT), a
	inc	hl
	djnz	cmd_a
	ld	a, c
	cp	#0x40
	jr	z, crc0
	cp	#0x48
	jr	z, crc8
	ld	a, #0x01
	jr	crc_o
crc0:	ld	a, #0x95
	jr	crc_o
crc8:	ld	a, #0x87
crc_o:	out	(SD_DAT), a
	ret
cmd_r:	ld	b, #10
cmd_w:	in	a, (SD_DAT)
	bit	7, a
	jr	z, cmd_ok
	djnz	cmd_w
	scf
	ret
cmd_ok:	or	a
	ret

;; ---------------------------------------------------------------- инициализация

_sd_init::
	xor	a
	ld	(_sd_type), a
	call	cs_off
	ld	b, #250
	call	skip
	ld	b, #250
	call	skip
	ld	de, #8000		; CMD0 до ответа #01 (карта могла быть занята)
init0:	push	de
	call	arg_zero
	ld	c, #0x40
	call	cmd
	pop	de
	jr	c, init0n
	cp	#0x01
	jr	z, init8
init0n:	dec	de
	ld	a, d
	or	e
	jr	nz, init0
	jp	fail

init8:	call	arg_zero		; CMD8: SDv2? эхо #01AA
	ld	hl, #sd_arg + 2
	ld	(hl), #0x01
	inc	hl
	ld	(hl), #0xAA
	ld	c, #0x48
	call	cmd
	jp	c, fail
	cp	#0x01
	jr	nz, sdv1
	in	a, (SD_DAT)
	in	a, (SD_DAT)
	in	a, (SD_DAT)
	cp	#0x01
	jp	nz, fail
	in	a, (SD_DAT)
	cp	#0xAA
	jp	nz, fail
	ld	e, #0x40		; ACMD41 с HCS
	jr	acmd

sdv1:	ld	e, #0			; SDv1: ACMD41 без HCS, адресация байтовая
acmd:	ld	hl, #0x8000
acmd_l:	push	hl
	push	de
	call	arg_zero
	ld	c, #0x77		; CMD55
	call	cmd
	pop	de
	push	de
	call	arg_zero
	ld	a, e
	ld	(sd_arg), a
	ld	c, #0x69		; ACMD41
	call	cmd
	pop	de
	pop	hl
	jp	c, fail
	or	a
	jr	z, ready
	dec	hl
	ld	a, h
	or	l
	jr	nz, acmd_l
	jp	fail

ready:	ld	a, e
	or	a
	ld	a, #0x04
	jr	z, settype		; SDv1 — байтовая
	call	arg_zero		; CMD58: OCR, бит 30 (CCS) — блочная
	ld	c, #0x7A
	call	cmd
	jp	c, fail
	or	a
	jp	nz, fail
	in	a, (SD_DAT)
	ld	e, a
	in	a, (SD_DAT)
	in	a, (SD_DAT)
	in	a, (SD_DAT)
	ld	a, e
	and	#0x40
	ld	a, #0x04
	jr	z, settype
	ld	a, #CT_BLOCK
settype:
	ld	(_sd_type), a
	cp	#CT_BLOCK
	jr	z, init_ok
	call	arg_zero		; байтовая адресация: CMD16 — блок 512
	ld	hl, #sd_arg + 2
	ld	(hl), #0x02
	ld	c, #0x50
	call	cmd
init_ok:
	call	release
	xor	a
	ret

fail:	call	release
	xor	a
	ld	(_sd_type), a
	ld	a, #1
	ret

;; ---------------------------------------------------------------- адрес

;; sd_arg = номер сектора (блочная) или байтовое смещение (* 512)
set_arg:
	ld	a, (_sd_type)
	or	a
	jr	z, sa_bad
	cp	#CT_BLOCK
	jr	nz, sa_byte
	ld	a, (_sd_lba + 3)
	ld	(sd_arg + 0), a
	ld	a, (_sd_lba + 2)
	ld	(sd_arg + 1), a
	ld	a, (_sd_lba + 1)
	ld	(sd_arg + 2), a
	ld	a, (_sd_lba + 0)
	ld	(sd_arg + 3), a
	or	a
	ret
sa_byte:				; lba << 9: байты lba 2,1,0 -> arg 0,1,2 со сдвигом на 1
	ld	a, (_sd_lba + 0)
	add	a, a
	ld	(sd_arg + 2), a
	ld	a, (_sd_lba + 1)
	rla
	ld	(sd_arg + 1), a
	ld	a, (_sd_lba + 2)
	rla
	ld	(sd_arg + 0), a
	xor	a
	ld	(sd_arg + 3), a
	or	a
	ret
sa_bad:	scf
	ret

;; ---------------------------------------------------------------- чтение

_sd_rd::
	call	set_arg
	jr	c, fail_rw
	ld	c, #0x51		; CMD17
	call	cmd
	jr	c, fail_rw
	or	a
	jr	nz, fail_rw
	ld	de, #0x4000		; токен данных #FE
rd_t:	in	a, (SD_DAT)
	cp	#0xFF
	jr	nz, rd_got
	dec	de
	ld	a, d
	or	e
	jr	nz, rd_t
	jr	fail_rw
rd_got:	cp	#0xFE
	jr	nz, fail_rw
	ld	hl, (_sd_ptr)
	ld	c, #SD_DAT
	ld	b, #0
	inir
	ld	b, #0
	inir
	in	a, (SD_DAT)		; CRC
	in	a, (SD_DAT)
	call	release
	xor	a
	ret

fail_rw:
	call	release
	ld	a, #1
	ret

;; ---------------------------------------------------------------- чтение подряд (DMA)

_sd_rdm::
	call	set_arg
	jr	c, fail_rw
	ld	hl, (_sd_cnt)
	ld	a, h
	or	l
	jr	z, fail_rw
	ld	c, #0x52		; CMD18
	call	cmd
	jr	c, fail_rw
	or	a
	jr	nz, fail_rw
rdm_blk:
	ld	de, #0x4000		; токен данных #FE
rdm_t:	in	a, (SD_DAT)
	cp	#0xFF
	jr	nz, rdm_got
	dec	de
	ld	a, d
	or	e
	jr	nz, rdm_t
	jr	rdm_fail
rdm_got:
	cp	#0xFE
	jr	nz, rdm_fail
	call	dma_sec
	in	a, (SD_DAT)		; CRC
	in	a, (SD_DAT)
	ld	hl, (_sd_phys + 1)	; адрес += 512
	inc	hl
	inc	hl
	ld	(_sd_phys + 1), hl
	ld	hl, (_sd_cnt)
	dec	hl
	ld	(_sd_cnt), hl
	ld	a, h
	or	l
	jr	nz, rdm_blk
	call	rdm_stop
	call	release
	xor	a
	ret
rdm_fail:
	call	rdm_stop
	jp	fail_rw

;; DMA SPI->RAM: 256 слов с порта #57 по физическому адресу _sd_phys. Регистры —
;; напрямую с опросом DMAStatus, как в gfx.c / far.c (очередь dmaq не запущена).
dma_sec:
	ld	bc, #0x27AF		; DMAStatus: ждать, пока занят (графика)
ds_w0:	in	a, (c)
	rlca
	jr	c, ds_w0
	ld	a, (_sd_phys + 0)	; DAL = A7:0
	ld	b, #0x1D
	out	(c), a
	ld	a, (_sd_phys + 1)
	ld	e, a
	and	#0x3F			; DAH = A13:8
	ld	b, #0x1E
	out	(c), a
	ld	a, e			; DAX = A21:14 = (p2 << 2) | (p1 >> 6)
	rlca
	rlca
	and	#0x03
	ld	e, a
	ld	a, (_sd_phys + 2)
	add	a, a
	add	a, a
	or	e
	ld	b, #0x1F
	out	(c), a
	ld	a, #0xFF		; DMALen: 256 слов
	ld	b, #0x26
	out	(c), a
	xor	a			; DMANum: 1 пачка
	ld	b, #0x28
	out	(c), a
	ld	a, #0x02		; DMACtrl: SPI->RAM, пуск
	ld	b, #0x27
	out	(c), a
ds_w1:	in	a, (c)
	rlca
	jr	c, ds_w1
	ret

;; CMD12: остановить передачу (CS не отпускать), пропустить байт-заполнитель, R1b —
;; ждать, пока карта занята
rdm_stop:
	call	arg_zero
	ld	c, #0x4C
	call	cmd_k
	ld	de, #0
rdm_s:	in	a, (SD_DAT)
	inc	a
	ret	z
	dec	de
	ld	a, d
	or	e
	jr	nz, rdm_s
	ret

;; ---------------------------------------------------------------- запись

_sd_wr::
	call	set_arg
	jr	c, fail_wr
	ld	c, #0x58		; CMD24
	call	cmd
	jr	c, fail_wr
	or	a
	jr	nz, fail_wr
	ld	a, #0xFF
	out	(SD_DAT), a
	ld	a, #0xFE		; токен блока
	out	(SD_DAT), a
	ld	hl, (_sd_ptr)
	ld	c, #SD_DAT
	ld	b, #0
	otir
	ld	b, #0
	otir
	ld	a, #0xFF		; CRC (не проверяется)
	out	(SD_DAT), a
	out	(SD_DAT), a
	in	a, (SD_DAT)		; ответ данных: xxx0 0101 — принято
	and	#0x1F
	cp	#0x05
	jr	nz, fail_wr
	ld	de, #0		; ждать, пока карта занята (0)
wr_b:	in	a, (SD_DAT)
	or	a
	jr	nz, wr_ok
	dec	de
	ld	a, d
	or	e
	jr	nz, wr_b
fail_wr:
	jp	fail_rw
wr_ok:	call	release
	xor	a
	ret
