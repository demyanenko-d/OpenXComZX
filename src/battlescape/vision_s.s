;; Банк 40: пересчёт сектора тумана войны (vision.c fov_unit) — обход таблицы линий (fu_run), линия
;; (fl_line / fl_run: шаг calculateLine по Брезенхему, открытые клетки — в ml_*) и отметки в туман
;; (fm_run). Шаг по тому же этажу решает hb_core (hblk на ассемблере), со сменой этажа — fl_zstep
;; (vblk и hblk, как vision.c los). Результат тот же, что у C-версии. Состояние линии — запись F (vision.c fls_t)
;; в памяти банка.
;; (Таблица «шаг из клетки свободен» с ленивым заполнением пробовалась и не окупилась: клетка за
;; ходьбу проверяется мало раз, а заполнение — восемь hblk; 16 §5.2.)

	.module vision_s
	.optsdcc -mz80 sdcccall(1)

	.globl	_fu_run, _fl_line, _fl_run, _hb_fast, _fm_run, _fog_bbn, _pg_map3

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
FL_BX	= FLS + 25		; шаг: откуда (прошлая клетка, абсолютная)
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
FL_SV	= FLS + 38		; HB_I и HB_S — номер и маска прошлой клетки — верны
FL_AI	= FLS + 39		; слово: номер текущей клетки
FL_K	= FLS + 41		; fm_run: номер в ml_*
FL_BBX	= FLS + 42		; fm_run: клетка для vision.c fog_bbn
FL_BBY	= FLS + 43
FL_BBZ	= FLS + 44
FL_FI	= FLS + 45		; fm_run: слово, номер клетки
FL_TV	= FLS + 47		; fl_zstep: vblk держит

FOGPG	= 0xBCCB		; vision.c fog_pg: первая страница тумана
FOGN	= 0xBCCE		; vision.c vis_fog_n: открытых клеток
FOG_W	= 1
FOG_N	= 2
FOG_C	= 4
FOG_L	= 8

FOGTAB	= 0x0B			; map.h FOGTAB_PAGE: таблица линий (tools/gen_fog_tab.js), 5 байт на запись
TB	= 0xB800		; кусок таблицы: 64 записи
FU	= 0xBEE0		; vision.c U (fu_t): вход
FU_N	= FU + 0		; слово: записей
FU_OFF	= FU + 2		; слово: начало таблицы
FU_SW	= FU + 4		; оси сектора обменены
FU_NX	= FU + 5		; x со знаком минус
FU_NY	= FU + 6		; y со знаком минус
FU_K	= FU + 7		; рабочие: слово, номер записи
FU_TBK	= FU + 9		;   слово, первая запись в TB (#FF00 — пусто)
FU_HAVE	= FU + 11		;   линия уже была (есть общее начало)
FU_PB	= FU + 12		;   позиция упора прошлой линии
FU_LEFF	= FU + 13		;   общее начало с прошлой линией
TC_SRC	= FU + 14		; tcopy: слово, откуда (байт таблицы)
TC_DST	= FU + 16		;   слово, куда
TC_N	= FU + 18		;   слово, сколько

;; Заготовки линии (только ассемблер; бывший путь pp_*)
FX	= 0xBE48
FX_PM	= FX + 0		; слово: указатель на FL_CX / CY / CZ — обычная ось главной оси линии
FX_PY	= FX + 2		;   ведомой y (всегда x или y)
FX_PZ	= FX + 4		;   ведомой z
FX_DM	= FX + 6		; слово: смещение номера клетки шага по главной оси
FX_DY	= FX + 8		;   по ведомой y
FX_DIR0	= FX + 10		; направление шага по главной оси
FX_DIR1	= FX + 11		;   по главной и ведомой y
FX_FAST	= FX + 12		; главная ось — x или y (шаг без смены этажа возможен)
FX_YS	= FX + 13		; fl_adv: был шаг ведомой y
FX_ZS	= FX + 14		;   ведомой z
FX_NY	= FX + 15		; слово: fl_line, шагов ведомой y в прогоне
FX_NZ	= FX + 17		; слово: ведомой z
MLX	= 0xBE00		; vision.c ml_x / ml_y / ml_z
MLY	= 0xBE18
MLZ	= 0xBE30
YTAB	= 0xBC00		; vision.c ytab: y * sx (слова)
ZTAB	= 0xBCA0		; vision.c ztab: z * sx * sy (в той же странице #BC, что ytab — aidx)
VMCUR	= 0xBCD5		; vision.c vm_cur: что сейчас в Win3

;; void fu_run(void): сектор бойца F.ux..uz — линии таблицы по порядку (vision.c fov_unit). Записи:
;; x | (dz + 3) << 5, y, L (общее начало с прошлой записью), J (слово: следующая с меньшим L).
;; Общее начало с последней посчитанной линией — минимум L с неё; упёрлась она внутри него — прыжок J
_fu_run::
	ld	hl, #0
	ld	(FU_K), hl
	ld	hl, #0xFF00
	ld	(FU_TBK), hl
	xor	a, a
	ld	(FU_HAVE), a
	ld	(FU_PB), a
	ld	(FU_LEFF), a
fu_loop:
	ld	hl, (FU_K)
	ld	de, (FU_N)
	or	a, a
	sbc	hl, de
	ret	nc			; k >= n
	;; r = k - tbk; вне куска — следующий кусок: min(64, n - k) записей
	ld	hl, (FU_K)
	ld	de, (FU_TBK)
	or	a, a
	sbc	hl, de
	ld	a, h
	or	a, a
	jr	nz, fu_load
	ld	a, l
	cp	a, #64
	jr	c, fu_ent
fu_load:
	ld	hl, (FU_N)
	ld	de, (FU_K)
	or	a, a
	sbc	hl, de
	ld	a, h
	or	a, a
	jr	nz, 1$
	ld	a, l
	cp	a, #64
	jr	c, 2$
1$:	ld	a, #64
2$:	ld	l, a			; BC = записей * 5
	ld	h, #0
	ld	c, l
	ld	b, h
	add	hl, hl
	add	hl, hl
	add	hl, bc
	push	hl
	ld	hl, (FU_K)		; HL = off + k * 5
	ld	c, l
	ld	b, h
	add	hl, hl
	add	hl, hl
	add	hl, bc
	ld	bc, (FU_OFF)
	add	hl, bc
	pop	bc
	ld	de, #TB
	call	tcopy
	ld	hl, (FU_K)
	ld	(FU_TBK), hl
	ld	l, #0
fu_ent:	;; HL = TB + r * 5 (r < 64)
	ld	h, #0
	ld	c, l
	ld	b, h
	add	hl, hl
	add	hl, hl
	add	hl, bc
	ld	bc, #TB
	add	hl, bc
	inc	hl
	inc	hl			; L
	ld	a, (FU_LEFF)		; leff = min(leff, L)
	cp	a, (hl)
	jr	c, 3$
	ld	a, (hl)
	ld	(FU_LEFF), a
3$:	ld	a, (FU_HAVE)
	or	a, a
	jr	z, 4$
	ld	a, (FU_PB)
	ld	b, a
	ld	a, (FU_LEFF)
	cp	a, b
	jr	c, 4$
	jr	z, 4$
	inc	hl			; pb < leff: прошлая линия упёрлась в общем начале — прыжок
	ld	e, (hl)
	inc	hl
	ld	d, (hl)
	ld	(FU_K), de
	jp	fu_loop
4$:	ld	de, (FU_K)
	inc	de
	ld	(FU_K), de
	dec	hl
	dec	hl
	ld	a, (hl)			; x, dz
	and	a, #31
	ld	b, a
	ld	a, (hl)
	rlca
	rlca
	rlca
	and	a, #7
	sub	a, #3
	ld	d, a
	inc	hl
	ld	c, (hl)			; y
	ld	a, (FU_SW)
	or	a, a
	jr	z, 5$
	ld	a, b
	ld	b, c
	ld	c, a
5$:	ld	a, (FU_NX)
	or	a, a
	jr	z, 6$
	xor	a, a
	sub	a, b
	ld	b, a
6$:	ld	a, (FU_NY)
	or	a, a
	jr	z, 7$
	xor	a, a
	sub	a, c
	ld	c, a
7$:	;; цель вне поля — пропуск (в минимум L она уже вошла)
	ld	a, (FL_UX)
	add	a, b
	ld	hl, #HB_MSX
	cp	a, (hl)
	jp	nc, fu_loop
	ld	a, (FL_UY)
	add	a, c
	inc	hl
	cp	a, (hl)
	jp	nc, fu_loop
	ld	a, (FL_UZ)
	add	a, d
	inc	hl
	cp	a, (hl)
	jp	nc, fu_loop
	ld	a, b
	ld	(FL_X1), a
	ld	a, c
	ld	(FL_Y1), a
	ld	a, d
	ld	(FL_Z1), a
	ld	a, (FU_HAVE)
	or	a, a
	jr	z, 8$
	ld	a, (FU_LEFF)
8$:	ld	(FL_I), a
	call	_fl_line
	call	_fm_run
	ld	a, (FL_B)
	ld	(FU_PB), a
	ld	a, #1
	ld	(FU_HAVE), a
	ld	a, #0xFF
	ld	(FU_LEFF), a
	jp	fu_loop

;; Копия BC байт (не 0) таблицы линий с байта HL в DE — через Win3 (VMCUR), кусками до конца страницы
tcopy:
	ld	(TC_SRC), hl
	ld	(TC_DST), de
	ld	(TC_N), bc
1$:	ld	hl, (TC_SRC)
	ld	a, h
	rlca
	rlca
	and	a, #3
	add	a, #FOGTAB
	ld	hl, #VMCUR
	cp	a, (hl)
	jr	z, 2$
	ld	(hl), a
	call	_pg_map3
2$:	ld	hl, (TC_SRC)
	ld	a, h
	or	a, #0xC0
	ld	h, a			; HL — в окне
	ld	a, l			; DE = до конца окна
	cpl
	ld	e, a
	ld	a, h
	cpl
	ld	d, a
	inc	de
	push	hl
	ld	hl, (TC_N)
	or	a, a
	sbc	hl, de
	jr	c, 3$
	jr	z, 3$
	ld	(TC_N), hl		; не уместилось: до конца окна, остальное — со следующей страницы
	ld	b, d
	ld	c, e
	pop	hl
	ld	de, (TC_DST)
	ldir
	ld	(TC_DST), de
	ld	hl, (TC_SRC)
	ld	a, h
	and	a, #0xC0
	add	a, #0x40
	ld	h, a
	ld	l, #0
	ld	(TC_SRC), hl
	jr	1$
3$:	ld	bc, (TC_N)
	pop	hl
	ld	de, (TC_DST)
	ldir
	ret

;; void fl_line(void): линия к цели F.x1, y1, z1 с позиции F.i (позиции до неё — общие с прошлой
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
3$:	;; оси линии -> обычные: указатели на FL_CX / CY / CZ для главной оси и ведомых (обмен xy, потом xz)
	ld	hl, #FL_CX
	ld	(FX_PM), hl
	ld	hl, #FL_CY
	ld	(FX_PY), hl
	ld	hl, #FL_CZ
	ld	(FX_PZ), hl
	ld	a, (FL_SXY)
	or	a, a
	jr	z, 4$
	ld	hl, (FX_PM)
	ld	de, (FX_PY)
	ld	(FX_PM), de
	ld	(FX_PY), hl
4$:	ld	a, (FL_SXZ)
	or	a, a
	jr	z, 5$
	ld	hl, (FX_PM)
	ld	de, (FX_PZ)
	ld	(FX_PM), de
	ld	(FX_PZ), hl
5$:	;; главная ось — x или y: шаг без смены этажа возможен; его смещение номера клетки и направление —
	;; на всю линию: только по главной оси (0) и по главной с ведомой y (1). Ведомая y — всегда x или y
	xor	a, a
	ld	(FX_FAST), a
	ld	a, (FX_PM)
	cp	a, #<FL_CZ
	jr	z, 6$
	ld	a, #1
	ld	(FX_FAST), a
	ld	bc, #0
	ld	hl, (FX_PM)
	ld	a, (FL_STX)
	call	vstep
	ld	(FX_DM), de
	call	vdir
	ld	(FX_DIR0), a
	ld	hl, (FX_PY)
	ld	a, (FL_STY)
	call	vstep
	ld	(FX_DY), de
	call	vdir
	ld	(FX_DIR1), a
6$:	;; позиция 0, наклоны ddx / 2 (|d| <= 20 — хватает байта)
	xor	a, a
	ld	(FL_CX), a
	ld	(FL_CY), a
	ld	(FL_CZ), a
	ld	(FL_LX), a
	ld	(FL_LY), a
	ld	(FL_LZ), a
	ld	a, (FL_DDX)
	srl	a
	ld	(FL_DXY), a
	ld	(FL_DXZ), a
	ld	a, (FL_I)
	or	a, a
	jp	z, _fl_run
	;; Брезенхем вперёд на i позиций без проверок — путь общий с прошлой линией. До позиции i - 1 —
	;; в регистрах: наклоны и число шагов ведомых осей (IX — по y, IY — по z); позиция i - 1 по ним
	;; (главная — (i - 1) * stx, ведомые — число шагов * st), последний шаг — fl_adv (прошлая
	;; позиция и признаки шага ведомых осей для fl_run)
	dec	a
	jr	z, 9$
	ld	b, a
	push	ix
	push	iy
	ld	ix, #0
	ld	iy, #0
	ld	a, (FL_DDX)
	ld	c, a
	ld	a, (FL_DXY)
	ld	d, a
	ld	a, (FL_DXZ)
	ld	e, a
	ld	a, (FL_DDY)
	ld	h, a
	ld	a, (FL_DDZ)
	ld	l, a
7$:	ld	a, d
	sub	a, h
	jp	p, 10$
	add	a, c
	inc	ix
10$:	ld	d, a
	ld	a, e
	sub	a, l
	jp	p, 11$
	add	a, c
	inc	iy
11$:	ld	e, a
	djnz	7$
	ld	a, d
	ld	(FL_DXY), a
	ld	a, e
	ld	(FL_DXZ), a
	ld	(FX_NY), ix
	ld	(FX_NZ), iy
	pop	iy
	pop	ix
	ld	a, (FL_I)
	dec	a
	ld	hl, #FL_STX
	call	sgnmul
	ld	hl, (FX_PM)
	ld	(hl), a
	ld	a, (FX_NY)
	ld	hl, #FL_STY
	call	sgnmul
	ld	hl, (FX_PY)
	ld	(hl), a
	ld	a, (FX_NZ)
	ld	hl, #FL_STZ
	call	sgnmul
	ld	hl, (FX_PZ)
	ld	(hl), a
9$:	ld	hl, (FL_CX)
	ld	(FL_LX), hl
	ld	a, (FL_CZ)
	ld	(FL_LZ), a
	call	fl_adv
	jp	_fl_run

;; A = A со знаком шага (HL): -A при (HL) < 0
sgnmul:
	bit	7, (hl)
	ret	z
	neg
	ret

;; Шаг A (±1) по оси HL (указатель на FL_CX или FL_CY): B (dx) или C (dy) += A; DE — смещение номера
;; клетки (±1 или ±sx)
vstep:
	ld	e, a
	ld	a, l
	cp	a, #<FL_CX
	jr	nz, 1$
	ld	a, b
	add	a, e
	ld	b, a
	ld	a, e
	rla
	sbc	a, a
	ld	d, a
	ret
1$:	ld	a, c
	add	a, e
	ld	c, a
	bit	7, e
	ld	de, (HB_WSX)
	ret	z
	ld	de, (HB_NSX)
	ret

;; A = направление шага (B, C) = (dx, dy): dir_no[(dx + 1) * 3 + dy + 1]. Портит DE, HL
vdir:
	ld	a, b
	add	a, a
	add	a, b
	add	a, c
	add	a, #4
	ld	e, a
	ld	d, #0
	ld	hl, #dir_no
	add	hl, de
	ld	a, (hl)
	ret

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
	xor	a, a
	ld	(FL_SV), a
fl_loop:
	;; a = боец + c (c — позиция в обычных осях, её ведёт fl_adv)
	ld	hl, #FL_CX
	ld	a, (FL_UX)
	add	a, (hl)
	ld	(FL_AX), a
	inc	hl
	ld	a, (FL_UY)
	add	a, (hl)
	ld	(FL_AY), a
	inc	hl
	ld	a, (FL_UZ)
	add	a, (hl)
	ld	(FL_AZ), a
	ld	a, (FL_I)
	or	a, a
	jp	z, fl_pass		; позиция 0 — клетка бойца: блокировок нет
	;; пропуск объекта — позиции 0 и 1 (steps < 2)
	cp	a, #2
	ld	a, #0
	adc	a, a
	ld	(FL_SKIP), a
	ld	(HB_SKIP), a
	;; этаж сменился (главная ось — z или шаг ведомой z) — vblk, медленный путь на C
	ld	a, (FX_FAST)
	or	a, a
	jp	z, fl_slow
	ld	a, (FX_ZS)
	or	a, a
	jp	nz, fl_slow
	;; тот же этаж: vblk = 0, шаг решает hblk (hb_core) — 0 свободно, 1 упор, #FF большая стена.
	;; Номер и маска прошлой клетки — с прошлого шага (после медленного шага и в начале — заново)
	ld	a, (FL_SV)
	or	a, a
	jr	nz, 5$
	call	fl_b
	call	aidx
	ld	(HB_I), hl
	call	amki
	ld	(HB_S), a
	ld	a, #1
	ld	(FL_SV), a
5$:	;; номер клетки a = номер b + смещение шага, направление шага — заготовки линии (fl_line)
	ld	hl, (HB_I)
	ld	de, (FX_DM)
	add	hl, de
	ld	a, (FX_YS)
	or	a, a
	ld	a, (FX_DIR0)
	jr	z, 6$
	ld	de, (FX_DY)
	add	hl, de
	ld	a, (FX_DIR1)
6$:	ld	(HB_DIR), a
	ld	(FL_AI), hl
	call	amki
	ld	(HB_E), a
	call	hb_core
	or	a, a
	jr	z, 9$
	inc	a
	jr	nz, fl_stop		; 1 — упор: клетка не открывается
	call	fl_add			; большая стена: клетка в траектории, дальше нет
	jr	fl_stop
9$:	ld	hl, (FL_AI)		; клетка a — прошлая для следующего шага
	ld	(HB_I), hl
	ld	a, (HB_E)
	ld	(HB_S), a
	jr	fl_pass
fl_slow:
	call	fl_b
	xor	a, a
	ld	(FL_SV), a		; hb_fast портит HB_I и HB_S
	call	fl_zstep		; A: 0 — свободно, 1 — упор, 2 — большая стена: клетка видна, дальше нет
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
	ld	hl, (FL_CX)		; прошлая позиция = текущая (CX, CY словом)
	ld	(FL_LX), hl
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

;; Шаг Брезенхема: ведомые оси — drift -= dd; < 0 — шаг по ней (FX_YS / FX_ZS = 1) и drift += ddx;
;; главная — всегда. Позиция — в обычных осях (FL_CX..CZ) через указатели FX_PM / PY / PZ
fl_adv:
	ld	a, (FL_DDX)
	ld	c, a			; C = ddx
	ld	b, #0
	ld	a, (FL_DXY)
	ld	hl, #FL_DDY
	sub	a, (hl)
	jp	p, 1$
	add	a, c
	inc	b
	ld	(FL_DXY), a
	ld	a, (FL_STY)
	ld	hl, (FX_PY)
	add	a, (hl)
	ld	(hl), a
	jr	2$
1$:	ld	(FL_DXY), a
2$:	ld	a, b
	ld	(FX_YS), a
	ld	b, #0
	ld	a, (FL_DXZ)
	ld	hl, #FL_DDZ
	sub	a, (hl)
	jp	p, 3$
	add	a, c
	inc	b
	ld	(FL_DXZ), a
	ld	a, (FL_STZ)
	ld	hl, (FX_PZ)
	add	a, (hl)
	ld	(hl), a
	jr	4$
3$:	ld	(FL_DXZ), a
4$:	ld	a, b
	ld	(FX_ZS), a
	ld	a, (FL_STX)
	ld	hl, (FX_PM)
	add	a, (hl)
	ld	(hl), a
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

;; Шаг линии со сменой этажа b -> a (тот же разбор, что в vision.c los): tv = vblk(b, a),
;; r = hblk(b, a на этаже b, пропуск объекта — FL_SKIP). Слагаемые блокировки — 0 или >= 255, поэтому
;; «r + tv > 127» — «r или tv держит». A: 0 — свободно, 1 — упор, 2 — большая стена (клетка видна)
fl_zstep:
	call	vb_fast
	ld	(FL_TV), a
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
	inc	a
	jr	nz, 1$
	ld	a, (FL_TV)		; большая стена: если пол / потолок не держит — клетка видна, дальше нет
	or	a, a
	ld	a, #2
	ret	z
	xor	a, a			; иначе r = 0
	jr	2$
1$:	dec	a
2$:	ld	hl, #FL_TV
	or	a, (hl)
	ret	z
	ld	a, #1
	ret

;; TileEngine::verticalBlockage(b, a, DT_NONE) для az != bz: A = 1 — держит (> 127), 0 — нет.
;; Вниз: пол и объект своей клетки, при смещении — ещё hblk на своём этаже и пол с объектом соседней;
;; вверх: пол и объект клетки этажом выше над собой и над соседней (vision.c vblk)
vb_fast:
	ld	a, (FL_BX)
	ld	b, a
	ld	a, (FL_BY)
	ld	c, a
	ld	a, (FL_BZ)
	ld	d, a
	ld	a, (FL_AZ)
	cp	a, d
	jr	nc, vb_up
	call	amk3			; вниз: своя клетка
	call	vfo
	ret	nz
	call	vb_col
	ret	z			; та же колонна
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
	xor	a, a
	ld	(HB_SKIP), a
	call	_hb_fast
	dec	a			; 1 — держит (#FF, большая стена, даёт -1: сумму не поднимает)
	jr	z, 1$
	ld	a, (FL_AX)		; соседняя клетка на своём этаже
	ld	b, a
	ld	a, (FL_AY)
	ld	c, a
	ld	a, (FL_BZ)
	ld	d, a
	call	amk3
	jr	vfo
1$:	inc	a
	ret
vb_up:	inc	d			; вверх: над собой
	call	amk3
	call	vfo
	ret	nz
	call	vb_col
	ret	z
	ld	a, (FL_AX)		; над соседней
	ld	b, a
	ld	a, (FL_AY)
	ld	c, a
	ld	a, (FL_BZ)
	inc	a
	ld	d, a
	call	amk3
	; jr vfo

;; Пол (VM_F — 256) или объект (направление 8 / 9: держит при большой стене 0 или 1) маски A:
;; A = 1 и NZ — держит, A = 0 и Z — нет
vfo:
	ld	b, a
	and	a, #VM_F | VM_O
	ret	z
	and	a, #VM_F
	jr	nz, 1$
	ld	a, b
	and	a, #0xE0
	jr	z, 1$
	xor	a, a
	ret
1$:	ld	a, #1
	or	a, a
	ret

;; Z — a и b в одной колонне (x и y равны)
vb_col:
	ld	a, (FL_AX)
	ld	hl, #FL_BX
	cp	a, (hl)
	ret	nz
	ld	a, (FL_AY)
	inc	hl
	cp	a, (hl)
	ret

;; Маска клетки (B, C, D) — A, вне поля 0
amk3:
	call	ainm
	jr	nc, 1$
	call	aidx
	jp	amki
1$:	xor	a, a
	ret

;; Клетка b = боец + прошлая позиция: в FL_BX..BZ и B, C, D
fl_b:	ld	a, (FL_UX)
	ld	hl, #FL_LX
	add	a, (hl)
	ld	(FL_BX), a
	ld	b, a
	ld	a, (FL_UY)
	inc	hl
	add	a, (hl)
	ld	(FL_BY), a
	ld	c, a
	ld	a, (FL_UZ)
	inc	hl
	add	a, (hl)
	ld	(FL_BZ), a
	ld	d, a
	ret

;; void fm_run(void): клетки линии ml_* (F.nm) — в туман (Tile::setDiscovered, TileEngine.cpp:328-333):
;; клетка целиком и бит FOG_L, восточному соседу — западная стена, южному — северная. Клетка с FOG_L
;; уже отмечена линией вместе с соседями. Новое видимое — счёт (FOG_C) и прямоугольник (fog_bbn)
_fm_run::
	xor	a, a
	ld	(FL_K), a
fm_loop:
	ld	a, (FL_NM)
	ld	hl, #FL_K
	cp	a, (hl)
	ret	z
	ld	e, (hl)
	inc	(hl)
	ld	d, #0
	ld	hl, #MLX
	add	hl, de
	ld	b, (hl)
	ld	hl, #MLY
	add	hl, de
	ld	c, (hl)
	ld	hl, #MLZ
	add	hl, de
	ld	d, (hl)
	ld	a, b
	ld	(FL_BBX), a
	ld	a, c
	ld	(FL_BBY), a
	ld	a, d
	ld	(FL_BBZ), a
	call	aidx
	ld	(FL_FI), hl
	call	afog
	bit	3, a			; FOG_L
	jr	nz, fm_loop
	ld	e, #FOG_W | FOG_N | FOG_C | FOG_L
	call	afset
	ld	a, (FL_BBX)		; восточный сосед — западная стена
	inc	a
	ld	hl, #HB_MSX
	cp	a, (hl)
	jr	nc, 1$
	ld	(FL_BBX), a
	ld	hl, (FL_FI)
	inc	hl
	call	afog
	ld	e, #FOG_W
	call	afset
	ld	hl, #FL_BBX
	dec	(hl)
1$:	ld	a, (FL_BBY)		; южный — северная
	inc	a
	ld	hl, #HB_MSY
	cp	a, (hl)
	jr	nc, fm_loop
	ld	(FL_BBY), a
	ld	hl, (FL_FI)
	ld	de, (HB_WSX)
	add	hl, de
	call	afog
	ld	e, #FOG_N
	call	afset
	jr	fm_loop

;; Байт тумана по адресу HL (A — прежний) | E (vision.c fset): новое — запись, и если клетка не была
;; видна — счёт (с FOG_C) и прямоугольник перерисовки
afset:
	ld	d, a
	or	a, e
	cp	a, d
	ret	z
	ld	(hl), a
	bit	2, d			; уже видна (FOG_C) — стены на картинку не влияют
	ret	nz
	bit	2, e
	jp	z, _fog_bbn
	ld	hl, (FOGN)
	inc	hl
	ld	(FOGN), hl
	jp	_fog_bbn

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
HB_B	= HB + 15
HB_I	= HB + 16		; слово: номер клетки s (соседи t — смещением от него)
HB_NSX	= HB + 18		; слово: -sx
HB_WSX	= HB + 20		; слово: sx

VM_W	= 1
VM_N	= 2
VM_O	= 4
VM_F	= 8

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
	;; номер и маска своей клетки, маска соседней
	ld	a, (HB_Z)
	ld	d, a
	ld	a, (HB_SX)
	ld	b, a
	ld	a, (HB_SY)
	ld	c, a
	call	aidx
	ld	(HB_I), hl
	call	amki
	ld	(HB_S), a
	ld	a, (HB_Z)
	ld	d, a
	ld	a, (HB_EX)
	ld	b, a
	ld	a, (HB_EY)
	ld	c, a
	call	aidx
	call	amki
	ld	(HB_E), a
;; Ядро hblk: HB_DIR (не 8), HB_S, HB_E, HB_I, HB_SKIP заданы; обе клетки на поле — тогда и
;; клетки-углы t диагонального шага на поле (их x и y — от s и e)
hb_core:
	;; прямой шаг, объектов нет ни в одной из клеток — держит только стена: 0 — NB(s), 2 — WB(e),
	;; 4 — NB(e), 6 — WB(s) (объекты без VM_O обзор не держат — aobj, большая стена тоже)
	ld	a, (HB_S)
	ld	hl, #HB_E
	or	a, (hl)
	and	a, #VM_O
	jr	nz, hb_full
	ld	a, (HB_DIR)
	rrca
	jr	c, hb_full
	ld	b, #VM_N		; A = dir / 2: чётное — северная стена, нечётное — западная
	bit	0, a
	jr	z, 1$
	ld	b, #VM_W
1$:	dec	a			; 1 и 2 (восток, юг) — стена соседней клетки
	cp	a, #2
	ld	a, (HB_E)
	jr	c, 2$
	ld	a, (HB_S)
2$:	and	a, b
	ret	z
	ld	a, #1
	ret
hb_full:
	xor	a, a
	ld	(HB_B), a
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
hc1:	call	at_n
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
1$:	call	at_e
	and	a, #VM_N | VM_W
	jp	nz, hb_set
	ld	e, #7
	ld	l, #2
	call	at_obj
	jp	hb_set

;; Юго-восток: t = (sx, sy+1): NB(t) + WB(e) + (t.bw != 3 ? obj(t, 1) : 0); если не 0 —
;; t = (sx+1, sy): WB(t) + NB(e) + (t.bw != 3 ? obj(t, 5) : 0)
hc3:	call	at_s
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
1$:	call	at_e
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
hc5:	call	at_s
	and	a, #VM_N | VM_W
	jr	nz, 1$
	ld	e, #7
	ld	l, #2
	call	at_obj
	or	a, a
	jp	z, hb_set
1$:	call	at_w
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
hc7:	call	at_n
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
1$:	call	at_w
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

;; T = маска соседа s: к северу (y - 1), югу, востоку (x + 1), западу; A = она же
at_n:	ld	de, (HB_NSX)
	jr	at_off
at_s:	ld	de, (HB_WSX)
	jr	at_off
at_e:	ld	de, #1
	jr	at_off
at_w:	ld	de, #-1
at_off:	ld	hl, (HB_I)
	add	hl, de
	call	amki
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

;; Номер клетки (B, C, D) на поле — HL = ztab[z] + ytab[y] + x (таблицы — в одной странице #BC00).
;; Портит A, DE
aidx:
	ld	a, d
	add	a, a
	add	a, #<ZTAB
	ld	l, a
	ld	h, #>ZTAB
	ld	e, (hl)
	inc	l
	ld	d, (hl)
	ld	a, c
	add	a, a			; ytab — с #BC00, h тот же
	ld	l, a
	ld	a, (hl)
	inc	l
	ld	h, (hl)
	ld	l, a
	add	hl, de
	ld	e, b
	ld	d, #0
	add	hl, de
	ret

;; Маска клетки номер HL — A (страница маски — в Win3). Портит DE, HL; BC сохраняется
amki:
	ld	a, h
	rlca
	rlca
	and	a, #3
	ex	de, hl
	ld	hl, #HB_VMP
	add	a, (hl)
	;; A — страница, DE — номер: подключить и прочесть
amk_c:	ld	hl, #VMCUR
	cp	a, (hl)
	jr	z, 1$
	ld	(hl), a
	push	de
	push	bc
	call	_pg_map3
	pop	bc
	pop	de
1$:	ld	a, d
	or	a, #0xC0
	ld	h, a
	ld	l, e
	ld	a, (hl)
	ret

;; Байт тумана клетки номер HL — A, HL — его адрес (страница тумана — в Win3). Портит DE; BC сохраняется
afog:
	ld	a, h
	rlca
	rlca
	and	a, #3
	ex	de, hl
	ld	hl, #FOGPG
	add	a, (hl)
	jr	amk_c
