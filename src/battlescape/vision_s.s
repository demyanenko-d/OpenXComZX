;; Банк 40: цикл линии тумана войны (vision.c fov_line) — горячее место пересчёта сектора.
;; Шаг calculateLine по Брезенхему, запись пути (pp_*) и открытых клеток (ml_*). Шаг по тому же этажу
;; решает hb_fast (hblk на ассемблере), со сменой этажа — vision.c fog_slow (vblk на C). Результат
;; тот же, что у C-цикла. Состояние линии — запись F (vision.c fls_t) в памяти банка.
;; (Таблица «шаг из клетки свободен» с ленивым заполнением пробовалась и не окупилась: клетка за
;; ходьбу проверяется мало раз, а заполнение — восемь hblk; 16 §5.2.)

	.module vision_s
	.optsdcc -mz80 sdcccall(1)

	.globl	_fl_line, _fl_run, _hb_fast, _fog_slow, _pg_map3

	.area	_BANK40

FLS	= 0xBE90		; vision.c F (fls_t): поля по порядку
FL_UX	= FLS + 0		; клетка бойца
FL_UY	= FLS + 1
FL_UZ	= FLS + 2
FL_X	= FLS + 3		; позиция в осях линии (главная, ведомые)
FL_Y	= FLS + 4
FL_Z	= FLS + 5
FL_LX	= FLS + 6		; прошлая позиция (смещение от бойца, обычные оси)
FL_LY	= FLS + 7
FL_LZ	= FLS + 8
FL_I	= FLS + 9		; номер позиции
FL_DDX	= FLS + 10
FL_DDY	= FLS + 11
FL_DDZ	= FLS + 12
FL_STX	= FLS + 13
FL_STY	= FLS + 14
FL_STZ	= FLS + 15
FL_SXY	= FLS + 16
FL_SXZ	= FLS + 17
FL_DXY	= FLS + 18		; слово
FL_DXZ	= FLS + 20		; слово
FL_NM	= FLS + 22		; открытых клеток в ml_*
FL_B	= FLS + 23		; выход: позиция упора (#FF — дошла)
FL_BX	= FLS + 25		; для fog_slow: откуда (прошлая клетка, абсолютная)
FL_BY	= FLS + 26
FL_BZ	= FLS + 27
FL_AX	= FLS + 28		;   куда
FL_AY	= FLS + 29
FL_AZ	= FLS + 30
FL_SKIP	= FLS + 31		;   пропуск объекта (позиции 0 и 1)
FL_CX	= FLS + 32		; текущая позиция (смещение, обычные оси)
FL_CY	= FLS + 33
FL_CZ	= FLS + 34
FL_X1	= FLS + 35		; fl_line: цель (смещение от бойца)
FL_Y1	= FLS + 36
FL_Z1	= FLS + 37

PPX	= 0xBE48		; vision.c pp_x / pp_y / pp_z
PPY	= 0xBE60
PPZ	= 0xBE78
MLX	= 0xBE00		; vision.c ml_x / ml_y / ml_z
MLY	= 0xBE18
MLZ	= 0xBE30
YTAB	= 0xBC00		; vision.c ytab: y * sx (слова)
ZTAB	= 0xBC80		; vision.c ztab: z * sx * sy
VMCUR	= 0xBCD5		; vision.c vm_cur: что сейчас в Win3

;; void fl_line(void): линия к цели F.x1, y1, z1 с позиции F.i (позиции до неё — в pp_* от прошлой
;; линии). Оси и знаки линии (calculateLine: главная — самая длинная, xy, затем xz), состояние
;; ведомых осей на позиции i — прогоном Брезенхема от нуля без проверок, дальше — fl_run.
;; Выход: F.nm открытых клеток в ml_*, F.b — позиция упора (#FF — дошла или вся линия общая)
_fl_line::
	xor	a, a
	ld	(FL_NM), a
	ld	(FL_SXY), a
	ld	(FL_SXZ), a
	ld	a, (FL_X1)
	ld	b, a			; B = a1 (главная)
	ld	a, (FL_Y1)
	ld	c, a			; C = b1
	ld	a, (FL_Z1)
	ld	d, a			; D = c1
	;; |b1| > |a1| — главная ось y
	ld	a, c
	call	aabs
	ld	e, a
	ld	a, b
	call	aabs
	cp	a, e
	jr	nc, 1$
	ld	a, b
	ld	b, c
	ld	c, a
	ld	a, #1
	ld	(FL_SXY), a
1$:	;; |c1| > |a1| — главная ось z
	ld	a, d
	call	aabs
	ld	e, a
	ld	a, b
	call	aabs
	cp	a, e
	jr	nc, 2$
	ld	a, b
	ld	b, d
	ld	d, a
	ld	a, #1
	ld	(FL_SXZ), a
2$:	ld	a, b
	call	aabs
	ld	(FL_DDX), a
	ld	a, c
	call	aabs
	ld	(FL_DDY), a
	ld	a, d
	call	aabs
	ld	(FL_DDZ), a
	ld	a, b
	call	asgn
	ld	(FL_STX), a
	ld	a, c
	call	asgn
	ld	(FL_STY), a
	ld	a, d
	call	asgn
	ld	(FL_STZ), a
	;; позиция i за концом линии — путь целиком общий с прошлой
	ld	a, (FL_DDX)
	ld	b, a
	ld	a, (FL_I)
	cp	a, b
	jr	z, 3$
	jr	c, 3$
	ld	a, #0xFF
	ld	(FL_B), a
	ret
3$:	;; начало: позиция 0, наклоны ddx / 2
	xor	a, a
	ld	(FL_X), a
	ld	(FL_Y), a
	ld	(FL_Z), a
	ld	(FL_LX), a
	ld	(FL_LY), a
	ld	(FL_LZ), a
	ld	a, (FL_DDX)
	srl	a
	ld	l, a
	ld	h, #0
	ld	(FL_DXY), hl
	ld	(FL_DXZ), hl
	ld	a, (FL_I)
	or	a, a
	jp	z, _fl_run
	;; прошлая позиция — из пути прошлой линии
	dec	a
	ld	e, a
	ld	d, #0
	ld	hl, #PPX
	add	hl, de
	ld	a, (hl)
	ld	(FL_LX), a
	ld	hl, #PPY
	add	hl, de
	ld	a, (hl)
	ld	(FL_LY), a
	ld	hl, #PPZ
	add	hl, de
	ld	a, (hl)
	ld	(FL_LZ), a
	;; Брезенхем вперёд на i позиций без проверок
	ld	a, (FL_I)
	ld	b, a
4$:	push	bc
	call	fl_adv
	pop	bc
	djnz	4$
	jp	_fl_run

;; A = |A|
aabs:
	bit	7, a
	ret	z
	neg
	ret
;; A = знак шага: -1 при A < 0, иначе 1
asgn:
	bit	7, a
	ld	a, #1
	ret	z
	ld	a, #-1
	ret

;; void fl_run(void): линия с позиции F.i до конца или упора
_fl_run::
fl_loop:
	;; c = позиция в обычных осях: обмен в обратном порядке (сначала xz, потом xy)
	ld	a, (FL_X)
	ld	b, a
	ld	a, (FL_Y)
	ld	c, a
	ld	a, (FL_Z)
	ld	d, a
	ld	a, (FL_SXZ)
	or	a, a
	jr	z, 1$
	ld	a, b
	ld	b, d
	ld	d, a
1$:	ld	a, (FL_SXY)
	or	a, a
	jr	z, 2$
	ld	a, b
	ld	b, c
	ld	c, a
2$:	ld	a, b
	ld	(FL_CX), a
	ld	a, c
	ld	(FL_CY), a
	ld	a, d
	ld	(FL_CZ), a
	;; путь: pp[i] = c
	ld	a, (FL_I)
	ld	e, a
	ld	d, #0
	ld	hl, #PPX
	add	hl, de
	ld	(hl), b
	ld	hl, #PPY
	add	hl, de
	ld	(hl), c
	ld	hl, #PPZ
	add	hl, de
	ld	a, (FL_CZ)
	ld	(hl), a
	;; a = боец + c
	ld	a, (FL_UX)
	add	a, b
	ld	(FL_AX), a
	ld	a, (FL_UY)
	add	a, c
	ld	(FL_AY), a
	ld	a, (FL_UZ)
	ld	hl, #FL_CZ
	add	a, (hl)
	ld	(FL_AZ), a
	ld	a, e
	or	a, a
	jp	z, fl_pass		; позиция 0 — клетка бойца: блокировок нет
	;; b = боец + прошлая позиция
	ld	a, (FL_UX)
	ld	hl, #FL_LX
	add	a, (hl)
	ld	(FL_BX), a
	ld	a, (FL_UY)
	ld	hl, #FL_LY
	add	a, (hl)
	ld	(FL_BY), a
	ld	a, (FL_UZ)
	ld	hl, #FL_LZ
	add	a, (hl)
	ld	(FL_BZ), a
	;; пропуск объекта — позиции 0 и 1 (steps < 2)
	ld	a, e
	cp	a, #2
	ld	a, #0
	adc	a, a
	ld	(FL_SKIP), a
	;; этаж сменился — vblk, медленный путь на C
	ld	a, (FL_LZ)
	ld	hl, #FL_CZ
	cp	a, (hl)
	jr	nz, fl_slow
	;; тот же этаж: vblk = 0, шаг решает hblk (hb_fast) — 0 свободно, 1 упор, #FF большая стена
	ld	a, (FL_BX)
	ld	(HB_SX), a
	ld	a, (FL_BY)
	ld	(HB_SY), a
	ld	a, (FL_BZ)
	ld	(HB_Z), a
	ld	a, (FL_AX)
	ld	(HB_EX), a
	ld	a, (FL_AY)
	ld	(HB_EY), a
	ld	a, (FL_SKIP)
	ld	(HB_SKIP), a
	call	_hb_fast
	or	a, a
	jr	z, fl_pass
	inc	a
	jr	nz, fl_stop		; 1 — упор: клетка не открывается
	call	fl_add			; большая стена: клетка в траектории, дальше нет
	jr	fl_stop
fl_slow:
	call	_fog_slow		; A: 0 — свободно, 1 — упор, 2 — большая стена: клетка видна, дальше нет
	or	a, a
	jr	z, fl_pass
	dec	a
	jr	z, fl_stop
	call	fl_add
fl_stop:
	ld	a, (FL_I)
	ld	(FL_B), a
	ret
fl_pass:
	call	fl_add
	ld	a, (FL_CX)
	ld	(FL_LX), a
	ld	a, (FL_CY)
	ld	(FL_LY), a
	ld	a, (FL_CZ)
	ld	(FL_LZ), a
	ld	a, (FL_DDX)
	ld	b, a
	ld	a, (FL_I)
	cp	a, b
	jr	nz, 4$
	ld	a, #0xFF		; дошла до цели
	ld	(FL_B), a
	ret
4$:	inc	a
	ld	(FL_I), a
	call	fl_adv
	jp	fl_loop

;; Шаг Брезенхема (позиция x, y, z в осях линии): ведомые оси — drift -= dd; < 0 — шаг и drift += ddx
fl_adv:
	ld	a, (FL_DDX)
	ld	c, a			; C = ddx
	ld	hl, (FL_DXY)
	ld	a, (FL_DDY)
	ld	e, a
	ld	d, #0
	or	a, a
	sbc	hl, de
	jp	p, 5$
	ld	e, c
	add	hl, de
	ld	a, (FL_STY)
	ld	b, a
	ld	a, (FL_Y)
	add	a, b
	ld	(FL_Y), a
5$:	ld	(FL_DXY), hl
	ld	hl, (FL_DXZ)
	ld	a, (FL_DDZ)
	ld	e, a
	ld	d, #0
	or	a, a
	sbc	hl, de
	jp	p, 6$
	ld	e, c
	add	hl, de
	ld	a, (FL_STZ)
	ld	b, a
	ld	a, (FL_Z)
	add	a, b
	ld	(FL_Z), a
6$:	ld	(FL_DXZ), hl
	ld	a, (FL_STX)
	ld	b, a
	ld	a, (FL_X)
	add	a, b
	ld	(FL_X), a
	ret

;; Клетка a (FL_AX..AZ) — в список открытых
fl_add:
	ld	a, (FL_NM)
	ld	e, a
	ld	d, #0
	inc	a
	ld	(FL_NM), a
	ld	hl, #MLX
	add	hl, de
	ld	a, (FL_AX)
	ld	(hl), a
	ld	hl, #MLY
	add	hl, de
	ld	a, (FL_AY)
	ld	(hl), a
	ld	hl, #MLZ
	add	hl, de
	ld	a, (FL_AZ)
	ld	(hl), a
	ret

;; Номер направления по тому же индексу (vision.c dir_of); 8 — на месте
dir_no:
	.db	7, 6, 5, 0, 8, 4, 1, 2, 3

;; ---------------------------------------------------------------- hblk на ассемблере
;; TileEngine::horizontalBlockage(start, end, DT_NONE, skip) — тот же разбор, что vision.c hblk, но
;; вместо суммы блокировок (0 или >= 255 у каждого слагаемого) — признак: A = 0 — свободно,
;; 1 — упор (> 127), #FF — большая стена (-1). Вход — поля HB (vision.c hb_t).

HB	= 0xBEC0
HB_SX	= HB + 0		; откуда
HB_SY	= HB + 1
HB_Z	= HB + 2
HB_EX	= HB + 3		; куда (тот же этаж)
HB_EY	= HB + 4
HB_SKIP	= HB + 5		; пропуск объекта своей клетки (первые шаги линии)
HB_VMP	= HB + 6		; первая страница маски обзора (копия vm_page)
HB_MSX	= HB + 7		; размеры поля (копии vm_sx, vm_sy, vm_sz)
HB_MSY	= HB + 8
HB_MSZ	= HB + 9
HB_DIR	= HB + 10		; рабочие
HB_S	= HB + 11
HB_E	= HB + 12
HB_T	= HB + 13
HB_ORG	= HB + 14
HB_B	= HB + 15

VM_W	= 1
VM_N	= 2
VM_O	= 4

;; uint8_t hb_fast(void)
_hb_fast::
	ld	a, (HB_SX)
	ld	b, a
	ld	a, (HB_SY)
	ld	c, a
	ld	a, (HB_Z)
	ld	d, a
	call	ainm
	jp	nc, hb_zero
	ld	a, (HB_EX)
	ld	b, a
	ld	a, (HB_EY)
	ld	c, a
	call	ainm
	jp	nc, hb_zero
	;; направление: dir_no[(dx + 1) * 3 + (dy + 1)]
	ld	a, (HB_SX)
	ld	b, a
	ld	a, (HB_EX)
	sub	a, b
	inc	a
	cp	a, #3
	jp	nc, hb_zero
	ld	c, a
	add	a, a
	add	a, c
	ld	c, a
	ld	a, (HB_SY)
	ld	b, a
	ld	a, (HB_EY)
	sub	a, b
	inc	a
	cp	a, #3
	jp	nc, hb_zero
	add	a, c
	ld	e, a
	ld	d, #0
	ld	hl, #dir_no
	add	hl, de
	ld	a, (hl)
	cp	a, #8
	jp	z, hb_zero
	ld	(HB_DIR), a
	;; маски своей и соседней клетки
	ld	a, (HB_Z)
	ld	d, a
	ld	a, (HB_SX)
	ld	b, a
	ld	a, (HB_SY)
	ld	c, a
	call	amk
	ld	(HB_S), a
	ld	a, (HB_EX)
	ld	b, a
	ld	a, (HB_EY)
	ld	c, a
	call	amk
	ld	(HB_E), a
	xor	a, a
	ld	(HB_B), a
	ld	(HB_ORG), a
	ld	a, (HB_DIR)
	add	a, a
	ld	e, a
	ld	d, #0
	ld	hl, #hb_case
	add	hl, de
	ld	e, (hl)
	inc	hl
	ld	d, (hl)
	ex	de, hl
	jp	(hl)
hb_case:
	.dw	hc0, hc1, hc2, hc3, hc4, hc5, hc6, hc7

;; b = NB(s)
hc0:	ld	a, (HB_S)
	and	a, #VM_N
	jp	hb_set
;; b = WB(e)
hc2:	ld	a, (HB_E)
	and	a, #VM_W
	jp	hb_set
;; b = NB(e)
hc4:	ld	a, (HB_E)
	and	a, #VM_N
	jp	hb_set
;; b = WB(s)
hc6:	ld	a, (HB_S)
	and	a, #VM_W
	jp	hb_set

;; Северо-восток: t = (sx, sy-1): NB(s) + WB(e) + (t.bw != 2 ? obj(t, 3) : 0); если 0 — так и есть,
;; иначе t = (sx+1, sy): NB(t) + WB(t) + (t.bw != 2 ? obj(t, 7) : 0)
hc1:	ld	b, #0
	ld	c, #-1
	call	at_rel
	ld	a, (HB_S)
	and	a, #VM_N
	jr	nz, 1$
	ld	a, (HB_E)
	and	a, #VM_W
	jr	nz, 1$
	ld	e, #3
	ld	l, #2
	call	at_obj
	or	a, a
	jp	z, hb_set
1$:	ld	b, #1
	ld	c, #0
	call	at_rel
	and	a, #VM_N | VM_W
	jp	nz, hb_set
	ld	e, #7
	ld	l, #2
	call	at_obj
	jp	hb_set

;; Юго-восток: t = (sx, sy+1): NB(t) + WB(e) + (t.bw != 3 ? obj(t, 1) : 0); если не 0 —
;; t = (sx+1, sy): WB(t) + NB(e) + (t.bw != 3 ? obj(t, 5) : 0)
hc3:	ld	b, #0
	ld	c, #1
	call	at_rel
	and	a, #VM_N
	jr	nz, 1$
	ld	a, (HB_E)
	and	a, #VM_W
	jr	nz, 1$
	ld	e, #1
	ld	l, #3
	call	at_obj
	or	a, a
	jp	z, hb_set
1$:	ld	b, #1
	ld	c, #0
	call	at_rel
	and	a, #VM_W
	jp	nz, hb_set
	ld	a, (HB_E)
	and	a, #VM_N
	jp	nz, hb_set
	ld	e, #5
	ld	l, #3
	call	at_obj
	jp	hb_set

;; Юго-запад: t = (sx, sy+1): NB(t) + WB(t) + (t.bw != 2 ? obj(t, 7) : 0); если не 0 —
;; t = (sx-1, sy): WB(s) + NB(e) + (t.bw != 2 ? obj(t, 3) : 0)
hc5:	ld	b, #0
	ld	c, #1
	call	at_rel
	and	a, #VM_N | VM_W
	jr	nz, 1$
	ld	e, #7
	ld	l, #2
	call	at_obj
	or	a, a
	jp	z, hb_set
1$:	ld	b, #-1
	ld	c, #0
	call	at_rel
	ld	a, (HB_S)
	and	a, #VM_W
	jp	nz, hb_set
	ld	a, (HB_E)
	and	a, #VM_N
	jp	nz, hb_set
	ld	e, #3
	ld	l, #2
	call	at_obj
	jp	hb_set

;; Северо-запад: t = (sx, sy-1): NB(s) + WB(t) + (t.bw != 3 ? obj(t, 5) : 0); если не 0 —
;; t = (sx-1, sy): WB(s) + NB(t) + (t.bw != 3 ? obj(t, 1) : 0)
hc7:	ld	b, #0
	ld	c, #-1
	call	at_rel
	ld	a, (HB_S)
	and	a, #VM_N
	jr	nz, 1$
	ld	a, (HB_T)
	and	a, #VM_W
	jr	nz, 1$
	ld	e, #5
	ld	l, #3
	call	at_obj
	or	a, a
	jp	z, hb_set
1$:	ld	b, #-1
	ld	c, #0
	call	at_rel
	ld	a, (HB_S)
	and	a, #VM_W
	jp	nz, hb_set
	ld	a, (HB_T)
	and	a, #VM_N
	jp	nz, hb_set
	ld	e, #1
	ld	l, #3
	call	at_obj
	; jp hb_set

;; A != 0 — упор от стен; затем объект своей клетки и большая стена соседней
hb_set:
	or	a, a
	jr	z, 1$
	ld	a, #1
	ld	(HB_B), a
1$:	;; if (!skip || s.bw) b += obj(s, dir, 0)
	ld	a, (HB_SKIP)
	or	a, a
	jr	z, 2$
	ld	a, (HB_S)
	and	a, #0xF0
	jr	z, 3$
2$:	ld	a, (HB_DIR)
	ld	e, a
	ld	a, (HB_S)
	ld	d, #0
	call	aobj
	or	a, a
	jr	z, 3$
	ld	a, #1
	ld	(HB_B), a
3$:	;; if (b <= 127 && obj(e, (dir + 4) & 7, 1) > 127) return -1
	ld	a, (HB_B)
	or	a, a
	ret	nz
	ld	a, (HB_DIR)
	add	a, #4
	and	a, #7
	ld	e, a
	ld	a, (HB_E)
	ld	d, #1
	call	aobj
	or	a, a
	ret	z
	ld	a, #0xFF
	ret
hb_zero:
	xor	a, a
	ret

;; T = маска клетки (sx + B, sy + C) на этаже HB_Z; A = она же
at_rel:
	ld	a, (HB_SX)
	add	a, b
	ld	b, a
	ld	a, (HB_SY)
	add	a, c
	ld	c, a
	ld	a, (HB_Z)
	ld	d, a
	call	amk
	ld	(HB_T), a
	ret

;; Объект клетки T по направлению E, если её большая стена не L: A = 1 — держит, 0 — нет
at_obj:
	ld	a, (HB_T)
	rrca
	rrca
	rrca
	rrca
	and	a, #0x0F
	cp	a, l
	jr	z, 1$
	ld	a, (HB_T)
	ld	d, #0
	jp	aobj
1$:	xor	a, a
	ret

;; obj_blk(m = A, dir = E (0..7), origin = D): A = 1 — объект держит обзор, 0 — нет
aobj:
	bit	2, a			; VM_O
	jr	z, 2$
	rrca
	rrca
	rrca
	rrca
	and	a, #0x0F		; тип большой стены
	ld	c, a
	ld	a, d
	or	a, a
	jr	z, 1$
	ld	a, c			; со стороны начала линии диагональные (2, 3) не держат
	cp	a, #2
	jr	z, 2$
	cp	a, #3
	jr	z, 2$
1$:	ld	b, #0
	ld	hl, #obj_exc
	add	hl, bc
	ld	b, (hl)			; направления, по которым эта большая стена не держит
	ld	d, #0
	ld	hl, #dir_mask
	add	hl, de
	ld	a, (hl)
	and	a, b
	jr	nz, 2$
	ld	a, #1
	ret
2$:	xor	a, a
	ret

;; Бит направления 0..7
dir_mask:
	.db	0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80
;; obj_blk: по каким направлениям 0..7 большая стена типа w не держит обзор (Pathfinding::bigWallTypes:
;; 4 WEST, 5 NORTH, 6 EAST, 7 SOUTH, 8 EAST_AND_SOUTH, 9 WEST_AND_NORTH; vision.c obj_blk)
obj_exc:
	.db	0x00, 0x00, 0x00, 0x00, 0x1F, 0x7C, 0xF1, 0xC7, 0xC1, 0x1C, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00

;; На поле ли клетка (B, C, D): перенос — да
ainm:
	ld	a, b
	ld	hl, #HB_MSX
	cp	a, (hl)
	ret	nc
	ld	a, c
	ld	hl, #HB_MSY
	cp	a, (hl)
	ret	nc
	ld	a, d
	ld	hl, #HB_MSZ
	cp	a, (hl)
	ret

;; Маска клетки (B, C, D) — A (вне поля 0). B, C, D сохраняются
amk:
	call	ainm
	jr	nc, 2$
	push	de
	ld	a, d
	add	a, a
	ld	e, a
	ld	d, #0
	ld	hl, #ZTAB
	add	hl, de
	ld	e, (hl)
	inc	hl
	ld	d, (hl)
	ld	a, c
	add	a, a
	ld	l, a
	ld	h, #>YTAB
	ld	a, (hl)
	inc	hl
	ld	h, (hl)
	ld	l, a
	add	hl, de
	ld	e, b
	ld	d, #0
	add	hl, de
	pop	de
	ld	a, h
	rlca
	rlca
	and	a, #3
	push	bc
	ld	c, a
	ld	a, (HB_VMP)
	add	a, c
	ld	c, a
	ld	a, (VMCUR)
	cp	a, c
	jr	z, 1$
	ld	a, c
	ld	(VMCUR), a
	push	hl
	push	de
	call	_pg_map3
	pop	de
	pop	hl
1$:	pop	bc
	ld	a, h
	and	a, #0x3F
	or	a, #0xC0
	ld	h, a
	ld	a, (hl)
	ret
2$:	xor	a, a
	ret
