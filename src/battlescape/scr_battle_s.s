;; Банк 28: команды дисплей-листа боя (scr_battle.c, 17 §2) — кадр карты или бойца в 8 байт
;; регистров DMA (BLT1 | D_ALGN | ASZ): источник подряд (строка — ширина кадра), приёмник — кольцо
;; 512 x 256 в строках холста BUF_Y.. (страница строки — SCREEN_PAGE + b >> 5, смещение — (b & 31) << 9).
;; Запись кадра (tile_tab, unit_tab, TE_SIZE = 8): [0] страница, [1..2] смещение, [3] DMALEN,
;; [4] высота, [5] dx, [6] dy, [7] ширина. Был C: на ассемблере вчетверо короче — банк 28 не
;; вмещал ходьбу бойцов — и быстрее (обвязка на C — узкое место рендера, 17 §0).
;;
;; dl_blit — кадр целиком (вне буфера по Y — строки отсекаются); dl_blit_clip — кадр в
;; прямоугольнике (маски Map::drawUnit для идущего бойца). Кадр внутри буфера и не на стыке
;; кольца — одна команда, иначе команда на строку (на стыке — две: конец строки и начало).

	.module scr_battle_s
	.optsdcc -mz80 sdcccall(1)

	.globl	_dl_blit, _dl_blit_clip, _dl_run, _pg_map3
	.globl	_dl_n, _dl_page, _dl_over, _tgt_d

BUF_Y	= 200				; строка холста, с которой лежит буфер карты (scr_battle.c)
SCREEN_PAGE = 0x10
DR	= 0xB300			; рабочие переменные — начало памяти банка (build.ps1 ram), до tile_tab
DR_E	= DR + 0			; запись кадра
DR_BX	= DR + 2			; столбец кольца первой точки (0..511, чётный)
DR_BY	= DR + 4			; строка буфера первой строки (0..255)
DR_C0	= DR + 5			; первый столбец кадра
DR_W	= DR + 6			; ширина (чётная)
DR_R0	= DR + 7			; первая строка кадра
DR_N	= DR + 8			; строк
DR_W1	= DR + 9			; ширина до конца строки буфера
DR_W2	= DR + 10			;   и остаток с её начала
DR_SO	= DR + 11			; источник: смещение
DR_SP	= DR + 13			;   и страница
DR_STEP	= DR + 14			; шаг строки источника (ширина кадра)
CL_X	= DR + 16			; dl_blit_clip: аргументы (int16)
CL_Y	= DR + 18
CL_CX0	= DR + 20
CL_CX1	= DR + 22
CL_CY0	= DR + 24
CL_CY1	= DR + 26
CL_FX	= DR + 28			;   левый верхний угол кадра
CL_FY	= DR + 30
CL_TOP	= DR + 32			;   первая строка буфера
CL_LEFT	= DR + 34			;   первый столбец (мировой)
DR_TD	= DR + 36			; dl_run: сдвиг страниц целевого холста
DR_PG	= DR + 37			; emit: страница строки приёмника
BIAS	= 1024
DMA_RUN	= 0x99				; DMA_BLT1 | DMA_D_ALGN | DMA_ASZ (tsconf.h)

	.area	_BANK28

;; void dl_run(void) — прогон списка в холст _tgt_d (страницы приёмника в списке — основного
;; холста): на команду — ожидание DMA (DMAStatus, бит 7) и восемь регистров, порты #1A..#1F
;; подряд. Список — две страницы подряд: на 2048-й команде адрес переходит через #FFFF.
_dl_run::
	ld	a, (_tgt_d)
	ld	(DR_TD), a
	ld	a, (_dl_page)
	call	_pg_map3			; A — прежняя страница Win3
	push	af
	ld	de, (_dl_n)
	ld	hl, #0xC000
1$:	ld	a, d
	or	a, e
	jr	z, 9$
	ld	bc, #0x27AF
2$:	in	a, (c)
	add	a, a
	jr	c, 2$				; DMA ещё работает
	ld	b, #0x1A
	ld	a, (hl)				; SAL, SAH, SAX, DAL, DAH
	out	(c), a
	inc	hl
	inc	b
	ld	a, (hl)
	out	(c), a
	inc	hl
	inc	b
	ld	a, (hl)
	out	(c), a
	inc	hl
	inc	b
	ld	a, (hl)
	out	(c), a
	inc	hl
	inc	b
	ld	a, (hl)
	out	(c), a
	inc	hl
	inc	b
	ld	a, (DR_TD)			; DAX — в целевом холсте
	add	a, (hl)
	out	(c), a
	inc	hl
	ld	b, #0x26			; DMALEN
	ld	a, (hl)
	out	(c), a
	inc	hl
	ld	b, #0x28			; DMANUM
	ld	a, (hl)
	out	(c), a
	inc	hl
	ld	b, #0x27			; DMACTRL — запуск
	ld	a, #DMA_RUN
	out	(c), a
	dec	de
	ld	a, h
	or	a, l
	jr	nz, 1$
	push	de				; вторая страница списка
	ld	a, (_dl_page)
	inc	a
	call	_pg_map3
	pop	de
	ld	hl, #0xC000
	jr	1$
9$:	pop	af
	jp	_pg_map3

;; Место под команду: HL — адрес в подключённой странице списка; H = 0 — список полон
;; (dl_over++). На 2048-й записи подключается вторая страница (DL_PAGE_N). Портит A, DE.
dl_put:
	ld	hl, (_dl_n)
	ld	a, h
	cp	a, #0x10			; DL_MAX = 4096
	jr	nc, 2$
	cp	a, #0x08			; DL_PAGE_N = #0800 — перевалили во вторую страницу
	jr	nz, 1$
	ld	a, l
	or	a, a
	jr	nz, 1$
	push	hl
	ld	a, (_dl_page)
	inc	a
	call	_pg_map3
	pop	hl
1$:	ld	e, l
	ld	d, h
	inc	de
	ld	(_dl_n), de
	ld	a, h
	and	a, #0x07
	ld	h, a
	add	hl, hl
	add	hl, hl
	add	hl, hl
	ld	a, h
	or	a, #0xC0			; #C000 + (n & #7FF) * 8
	ld	h, a
	ret
2$:	ld	hl, (_dl_over)
	inc	hl
	ld	(_dl_over), hl
	ld	hl, #0
	ret

;; Команда: источник DR_SO / DR_SP, приёмник — строка буфера DR_BY, столбец BC; DMALEN = D,
;; DMANUM = E
emit:
	push	de
	push	bc
	call	dl_put
	pop	bc
	pop	de
	ld	a, h
	or	a, a
	ret	z				; список полон
	ld	a, (DR_SO)
	ld	(hl), a
	inc	hl
	ld	a, (DR_SO + 1)
	ld	(hl), a
	inc	hl
	ld	a, (DR_SP)
	ld	(hl), a
	inc	hl
	push	de
	push	hl
	ld	a, (DR_BY)
	ld	l, a
	ld	h, #0
	ld	de, #BUF_Y
	add	hl, de				; строка холста b = by + BUF_Y (0..455)
	ld	a, h
	add	a, a
	add	a, a
	add	a, a
	ld	d, a
	ld	a, l
	rlca
	rlca
	rlca
	and	a, #0x07
	or	a, d
	add	a, #SCREEN_PAGE
	ld	(DR_PG), a			; страница — SCREEN_PAGE + (b >> 5)
	ld	a, l
	and	a, #0x1F
	add	a, a
	ld	h, a
	ld	l, #0				; смещение строки — (b & 31) << 9
	add	hl, bc				; + столбец
	ex	de, hl
	pop	hl
	ld	(hl), e
	inc	hl
	ld	(hl), d
	inc	hl
	ld	a, (DR_PG)
	ld	(hl), a
	inc	hl
	pop	de
	ld	(hl), d
	inc	hl
	ld	(hl), e
	ret

;; HL — смещение источника: дошло до #4000 — следующая страница
norm:
	bit	6, h
	ret	z
	res	6, h
	push	hl
	ld	hl, #DR_SP
	inc	(hl)
	pop	hl
	ret

;; Строки кадра: DR_N строк с DR_R0, столбцы DR_C0 .. +DR_W; первая точка — (DR_BX, DR_BY)
rows:
	ld	hl, (DR_E)
	ld	a, (hl)
	ld	(DR_SP), a
	inc	hl
	ld	e, (hl)
	inc	hl
	ld	d, (hl)				; DE = смещение источника кадра
	ld	bc, #5
	add	hl, bc
	ld	a, (hl)
	ld	(DR_STEP), a
	ld	c, a
	ld	b, #0
	ex	de, hl				; HL = смещение, BC = шаг
	ld	a, (DR_R0)
	or	a, a
	jr	z, 2$
1$:	add	hl, bc				; + r0 строк (r0 < 40)
	dec	a
	jr	nz, 1$
2$:	ld	a, (DR_C0)
	ld	e, a
	ld	d, #0
	add	hl, de				; + c0 столбцов
	call	norm
	ld	(DR_SO), hl
	ld	hl, #512			; до конца строки буфера: 512 - bx
	ld	de, (DR_BX)
	or	a, a
	sbc	hl, de
	ld	a, (DR_W)
	ld	e, a
	ld	d, #0
	push	hl
	or	a, a
	sbc	hl, de
	pop	hl
	jr	c, 3$				; не влезает — w1 = 512 - bx (меньше w)
	ld	l, e				; влезает целиком
3$:	ld	a, l
	ld	(DR_W1), a
	ld	b, a
	ld	a, (DR_W)
	sub	a, b
	ld	(DR_W2), a
	jr	nz, 4$
	ld	a, (DR_STEP)			; полные строки без стыка — одной командой
	ld	b, a
	ld	a, (DR_W)
	cp	a, b
	jr	nz, 4$
	ld	hl, (DR_E)
	inc	hl
	inc	hl
	inc	hl
	ld	d, (hl)				; DMALEN кадра
	ld	a, (DR_N)
	dec	a
	ld	e, a
	ld	bc, (DR_BX)
	jp	emit
4$:	ld	a, (DR_N)
	or	a, a
	ret	z
5$:	push	af
	ld	a, (DR_W1)
	or	a, a
	jr	z, 6$
	srl	a
	dec	a
	ld	d, a
	ld	e, #0
	ld	bc, (DR_BX)
	call	emit
6$:	ld	a, (DR_W2)
	or	a, a
	jr	z, 7$
	ld	hl, (DR_SO)			; остаток строки — с начала строки буфера
	push	hl
	ld	a, (DR_SP)
	push	af
	ld	a, (DR_W1)
	ld	e, a
	ld	d, #0
	add	hl, de
	call	norm
	ld	(DR_SO), hl
	ld	a, (DR_W2)
	srl	a
	dec	a
	ld	d, a
	ld	e, #0
	ld	bc, #0
	call	emit
	pop	af
	ld	(DR_SP), a
	pop	hl
	ld	(DR_SO), hl
7$:	ld	hl, (DR_SO)			; следующая строка
	ld	a, (DR_STEP)
	ld	e, a
	ld	d, #0
	add	hl, de
	call	norm
	ld	(DR_SO), hl
	ld	hl, #DR_BY
	inc	(hl)
	pop	af
	dec	a
	jr	nz, 5$
	ret

;; void dl_blit(const uint8_t *e /*HL*/, int16_t x /*DE*/, int16_t y /*стек*/) — x мировая
;; (колонка кольца — (x + BIAS) & 511), y — строка буфера угла клетки
_dl_blit::
	pop	bc
	pop	iy				; y
	push	bc
	ld	(DR_E), hl
	ld	bc, #7
	add	hl, bc
	ld	a, (hl)				; ширина
	or	a, a
	ret	z				; пустой кадр
	ld	(DR_W), a
	dec	hl
	dec	hl
	ld	c, (hl)				; dx
	ld	b, #0
	ex	de, hl				; HL = x, DE = &e[5]
	add	hl, bc
	ld	bc, #BIAS
	add	hl, bc
	ld	a, h
	and	a, #1
	ld	h, a				; & 511
	bit	0, l
	ret	nz				; нечётный столбец — DMA адресует словами
	ld	(DR_BX), hl
	ex	de, hl
	inc	hl
	ld	c, (hl)				; dy
	ld	b, #0
	push	iy
	pop	hl
	add	hl, bc				; by = y + dy
	ld	de, (DR_E)
	inc	de
	inc	de
	inc	de
	inc	de
	ld	a, (de)
	ld	c, a				; C = высота
	ld	a, h
	or	a, a
	jr	z, 2$
	inc	a
	ret	nz				; выше -256 или ниже 255
	ld	a, l				; by < 0: виден, если by > -h
	add	a, c
	ret	nc				; by + h <= 0
	ret	z
	ld	(DR_N), a			; строк = by + h, с r0 = -by
	xor	a, a
	sub	a, l
	ld	(DR_R0), a
	xor	a, a
	ld	(DR_BY), a
	ld	(DR_C0), a
	jp	rows
2$:	ld	a, l				; 0 <= by <= 255
	ld	(DR_BY), a
	add	a, c
	jr	nc, 3$
	jr	nz, 4$				; by + h > 256 — низ отсечь
3$:	ld	hl, (DR_BX)			; и по X не на стыке кольца: bx + w <= 512
	ld	a, (DR_W)
	ld	e, a
	ld	d, #0
	add	hl, de
	ld	a, h
	cp	a, #2
	jr	c, 5$
	jr	nz, 4$
	ld	a, l
	or	a, a
	jr	nz, 4$
5$:	ld	hl, (DR_E)			; быстрый путь: кадр целиком — одна команда
	ld	a, (hl)
	ld	(DR_SP), a
	inc	hl
	ld	a, (hl)
	ld	(DR_SO), a
	inc	hl
	ld	a, (hl)
	ld	(DR_SO + 1), a
	inc	hl
	ld	d, (hl)				; DMALEN
	ld	a, c
	dec	a
	ld	e, a				; строк - 1
	ld	bc, (DR_BX)
	jp	emit
4$:	xor	a, a				; низ за буфером или стык кольца — по строкам
	ld	(DR_R0), a
	ld	(DR_C0), a
	ld	a, (DR_BY)
	ld	b, a
	xor	a, a
	sub	a, b				; 256 - by (0 — значит 256)
	jr	z, 6$
	cp	a, c
	jr	c, 7$
6$:	ld	a, c				; строк — min(h, 256 - by)
7$:	ld	(DR_N), a
	jp	rows

;; HL = max(HL, DE) со знаком (DE не меняется)
smax:
	call	scmp
	ret	nc
	ex	de, hl
	ret

;; HL = min(HL, DE) со знаком
smin:
	call	scmp
	ret	c
	ex	de, hl
	ret

;; Сравнение со знаком: перенос — HL < DE. HL и DE не меняются
scmp:
	push	hl
	push	de
	ld	a, h
	xor	a, #0x80
	ld	h, a
	ld	a, d
	xor	a, #0x80
	ld	d, a
	or	a, a
	sbc	hl, de
	pop	de
	pop	hl
	ret

;; void dl_blit_clip(const uint8_t *e /*HL*/, int16_t x /*DE*/, int16_t y, int16_t cx0, int16_t cx1,
;;                   int16_t cy0, int16_t cy1 /*стек*/) — кадр, обрезанный прямоугольником
;; [cx0, cx1) x [cy0, cy1) (мировые X, строки буфера; края по X чётные)
_dl_blit_clip::
	pop	iy				; адрес возврата
	ld	(DR_E), hl
	ld	(CL_X), de
	pop	hl
	ld	(CL_Y), hl
	pop	hl
	ld	(CL_CX0), hl
	pop	hl
	ld	(CL_CX1), hl
	pop	hl
	ld	(CL_CY0), hl
	pop	hl
	ld	(CL_CY1), hl
	push	iy
	ld	hl, (DR_E)
	ld	bc, #4
	add	hl, bc
	ld	a, (hl)				; высота
	ld	(DR_N), a
	inc	hl
	ld	e, (hl)				; dx
	inc	hl
	ld	c, (hl)				; dy
	inc	hl
	ld	a, (hl)				; ширина
	or	a, a
	ret	z
	ld	(DR_STEP), a
	ld	d, #0
	ld	hl, (CL_X)
	add	hl, de
	ld	(CL_FX), hl			; fx = x + dx
	ld	hl, (CL_Y)
	ld	b, #0
	add	hl, bc
	ld	(CL_FY), hl			; fy = y + dy
	; строки: top = max(cy0, fy, 0), bot = min(cy1, fy + h, 256)
	ld	de, (CL_CY0)
	call	smax
	ld	de, #0
	call	smax
	ld	(CL_TOP), hl
	ld	hl, (CL_FY)
	ld	a, (DR_N)
	ld	e, a
	ld	d, #0
	add	hl, de
	ld	de, (CL_CY1)
	call	smin
	ld	de, #256
	call	smin				; HL = bot
	ld	de, (CL_TOP)
	call	scmp
	ret	c				; bot < top
	or	a, a
	sbc	hl, de
	ret	z				; bot = top
	ld	a, l
	ld	(DR_N), a			; строк
	ld	a, e
	ld	(DR_BY), a			; первая строка буфера = top
	ex	de, hl
	ld	de, (CL_FY)
	or	a, a
	sbc	hl, de
	ld	a, l
	ld	(DR_R0), a			; r0 = top - fy
	; столбцы: left = max(cx0, fx), right = min(cx1, fx + w)
	ld	hl, (CL_FX)
	ld	de, (CL_CX0)
	call	smax
	ld	(CL_LEFT), hl
	ld	hl, (CL_FX)
	ld	a, (DR_STEP)
	ld	e, a
	ld	d, #0
	add	hl, de
	ld	de, (CL_CX1)
	call	smin				; HL = right
	ld	de, (CL_LEFT)
	call	scmp
	ret	c
	or	a, a
	sbc	hl, de
	ret	z
	ld	a, l
	ld	(DR_W), a			; ширина
	ex	de, hl
	push	hl
	ld	de, (CL_FX)
	or	a, a
	sbc	hl, de
	ld	a, l
	ld	(DR_C0), a			; c0 = left - fx
	pop	hl
	ld	bc, #BIAS
	add	hl, bc
	ld	a, h
	and	a, #1
	ld	h, a
	ld	(DR_BX), hl			; столбец кольца
	jp	rows
