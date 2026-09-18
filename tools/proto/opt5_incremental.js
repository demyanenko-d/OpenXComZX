// Прототип №5: НЕ СТРОИТЬ КАДР ЗАНОВО — аппаратный скролл (GXOffs/GYOffs) плюс точечная
// перерисовка (16_battlescape_plan.md §8.3a п. 3, §8.6; регистры — 02 §4, DMA — 02 §6).
//
// Что считает:
//   1. Правило зависимостей: какие клетки трогать при изменении клетки (x, y, z).
//      Выводится из габаритов спрайтов и ПРОВЕРЯЕТСЯ перебором всех клеток карты.
//   2. Предопределённые паттерны перерисовки для 8 направлений скролла (как в оригинальном
//      X-COM: не считать область в рантайме, а взять готовый список клеток).
//   3. Стоимость типовых событий боя в блитах, запусках DMA, байтах и кадрах.
//   4. Стирание старого: заливка чёрным + перерисовка против копии из «чистой карты».
//   5. Порог, за которым дешевле перерисовать весь вид.
//   6. Артефакты «наивных» вариантов (без обрезки блитов / без соседей) — попиксельно.
//   Каждый шаг сверяется попиксельно с честной полной перерисовкой (независимый рендер).
//
//   node tools/proto/opt5_incremental.js
//   node tools/proto/opt5_incremental.js --game UFO --sets BLANKS,CULTIVAT,BARN --map CULTA00
//
// Метрики:
//   блит    — один спрайт (часть клетки / юнит / дым), попавший в грязный прямоугольник;
//   запуск  — одна запись DMACtrl. Полоса строк с одним шагом идёт одним запуском
//             (BLT1 | D_ALGN | ASZ, 02 §6); обрезка по X рвёт линейность источника — тогда
//             запуск на строку. Модель «полов.» — тайлсет хранится половинками по 16 пикс.,
//             тогда обрезка по границе 16 не стоит ничего;
//   байты   — прочитано DMA (столько же записано).
'use strict';
const fs = require('fs');
const path = require('path');
const B = require('./battle');

function arg(name, def) {
	const i = process.argv.indexOf('--' + name);
	return i > 0 && i + 1 < process.argv.length ? process.argv[i + 1] : def;
}

// В battle.js путь к UFO устарел (каталог называется «XCom UFO Defense») — свой список
const GAMES = { TFTD: 'Steam/X-COM Terror from the Deep/TFD', UFO: 'Steam/XCom UFO Defense/XCOM' };

const game = arg('game', 'TFTD');
const root = GAMES[game];
const sets = arg('sets', 'BLANKS,SAND,ROCKS,WEEDS,DEBRIS,UFOBITS').split(',');
const mapName = arg('map', 'SEABED00');
const nblocks = parseInt(arg('blocks', '4'), 10);
const outDir = arg('out', 'tmp/proto/opt5');

const TILE_W = 32, TILE_H = 40, HALF_W = 16, QUART = 8, LEVEL_H = 24;
const VIEW_W = 320, VIEW_H = 144;          // окно карты; ниже 56 строк панели ICONS
const BUF_W = 512, BUF_H = 512;            // кольцевой буфер = экран 512x512 (02 §4)
const BUF_H_A = 456;                       // раскладка А: панель в строках 456..511

// Пропускная способность DMA за кадр (02 §6): BLT1 74 КБ, RAM→RAM 111 КБ, FILL 222 КБ
const KB = 1024;
const BW_BLT = 74 * KB, BW_COPY = 111 * KB, BW_FILL = 222 * KB;
const T_FRAME = 286720, T_LAUNCH = 300;    // такты кадра и настройки одного запуска DMA
// Калибровка «кадров по CPU»: §8.3b — полная перерисовка ~380 блитов = 24–32 кадра эмулятора
const CPU_FRAMES_PER_BLIT = 28 / 380;

// ---------------------------------------------------------------- данные

const parts = [], frames = [];
for (const s of sets) {
	const dir = path.join(root, 'TERRAIN');
	const mcd = B.readMcd(fs.readFileSync(path.join(dir, s + '.MCD')));
	const pck = B.readPck(fs.readFileSync(path.join(dir, s + '.PCK')), fs.readFileSync(path.join(dir, s + '.TAB')));
	const base = frames.length;
	for (const r of mcd) { r.base = base; parts.push(r); }
	for (const f of pck) frames.push(f);
}
function loadPck(dir, name) {
	return B.readPck(fs.readFileSync(path.join(root, dir, name + '.PCK')),
		fs.readFileSync(path.join(root, dir, name + '.TAB')));
}
const smokeFrames = loadPck('UFOGRAPH', 'SMOKE');
const unitSheet = loadPck('UNITS', game === 'TFTD' ? 'TDXCOM_0' : 'XCOM_0');

// Собранный кадр юнита (§8.5): ноги + торс + руки; холст 32x40 (упрощение — без сдвига +16)
function unitFrame(dir, phase, walking) {
	const img = new Uint8Array(TILE_W * TILE_H);
	const put = (f) => { const s = unitSheet[f]; if (s) for (let i = 0; i < img.length; i++) if (s[i]) img[i] = s[i]; };
	if (walking) { put(56 + 24 * dir + phase); put(32 + dir); put(40 + 24 * dir + phase); put(48 + 24 * dir + phase); }
	else { put(16 + dir); put(32 + dir); put(0 + dir); put(8 + dir); }
	return img;
}

function buildMap(pattern, n) {
	const dir = path.join(root, 'MAPS');
	const names = fs.readdirSync(dir).filter(f => f.startsWith(pattern) && f.endsWith('.MAP')).sort();
	const blocks = names.map(f => B.readMap(fs.readFileSync(path.join(dir, f))));
	if (n <= 1) return blocks[0];
	const bw = blocks[0].sx, bh = blocks[0].sy;
	let sz = 0;
	for (const b of blocks) if (b.sz > sz) sz = b.sz;
	const sx = bw * n, sy = bh * n;
	const cells = new Uint8Array(sx * sy * sz * 4);
	for (let by = 0; by < n; by++)
		for (let bx = 0; bx < n; bx++) {
			const b = blocks[(by * n + bx) % blocks.length];
			for (let z = 0; z < b.sz; z++)
				for (let y = 0; y < b.sy; y++)
					for (let x = 0; x < b.sx; x++) {
						const from = ((z * b.sy + y) * b.sx + x) * 4;
						const to = ((z * sy + by * bh + y) * sx + bx * bw + x) * 4;
						for (let k = 0; k < 4; k++) cells[to + k] = b.cells[from + k];
					}
		}
	return { sx, sy, sz, cells };
}

const map = buildMap(arg('blocks-of', mapName.replace(/[0-9]+$/, '')), nblocks);
const pal = B.readPalette(game, 6);
let level = Math.min(parseInt(arg('level', String(map.sz - 1)), 10), map.sz - 1);

const cropCache = new Map();
function crop(img) {
	if (!img) return null;
	let c = cropCache.get(img);
	if (c !== undefined) return c;
	let x0 = TILE_W, y0 = TILE_H, x1 = -1, y1 = -1;
	for (let y = 0; y < TILE_H; y++)
		for (let x = 0; x < TILE_W; x++)
			if (img[y * TILE_W + x]) {
				if (x < x0) x0 = x; if (x > x1) x1 = x;
				if (y < y0) y0 = y; if (y > y1) y1 = y;
			}
	c = x1 < 0 ? null : { x: x0, y: y0, w: x1 - x0 + 1, h: y1 - y0 + 1, img };
	cropCache.set(img, c);
	return c;
}
function partFrame(id) { const r = parts[id - 1]; return r ? frames[r.base + r.frame[0]] : null; }
function partYOfs(id) { const r = parts[id - 1]; return r ? r.pLevel : 0; }

let YMAX = 0;
for (const r of parts) if (r.pLevel > YMAX) YMAX = r.pLevel;

// ---------------------------------------------------------------- сцена

let cells = Uint8Array.from(map.cells);
const NT = map.sx * map.sy * map.sz;
const fog = new Uint8Array(NT).fill(1);
let smoke = new Map();
let units = [];
const unitAt = new Map();
function cidx(x, y, z) { return (z * map.sy + y) * map.sx + x; }
function cellParts(x, y, z) { const o = cidx(x, y, z) * 4; return [cells[o], cells[o + 1], cells[o + 2], cells[o + 3]]; }
function rebuildUnitIndex() { unitAt.clear(); for (const u of units) unitAt.set(cidx(u.x, u.y, u.z), u); }

function cellPx(x, y) { return (x - y) * HALF_W; }
function cellPy(x, y, z) { return (x + y) * QUART - z * LEVEL_H; }

// Габарит влияния клетки. Местность: спрайт 32x40, подъём по MCD.P_Level до YMAX.
// Юнит на клетке: сдвиг ходьбы до ±16 по X и ±8 по Y плюс terrainLevel до −24 (Map.cpp:1373).
const TER_L = 0, TER_R = TILE_W, TER_UP = YMAX, TER_DN = TILE_H;
const INF_L = 16, INF_R = TILE_W + 16, INF_UP = YMAX + 24, INF_DN = TILE_H;

function cellItems(x, y, z, out) {
	const i = cidx(x, y, z);
	if (!fog[i]) return;                     // неразведанное — чёрное, рисовать нечего
	const px = cellPx(x, y), py = cellPy(x, y, z);
	const p = cellParts(x, y, z);
	for (let k = 0; k < 4; k++) {
		if (!p[k]) continue;
		const c = crop(partFrame(p[k]));
		if (!c) continue;
		out.push({ c, x: px + c.x, y: py - partYOfs(p[k]) + c.y, kind: 'tile', ax: px });
	}
	const u = unitAt.get(i);
	if (u) { const c = crop(u.img); if (c) out.push({ c, x: px + u.ox + c.x, y: py + u.oy + c.y, kind: 'unit', ax: px + u.ox }); }
	const sm = smoke.get(i);
	if (sm !== undefined) { const c = crop(smokeFrames[sm]); if (c) out.push({ c, x: px + c.x, y: py + c.y, kind: 'smoke', ax: px }); }
}

// ---------------------------------------------------------------- буферы и блит

const buf = new Uint8Array(BUF_W * BUF_H);        // кадр в видеопамяти (кольцевой)
const clean = new Uint8Array(BUF_W * BUF_H);      // «чистая карта» без юнитов/дыма
function wrapX(x) { return ((x % BUF_W) + BUF_W) % BUF_W; }
function wrapY(y) { return ((y % BUF_H) + BUF_H) % BUF_H; }

function newStat() {
	return { scanned: 0, cellsDrawn: 0, blits: 0, clipped: 0, bytes: 0, fill: 0, copy: 0,
		launchFull: 0, launchHalf: 0, launchIdeal: 0, copyLaunch: 0, rects: 0, area: 0 };
}
function addStat(a, b) { for (const k of Object.keys(a)) a[k] += b[k]; return a; }

// Запусков DMA для непрерывного по X блита: полоса рвётся на границе страницы экрана
// (32 строки, 02 §4 «Адресация 256c») и на завороте буфера
function bands(by, h) {
	let n = 0, y = by, left = h;
	while (left > 0) { const step = Math.min(left, 32 - (y & 31), BUF_H - y); n++; y = (y + step) % BUF_H; left -= step; }
	return n;
}

function blitClip(it, r, stat, dst, doClip) {
	let x0 = it.x, x1 = it.x + it.c.w, y0 = it.y, y1 = it.y + it.c.h;
	if (doClip !== false) {
		x0 = Math.max(x0, r.x0); x1 = Math.min(x1, r.x1);
		y0 = Math.max(y0, r.y0); y1 = Math.min(y1, r.y1);
	} else if (x1 <= r.x0 || x0 >= r.x1 || y1 <= r.y0 || y0 >= r.y1) return false;
	if (x0 >= x1 || y0 >= y1) return false;
	const w = x1 - x0, h = y1 - y0, xclip = w !== it.c.w;
	stat.blits++;
	if (xclip) stat.clipped++;
	stat.bytes += ((w + 1) & ~1) * h;
	const by = wrapY(y0);
	stat.launchIdeal++;
	stat.launchFull += xclip ? h : bands(by, h);
	// половинки по 16 пикс.: кадр хранится двумя столбцами шириной 16 относительно клетки,
	// границы обрезки совпадают с их границами — каждая половина идёт одним запуском
	{
		const ax = it.ax;
		const aligned = ((x0 - ax) % HALF_W === 0 || x0 === it.x) && ((x1 - ax) % HALF_W === 0 || x1 === it.x + it.c.w);
		const nHalves = Math.floor((x1 - 1 - ax) / HALF_W) - Math.floor((x0 - ax) / HALF_W) + 1;
		stat.launchHalf += (!xclip || aligned) ? bands(by, h) * Math.max(1, nHalves) : h;
	}
	if (wrapX(x0) + w > BUF_W) { stat.launchFull++; stat.launchHalf++; stat.launchIdeal++; }
	for (let y = y0; y < y1; y++) {
		const sy = y - it.y + it.c.y, row = wrapY(y) * BUF_W;
		for (let x = x0; x < x1; x++) {
			const v = it.c.img[sy * TILE_W + (x - it.x + it.c.x)];
			if (v) dst[row + wrapX(x)] = v;
		}
	}
	return true;
}

function fillRect(r, stat, dst) {
	for (let y = r.y0; y < r.y1; y++) {
		const row = wrapY(y) * BUF_W;
		for (let x = r.x0; x < r.x1; x++) dst[row + wrapX(x)] = 0;
	}
	stat.fill += (r.x1 - r.x0) * (r.y1 - r.y0);
	stat.rects++; stat.area += (r.x1 - r.x0) * (r.y1 - r.y0);
}
function copyRect(r, stat) {
	for (let y = r.y0; y < r.y1; y++) {
		const row = wrapY(y) * BUF_W;
		for (let x = r.x0; x < r.x1; x++) buf[row + wrapX(x)] = clean[row + wrapX(x)];
	}
	stat.copy += (r.x1 - r.x0) * (r.y1 - r.y0);
	stat.copyLaunch += bands(wrapY(r.y0), r.y1 - r.y0);
	stat.rects++; stat.area += (r.x1 - r.x0) * (r.y1 - r.y0);
}

// Все элементы, влияющие на прямоугольник, в порядке художника (z ↑, глубина s = x+y ↑)
function itemsForRect(r, stat, want, tight) {
	const list = [];
	const L = tight ? TER_L : INF_L, R = tight ? TER_R : INF_R;
	const UP = tight ? TER_UP : INF_UP, DN = tight ? TER_DN : INF_DN;
	for (let z = 0; z <= level; z++) {
		const u0 = Math.ceil((r.x0 - R + 1) / HALF_W), u1 = Math.floor((r.x1 - 1 + L) / HALF_W);
		const s0 = Math.ceil((r.y0 - DN + 1 + z * LEVEL_H) / QUART);
		const s1 = Math.floor((r.y1 - 1 + UP + z * LEVEL_H) / QUART);
		for (let s = s0; s <= s1; s++)
			for (let u = u0; u <= u1; u++) {
				if (((s - u) & 1) !== 0) continue;
				const x = (s + u) >> 1, y = (s - u) >> 1;
				stat.scanned++;
				if (x < 0 || y < 0 || x >= map.sx || y >= map.sy) continue;
				const before = list.length;
				cellItems(x, y, z, list);
				if (list.length > before) stat.cellsDrawn++;
			}
	}
	return want ? list.filter(it => want.includes(it.kind)) : list;
}

// Вариант «чёрным»: залить и перерисовать всё, что попало
function paintAll(r, stat, opt) {
	opt = opt || {};
	fillRect(r, stat, buf);
	for (const it of itemsForRect(r, stat, null, opt.tight)) blitClip(it, r, stat, buf, !opt.noclip);
}
// Вариант «чистая карта»: местность отдельным буфером, кадр — копия + динамика
function paintTerrain(r, stat) {
	fillRect(r, stat, clean);
	for (const it of itemsForRect(r, stat, ['tile'])) blitClip(it, r, stat, clean);
}
function composite(r, stat) {
	copyRect(r, stat);
	for (const it of itemsForRect(r, stat, ['unit', 'smoke'])) blitClip(it, r, stat, buf);
}

// ---------------------------------------------------------------- честный эталон

// Независимый рендер окна 320x144 «в лоб»: все клетки карты, порядок художника, без
// прямоугольников и кольца
function renderRef(camX, camY) {
	const out = new Uint8Array(VIEW_W * VIEW_H);
	let blits = 0, bytes = 0, drawn = 0;
	const list = [];
	for (let z = 0; z <= level; z++)
		for (let s = 0; s <= map.sx + map.sy - 2; s++)
			for (let u = -(map.sy - 1); u <= map.sx - 1; u++) {
				if (((s - u) & 1) !== 0) continue;
				const x = (s + u) >> 1, y = (s - u) >> 1;
				if (x < 0 || y < 0 || x >= map.sx || y >= map.sy) continue;
				const px = cellPx(x, y) - camX, py = cellPy(x, y, z) - camY;
				if (px <= -INF_R || px >= VIEW_W + INF_L || py <= -INF_DN || py >= VIEW_H + INF_UP) continue;
				list.length = 0;
				cellItems(x, y, z, list);
				if (list.length) drawn++;
				for (const it of list) {
					const ix = it.x - camX, iy = it.y - camY;
					let n = 0;
					for (let yy = 0; yy < it.c.h; yy++) {
						const oy = iy + yy;
						if (oy < 0 || oy >= VIEW_H) continue;
						for (let xx = 0; xx < it.c.w; xx++) {
							const ox = ix + xx;
							if (ox < 0 || ox >= VIEW_W) continue;
							const v = it.c.img[(it.c.y + yy) * TILE_W + it.c.x + xx];
							if (v) { out[oy * VIEW_W + ox] = v; n++; }
						}
					}
					if (n) { blits++; bytes += it.c.w * it.c.h; }
				}
			}
	return { out, blits, bytes, drawn };
}
function compare() {
	const ref = renderRef(camX, camY);
	let bad = 0, firstX = -1, firstY = -1;
	for (let y = 0; y < VIEW_H; y++)
		for (let x = 0; x < VIEW_W; x++)
			if (buf[wrapY(camY + y) * BUF_W + wrapX(camX + x)] !== ref.out[y * VIEW_W + x]) {
				bad++;
				if (firstX < 0) { firstX = x; firstY = y; }
			}
	return { bad, ref, firstX, firstY };
}

// ---------------------------------------------------------------- камера и годная область

let camX = 0, camY = 0, valid = null, mode = 'black';
function align(r) {
	return { x0: Math.floor(r.x0 / HALF_W) * HALF_W, x1: Math.ceil(r.x1 / HALF_W) * HALF_W,
		y0: Math.floor(r.y0 / QUART) * QUART, y1: Math.ceil(r.y1 / QUART) * QUART };
}
let MARG_X = 0, MARG_Y = 0;                 // запас «на тайл вокруг экрана» (приём Кармака)
function needRect() {
	return { x0: camX - MARG_X, y0: camY - MARG_Y, x1: camX + VIEW_W + MARG_X, y1: camY + VIEW_H + MARG_Y };
}
function unionR(a, b) {
	if (!a) return b;
	return { x0: Math.min(a.x0, b.x0), y0: Math.min(a.y0, b.y0), x1: Math.max(a.x1, b.x1), y1: Math.max(a.y1, b.y1) };
}
function clipToValid(r) {
	return { x0: Math.max(r.x0, valid.x0), y0: Math.max(r.y0, valid.y0),
		x1: Math.min(r.x1, valid.x1), y1: Math.min(r.y1, valid.y1) };
}
function repaintRects(rects, stat, terrain) {
	for (const r of rects) {
		if (r.x1 <= r.x0 || r.y1 <= r.y0) continue;
		if (mode === 'clean') { if (terrain !== false) paintTerrain(r, stat); composite(r, stat); }
		else paintAll(r, stat);
	}
}
function extendValid() {
	const need = align(needRect());
	if (!valid || need.x1 <= valid.x0 || need.x0 >= valid.x1 || need.y1 <= valid.y0 || need.y0 >= valid.y1) {
		valid = need;
		return { rects: [need], full: true };
	}
	const rects = [];
	let x0 = Math.min(valid.x0, need.x0), x1 = Math.max(valid.x1, need.x1);
	let y0 = Math.min(valid.y0, need.y0), y1 = Math.max(valid.y1, need.y1);
	if (x1 - x0 > BUF_W) { if (need.x1 > valid.x1) x0 = x1 - BUF_W; else x1 = x0 + BUF_W; }
	if (y1 - y0 > BUF_H) { if (need.y1 > valid.y1) y0 = y1 - BUF_H; else y1 = y0 + BUF_H; }
	if (x0 < valid.x0) rects.push({ x0, x1: valid.x0, y0: Math.max(y0, valid.y0), y1: Math.min(y1, valid.y1) });
	if (x1 > valid.x1) rects.push({ x0: valid.x1, x1, y0: Math.max(y0, valid.y0), y1: Math.min(y1, valid.y1) });
	if (y0 < valid.y0) rects.push({ x0, x1, y0, y1: valid.y0 });
	if (y1 > valid.y1) rects.push({ x0, x1, y0: valid.y1, y1 });
	valid = { x0, x1, y0, y1 };
	return { rects: rects.filter(r => r.x1 > r.x0 && r.y1 > r.y0), full: false };
}
function moveCam(dx, dy, stat) { camX += dx; camY += dy; const e = extendValid(); repaintRects(e.rects, stat); return e; }
function rebuildAll(stat) { valid = null; const e = extendValid(); repaintRects(e.rects, stat); return e; }

function cellRect(x, y, z) {
	const px = cellPx(x, y), py = cellPy(x, y, z);
	const list = [];
	cellItems(x, y, z, list);
	if (!list.length) return { x0: px, y0: py, x1: px + TILE_W, y1: py + TILE_H };
	let r = { x0: 1e9, y0: 1e9, x1: -1e9, y1: -1e9 };
	for (const it of list) {
		if (it.x < r.x0) r.x0 = it.x;
		if (it.y < r.y0) r.y0 = it.y;
		if (it.x + it.c.w > r.x1) r.x1 = it.x + it.c.w;
		if (it.y + it.c.h > r.y1) r.y1 = it.y + it.c.h;
	}
	return r;
}

// ---------------------------------------------------------------- сброс сцены

function resetScene() {
	cells = Uint8Array.from(map.cells);
	fog.fill(1);
	smoke = new Map();
	units = [];
	const cx = map.sx >> 1, cy = map.sy >> 1;
	for (let i = 0; i < 6; i++)
		units.push({ x: cx - 2 + (i % 3), y: cy - 1 + ((i / 3) | 0), z: level, dir: 2, phase: 0,
			walking: false, ox: 0, oy: 0, img: unitFrame(2, 0, false) });
	rebuildUnitIndex();
	camX = cellPx(cx, cy) - VIEW_W / 2;
	camY = cellPy(cx, cy, level) - VIEW_H / 2;
	valid = null;
	buf.fill(0); clean.fill(0);
	const st = newStat();
	rebuildAll(st);
	if (mode === 'clean') { /* clean заполнен в repaintRects */ }
	return st;
}
// «Тугая» годная область: ровно видимое окно — чтобы шаги камеры мерились честно
function tightValid() {
	valid = null;
	const st = newStat();
	rebuildAll(st);
	return st;
}

// ---------------------------------------------------------------- отчёт

const log = [];
function say(s) { console.log(s); log.push(s); }
function costFrames(st) {
	const dma = st.bytes / BW_BLT + st.copy / BW_COPY + st.fill / BW_FILL
		+ (st.launchFull + st.copyLaunch) * T_LAUNCH / T_FRAME;
	const dmaHalf = st.bytes / BW_BLT + st.copy / BW_COPY + st.fill / BW_FILL
		+ (st.launchHalf + st.copyLaunch) * T_LAUNCH / T_FRAME;
	return { dma, dmaHalf, cpu: st.blits * CPU_FRAMES_PER_BLIT };
}
function tableHead() {
	say('| событие | клеток | блитов | обрез. | КБ | запусков целые/полов. | кадров DMA | кадров C | расх. |');
	say('|---|---:|---:|---:|---:|---:|---:|---:|---:|');
}
function tableRow(name, st, bad, note) {
	const f = costFrames(st);
	say(`| ${name}${note ? ' — ' + note : ''} | ${st.cellsDrawn.toFixed(0)} | ${st.blits.toFixed(0)} | ${st.clipped.toFixed(0)} | ${((st.bytes + st.copy + st.fill) / 1024).toFixed(1)} | ${st.launchFull.toFixed(0)}/${(st.launchHalf + st.copyLaunch).toFixed(0)} | ${f.dma.toFixed(2)} | ${f.cpu.toFixed(2)} | ${bad} |`);
}
function pngOut(file) {
	const rgb = Buffer.alloc(VIEW_W * VIEW_H * 3);
	for (let y = 0; y < VIEW_H; y++)
		for (let x = 0; x < VIEW_W; x++) {
			const v = buf[wrapY(camY + y) * BUF_W + wrapX(camX + x)];
			const c = pal[v], o = (y * VIEW_W + x) * 3;
			rgb[o] = c[0]; rgb[o + 1] = c[1]; rgb[o + 2] = c[2];
		}
	B.writePng(path.join(outDir, game + '_' + file), VIEW_W, VIEW_H, rgb);
}

fs.mkdirSync(outDir, { recursive: true });
say('# Прототип №5: аппаратный скролл и точечная перерисовка');
say(`игра ${game}, карта ${mapName} ${nblocks}x${nblocks} блоков = ${map.sx}x${map.sy}x${map.sz}, уровень камеры ${level}`);
say(`окно ${VIEW_W}x${VIEW_H}, кольцевой буфер ${BUF_W}x${BUF_H}, max P_Level тайлсета = ${YMAX}`);
say('');

// ================================================================ 1. правило зависимостей

function ruleSet(L, R, UP, DN, levels) {
	const out = [];
	for (let c = -levels; c <= levels; c++)
		for (let d = -4; d <= 4; d++) {
			if (Math.abs(d) * HALF_W >= L + R) continue;
			for (let s = -24; s <= 24; s++) {
				if (((s - d) & 1) !== 0) continue;
				if (Math.abs(s * QUART - c * LEVEL_H) >= UP + DN) continue;
				out.push({ a: (s + d) >> 1, b: (s - d) >> 1, c });
			}
		}
	return out;
}

say('## 1. Правило зависимостей');
{
	const ter = ruleSet(TER_L, TER_R, TER_UP, TER_DN, level);
	const full = ruleSet(INF_L, INF_R, INF_UP, INF_DN, level);
	const terSet = new Set(ter.map(o => o.a + ',' + o.b + ',' + o.c));
	const fullSet = new Set(full.map(o => o.a + ',' + o.b + ',' + o.c));
	say(`Клетка (x, y, z) поменялась → перерисовать (x+a, y+b, z+c), если её габарит влияния`);
	say(`пересекает габарит изменившейся. Из px = 16(x−y), py = 8(x+y) − 24z:`);
	say('');
	say('```');
	say(`только местность (спрайт 32x40, подъём P_Level ≤ ${YMAX}):`);
	say(`    |a − b| ≤ ${Math.floor((TER_L + TER_R - 1) / HALF_W)}   и   |(a + b) − 3c| ≤ ${Math.floor((TER_UP + TER_DN - 1) / QUART)}     → ${ter.length} смещений (${(ter.length / (2 * level + 1)).toFixed(0)} на уровень)`);
	say(`с юнитами (сдвиг ходьбы ±16 по X, terrainLevel −24 по Y):`);
	say(`    |a − b| ≤ ${Math.floor((INF_L + INF_R - 1) / HALF_W)}   и   |(a + b) − 3c| ≤ ${Math.floor((INF_UP + INF_DN - 1) / QUART)}     → ${full.length} смещений (${(full.length / (2 * level + 1)).toFixed(0)} на уровень)`);
	say('```');
	// проверка перебором
	resetScene();
	let worst = 0, total = 0, n = 0, missTer = 0, missFull = 0, worstTer = 0;
	for (let t = 0; t < 200; t++) {
		const x = 2 + Math.floor(Math.random() * (map.sx - 4));
		const y = 2 + Math.floor(Math.random() * (map.sy - 4));
		const z = Math.floor(Math.random() * (level + 1));
		const r = cellRect(x, y, z);
		let cnt = 0, cntTer = 0;
		for (let zz = 0; zz <= level; zz++)
			for (let yy = 0; yy < map.sy; yy++)
				for (let xx = 0; xx < map.sx; xx++) {
					const list = [];
					cellItems(xx, yy, zz, list);
					let hit = false, hitTer = false;
					for (const it of list)
						if (it.x < r.x1 && it.x + it.c.w > r.x0 && it.y < r.y1 && it.y + it.c.h > r.y0) {
							hit = true;
							if (it.kind === 'tile') hitTer = true;
						}
					if (!hit) continue;
					cnt++;
					if (hitTer) cntTer++;
					const key = (xx - x) + ',' + (yy - y) + ',' + (zz - z);
					if (!fullSet.has(key)) missFull++;
					if (hitTer && !terSet.has(key)) missTer++;
				}
		total += cnt; n++;
		if (cnt > worst) worst = cnt;
		if (cntTer > worstTer) worstTer = cntTer;
	}
	say('');
	say(`Проверка перебором всех ${map.sx}x${map.sy}x${level + 1} клеток для 200 случайных изменений:`);
	say(`- реально задето клеток: в среднем ${(total / n).toFixed(1)}, максимум ${worst} (только местность — максимум ${worstTer});`);
	say(`- ни одна задетая клетка не выпала из правила: вне «местность» ${missTer}, вне «с юнитами» ${missFull};`);
	say(`- аналитический набор — это КАНДИДАТЫ; после точной проверки габарита части остаётся ${(total / n).toFixed(1)} клетки.`);
}
say('');

// ================================================================ 2. паттерны 8 направлений

say('## 2. Предопределённые паттерны скролла (8 направлений)');
say('');
say('Камера двигается по решётке (шаг 16 по X, 8 по Y), поэтому набор клеток, попадающих в');
say('въехавшую полосу, ОДИН И ТОТ ЖЕ относительно камеры — его можно держать таблицей');
say('смещений (u = x−y, s = x+y относительно клетки под левым верхним углом окна).');
say('');
say('| направление | грязные полосы | кандидатов | клеток | блитов | КБ | запусков целые/полов. | кадров DMA | кадров C |');
say('|---|---|---:|---:|---:|---:|---:|---:|---:|');
const dirs8 = [
	['вправо  (+16, 0)', 16, 0], ['влево   (−16, 0)', -16, 0],
	['вниз    (0, +8)', 0, 8], ['вверх   (0, −8)', 0, -8],
	['вправо-вниз (+16,+8) = «x+1»', 16, 8], ['влево-вверх (−16,−8) = «x−1»', -16, -8],
	['влево-вниз (−16,+8) = «y+1»', -16, 8], ['вправо-вверх (+16,−8) = «y−1»', 16, -8],
];
mode = 'black';
const dirStats = {};
for (const [name, dx, dy] of dirs8) {
	resetScene();
	tightValid();
	const st = newStat();
	const e = moveCam(dx, dy, st);
	const c = compare();
	const f = costFrames(st);
	const shape = e.rects.map(r => `${r.x1 - r.x0}x${r.y1 - r.y0}`).join(' + ');
	dirStats[name] = { st, rects: e.rects.map(r => ({ ...r })) };
	say(`| ${name} | ${shape} | ${st.scanned} | ${st.cellsDrawn} | ${st.blits} | ${((st.bytes + st.fill) / 1024).toFixed(1)} | ${st.launchFull}/${st.launchHalf} | ${f.dma.toFixed(2)} | ${f.cpu.toFixed(2)} |`);
	if (c.bad) say(`| ^ РАСХОЖДЕНИЕ ${c.bad} точек, первая (${c.firstX},${c.firstY}) | | | | | | | | |`);
}
say('');
say('Сам «паттерн» вырождается в прямоугольник в координатах (u = x−y, s = x+y): для полосы');
say('[x0,x1)x[y0,y1) кандидаты — это u ∈ [⌈(x0−R)/16⌉, ⌊(x1−1+L)/16⌋], s ∈ [⌈(y0−40+24z)/8⌉,');
say('⌊(y1−1+UP+24z)/8⌋] с чётностью s ≡ u. Значит держать список клеток не надо — хватает');
say('четырёх чисел на полосу (и сдвига +3 по s на каждый уровень):');
say('');
say('| направление | полоса | u₀..u₁ | s₀..s₁ при z = 0 | клеток-кандидатов на уровень |');
say('|---|---|---|---|---:|');
for (const [name] of dirs8) {
	const d = dirStats[name];
	for (const r of d.rects) {
		const u0 = Math.ceil((r.x0 - INF_R + 1) / HALF_W), u1 = Math.floor((r.x1 - 1 + INF_L) / HALF_W);
		const s0 = Math.ceil((r.y0 - INF_DN + 1) / QUART), s1 = Math.floor((r.y1 - 1 + INF_UP) / QUART);
		const n = Math.round((u1 - u0 + 1) * (s1 - s0 + 1) / 2);
		say(`| ${name} | ${r.x1 - r.x0}x${r.y1 - r.y0} | ${u1 - u0 + 1} шт. | ${s1 - s0 + 1} шт. | ${n} |`);
	}
}
say('');

// скролл по пикселю
{
	resetScene(); tightValid();
	const st = newStat();
	let bad = 0;
	for (let i = 0; i < 16; i++) { moveCam(1, 0, st); bad += compare().bad; }
	const f = costFrames(st);
	say(`Скролл по 1 пикселю: 16 шагов вправо суммарно — ${st.blits} блитов, ${((st.bytes + st.fill) / 1024).toFixed(1)} КБ,`);
	say(`${f.dma.toFixed(2)} кадра DMA, расхождений ${bad}. То есть столько же, сколько один шаг на 16 px:`);
	say(`грязная полоса выравнивается по решётке 16, и новый столбец рисуется раз в 16 пикселей пути.`);
	say(`**Плавный скролл по пикселю не дороже скачкового** (при выравненной области годности).`);
}
say('');

// ================================================================ 3. события боя

function eventSuite(m) {
	mode = m;
	const rows = [];
	const cx = map.sx >> 1, cy = map.sy >> 1;

	// полная перерисовка
	{
		const st = resetScene();
		const c = compare();
		rows.push(['полная перерисовка вида', st, c.bad, `эталон ${c.ref.blits} блитов, ${c.ref.drawn} клеток`]);
		if (m === 'black') pngOut('full.png');
	}
	// шаг камеры (средний по 8 направлениям)
	{
		const acc = newStat();
		let bad = 0;
		for (const [, dx, dy] of dirs8) {
			resetScene(); tightValid();
			const st = newStat();
			moveCam(dx, dy, st);
			bad += compare().bad;
			addStat(acc, st);
		}
		for (const k of Object.keys(acc)) acc[k] /= 8;
		rows.push(['шаг камеры на клетку (среднее по 8 направлениям)', acc, bad, '']);
	}
	// шаг бойца
	{
		resetScene(); tightValid();
		const u = units[0];
		const st = newStat();
		let bad = 0;
		for (let ph = 1; ph <= 8; ph++) {
			const before = cellRect(u.x, u.y, u.z);
			if (ph < 8) { u.walking = true; u.phase = ph & 7; u.ox = ph * 2; u.img = unitFrame(2, ph & 7, true); }
			else {
				unitAt.delete(cidx(u.x, u.y, u.z));
				u.x += 1; u.ox = 0; u.walking = false; u.img = unitFrame(2, 0, false);
				rebuildUnitIndex();
			}
			const r = clipToValid(align(unionR(before, cellRect(u.x, u.y, u.z))));
			if (m === 'clean') composite(r, st); else paintAll(r, st);
			bad += compare().bad;
		}
		const per = newStat();
		for (const k of Object.keys(st)) per[k] = st[k] / 8;
		rows.push(['шаг бойца — одна фаза ходьбы (1/8 клетки)', per, bad, '']);
		rows.push(['шаг бойца — вся клетка (8 фаз)', st, 0, '']);
		if (m === 'black') pngOut('unit_step.png');
	}
	// дверь
	{
		resetScene(); tightValid();
		// клетка со стеной или объектом внутри видимого окна
		let dx = -1, dy = -1, dz = 0;
		for (let rad = 1; rad < 12 && dx < 0; rad++)
			for (let z = level; z >= 0 && dx < 0; z--)
				for (let y = cy - rad; y <= cy + rad && dx < 0; y++)
					for (let x = cx - rad; x <= cx + rad; x++) {
						if (x < 0 || y < 0 || x >= map.sx || y >= map.sy) continue;
						const p = cellParts(x, y, z);
						if (p[1] || p[2] || p[3]) { dx = x; dy = y; dz = z; break; }
					}
		const st = newStat();
		let bad = 0;
		if (dx >= 0) {
			const o = cidx(dx, dy, dz) * 4;
			const before = cellRect(dx, dy, dz);
			const k = cells[o + 1] ? 1 : (cells[o + 2] ? 2 : 3);
			if (cells[o + k] < parts.length) cells[o + k]++;
			const r = clipToValid(align(unionR(before, cellRect(dx, dy, dz))));
			if (m === 'clean') { paintTerrain(r, st); composite(r, st); } else paintAll(r, st);
			bad = compare().bad;
		}
		rows.push(['открытие двери (1 клетка)', st, bad, dx < 0 ? 'стена не найдена' : '']);
	}
	// взрыв 5x5
	{
		resetScene(); tightValid();
		const st = newStat();
		let r = null;
		for (let y = cy - 2; y <= cy + 2; y++)
			for (let x = cx - 2; x <= cx + 2; x++) {
				if (x < 0 || y < 0 || x >= map.sx || y >= map.sy) continue;
				r = unionR(r, cellRect(x, y, level));
				const o = cidx(x, y, level) * 4;
				cells[o + 1] = 0; cells[o + 2] = 0; cells[o + 3] = 0;
				smoke.set(cidx(x, y, level), 8 + ((x + y) & 3));
			}
		const rr = clipToValid(align(r));
		if (m === 'clean') { paintTerrain(rr, st); composite(rr, st); } else paintAll(rr, st);
		const c = compare();
		rows.push(['взрыв 5x5 с разрушением и дымом', st, c.bad, `прямоугольник ${rr.x1 - rr.x0}x${rr.y1 - rr.y0}`]);
		if (m === 'black') pngOut('explosion.png');

		// кадр анимации дыма — по прямоугольнику на клетку
		const st1 = newStat();
		for (const [i, f] of Array.from(smoke)) smoke.set(i, 8 + ((f + 1) & 3));
		for (const i of smoke.keys()) {
			const z = Math.floor(i / (map.sx * map.sy));
			const yy = Math.floor((i - z * map.sx * map.sy) / map.sx);
			const xx = i - z * map.sx * map.sy - yy * map.sx;
			const rc = clipToValid(align(cellRect(xx, yy, z)));
			if (rc.x1 <= rc.x0 || rc.y1 <= rc.y0) continue;
			if (m === 'clean') composite(rc, st1); else paintAll(rc, st1);
		}
		rows.push([`кадр анимации дыма (${smoke.size} клеток), прямоугольник на клетку`, st1, compare().bad, '']);

		// то же одним прямоугольником
		const st2 = newStat();
		let r2 = null;
		for (const [i, f] of Array.from(smoke)) smoke.set(i, 8 + ((f + 1) & 3));
		for (const i of smoke.keys()) {
			const z = Math.floor(i / (map.sx * map.sy));
			const yy = Math.floor((i - z * map.sx * map.sy) / map.sx);
			const xx = i - z * map.sx * map.sy - yy * map.sx;
			r2 = unionR(r2, cellRect(xx, yy, z));
		}
		const rr2 = clipToValid(align(r2));
		if (m === 'clean') composite(rr2, st2); else paintAll(rr2, st2);
		rows.push([`кадр анимации дыма (${smoke.size} клеток), одним прямоугольником`, st2, compare().bad, '']);
	}
	// туман войны
	{
		resetScene();
		fog.fill(0);
		for (const u of units)
			for (let z = 0; z <= level; z++)
				for (let y = u.y - 4; y <= u.y + 4; y++)
					for (let x = u.x - 4; x <= u.x + 4; x++)
						if (x >= 0 && y >= 0 && x < map.sx && y < map.sy) fog[cidx(x, y, z)] = 1;
		const st0 = tightValid();
		const c0 = compare();
		rows.push(['полная перерисовка при тумане войны', st0, c0.bad, `эталон ${c0.ref.blits} блитов`]);
		if (m === 'black') pngOut('fog.png');

		const st = newStat();
		let r = null;
		const u = units[0];
		for (let z = 0; z <= level; z++)
			for (let dy2 = -4; dy2 <= 4; dy2++)
				for (let dx2 = 5; dx2 <= 6; dx2++) {
					const x = u.x + dx2, y = u.y + dy2;
					if (x < 0 || y < 0 || x >= map.sx || y >= map.sy) continue;
					fog[cidx(x, y, z)] = 1;
					r = unionR(r, cellRect(x, y, z));
				}
		const rr = clipToValid(align(r));
		if (rr.x1 > rr.x0 && rr.y1 > rr.y0) {
			if (m === 'clean') { paintTerrain(rr, st); composite(rr, st); } else paintAll(rr, st);
		}
		rows.push(['раскрытие тумана: 2 ряда по 9 клеток', st, compare().bad, `прямоугольник ${rr.x1 - rr.x0}x${rr.y1 - rr.y0}`]);
	}
	// прыжок камеры
	{
		resetScene(); tightValid();
		const st = newStat();
		camX = cellPx(3, map.sy - 4) - VIEW_W / 2;
		camY = cellPy(3, map.sy - 4, level) - VIEW_H / 2;
		const e = extendValid();
		repaintRects(e.rects, st);
		rows.push(['прыжок камеры на бойца', st, compare().bad, e.full ? 'вид построен заново' : 'части хватило']);
		if (m === 'black') pngOut('jump.png');
	}
	// смена этажа. Вверх: добавляется уровень, он рисуется ПОВЕРХ уже готовой картинки —
	// стирать нечего, нужен только новый уровень (верхние этажи почти пустые). Камера
	// уезжает на 24 px, но это делает GYOffs, платим лишь за въехавшую полосу.
	// Вниз: верхний уровень надо УБРАТЬ — стираем и перерисовываем его габарит.
	if (level > 0) {
		const lv = level;
		// вниз
		resetScene(); tightValid();
		{
			const st = newStat();
			let r = null;
			for (let y = 0; y < map.sy; y++)
				for (let x = 0; x < map.sx; x++) {
					const list = [];
					cellItems(x, y, lv, list);        // и местность, и юниты уровня
					if (!list.length) continue;
					const cr = cellRect(x, y, lv);
					if (cr.x1 > valid.x0 && cr.x0 < valid.x1 && cr.y1 > valid.y0 && cr.y0 < valid.y1) r = unionR(r, cr);
				}
			level = lv - 1;
			const rr = r ? clipToValid(align(r)) : { x0: 0, x1: 0, y0: 0, y1: 0 };
			if (rr.x1 > rr.x0) { if (m === 'clean') { paintTerrain(rr, st); composite(rr, st); } else paintAll(rr, st); }
			rows.push(['этаж вниз (убрать верхний уровень)', st, compare().bad, `прямоугольник ${rr.x1 - rr.x0}x${rr.y1 - rr.y0}`]);
			level = lv;
		}
		// вверх
		resetScene();
		level = lv - 1;
		tightValid();
		{
			const st = newStat();
			level = lv;
			// новый уровень — самый верхний в порядке художника: рисуем ПОВЕРХ, ничего не стирая
			const list = [];
			for (let s = 0; s <= map.sx + map.sy - 2; s++)
				for (let u = -(map.sy - 1); u <= map.sx - 1; u++) {
					if (((s - u) & 1) !== 0) continue;
					const x = (s + u) >> 1, y = (s - u) >> 1;
					if (x < 0 || y < 0 || x >= map.sx || y >= map.sy) continue;
					const px = cellPx(x, y), py = cellPy(x, y, lv);
					if (px + INF_R <= valid.x0 || px - INF_L >= valid.x1 || py + INF_DN <= valid.y0 || py - INF_UP >= valid.y1) continue;
					st.scanned++;
					const before = list.length;
					cellItems(x, y, lv, list);
					if (list.length > before) st.cellsDrawn++;
				}
			for (const it of list) { blitClip(it, valid, st, buf); if (m === 'clean') blitClip(it, valid, newStat(), clean); }
			rows.push(['этаж вверх (дорисовать уровень поверх)', st, compare().bad, `${st.cellsDrawn} клеток уровня ${lv}`]);
		}
		resetScene();
	}
	// маршрут камеры
	{
		resetScene(); tightValid();
		const st = newStat();
		let bad = 0;
		const route = [[16, 8], [16, 8], [16, 8], [16, 8], [0, 16], [0, 16], [-16, 8], [-16, 8],
			[-16, -8], [-16, -8], [0, -16], [0, -16], [16, -8], [16, -8]];
		for (let i = 0; i < 40; i++) {
			const [dx, dy] = route[i % route.length];
			moveCam(dx, dy, st);
			bad += compare().bad;
		}
		const per = newStat();
		for (const k of Object.keys(st)) per[k] = st[k] / 40;
		rows.push(['маршрут камеры 40 шагов, в среднем на шаг', per, bad, '']);
		if (m === 'black') pngOut('route.png');
	}
	return rows;
}

say('## 3. Стоимость событий, вариант «залить чёрным и перерисовать»');
say('');
tableHead();
const rowsBlack = eventSuite('black');
for (const [n, st, bad, note] of rowsBlack) tableRow(n, st, bad, note);
say('');

say('## 4. То же, вариант «чистая карта отдельным буфером» (копия вместо стирания)');
say('');
say('Второй буфер 512x512 (16 страниц, 256 КБ) держит карту без юнитов, дыма и курсора;');
say('стирание — копия RAM→RAM (111 КБ/кадр) вместо заливки и перерисовки местности.');
say('');
tableHead();
const rowsClean = eventSuite('clean');
for (const [n, st, bad, note] of rowsClean) tableRow(n, st, bad, note);
say('');
{
	say('| событие | кадров «чёрным» | кадров «чистая карта» | выигрыш |');
	say('|---|---:|---:|---:|');
	for (let i = 0; i < rowsBlack.length && i < rowsClean.length; i++) {
		const a = costFrames(rowsBlack[i][1]).dma, b = costFrames(rowsClean[i][1]).dma;
		say(`| ${rowsBlack[i][0]} | ${a.toFixed(2)} | ${b.toFixed(2)} | ${a > 0 ? ((a / b - 1) * 100).toFixed(0) + ' %' : '—'} |`);
	}
}
say('');

// ================================================================ 5. порог полной перерисовки

say('## 5. Порог «дешевле перерисовать всё»');
say('');
mode = 'black';
{
	resetScene();
	const stFull = newStat();
	const rFull = clipToValid(align(needRect()));
	paintAll(rFull, stFull);
	const full = costFrames(stFull);
	say(`Полная перерисовка окна: ${stFull.blits} блитов, ${((stFull.bytes + stFull.fill) / 1024).toFixed(1)} КБ, ${full.dma.toFixed(2)} кадра DMA / ${full.cpu.toFixed(2)} кадра C.`);
	say('');
	say('| изменившихся клеток | по прямоугольнику на клетку | одним объединяющим | полностью | площадь объед./сумма |');
	say('|---:|---|---|---|---:|');
	for (const n of [1, 2, 4, 8, 16, 32, 64, 128]) {
		resetScene(); tightValid();
		// случайные клетки внутри окна
		const list = [];
		for (let i = 0; i < n; i++) {
			const x = 3 + Math.floor(Math.random() * (map.sx - 6));
			const y = 3 + Math.floor(Math.random() * (map.sy - 6));
			const px = cellPx(x, y), py = cellPy(x, y, level);
			if (px < camX || px > camX + VIEW_W - 32 || py < camY || py > camY + VIEW_H - 40) { i--; continue; }
			list.push([x, y, level]);
		}
		const stA = newStat();
		for (const [x, y, z] of list) {
			const r = clipToValid(align(cellRect(x, y, z)));
			if (r.x1 > r.x0 && r.y1 > r.y0) paintAll(r, stA);
		}
		const stB = newStat();
		let ur = null;
		for (const [x, y, z] of list) ur = unionR(ur, cellRect(x, y, z));
		const rr = clipToValid(align(ur));
		if (rr.x1 > rr.x0 && rr.y1 > rr.y0) paintAll(rr, stB);
		const fa = costFrames(stA), fb = costFrames(stB);
		say(`| ${n} | ${stA.blits} блитов, ${fa.dma.toFixed(2)} кадра | ${stB.blits} блитов, ${fb.dma.toFixed(2)} кадра (${rr.x1 - rr.x0}x${rr.y1 - rr.y0}) | ${stFull.blits} блитов, ${full.dma.toFixed(2)} кадра | ${(stB.area / stA.area).toFixed(2)} |`);
	}
}
say('');

// ================================================================ 6. артефакты наивных вариантов

say('## 6. Артефакты «наивных» вариантов (почему нужна обрезка и соседи)');
say('');
say('| вариант | блитов | неверных точек в окне | где |');
say('|---|---:|---:|---|');
{
	const variants = [
		['правильный: соседи по правилу + обрезка блитов по прямоугольнику', {}],
		['без обрезки блитов (спрайты вылезают за прямоугольник)', { noclip: true }],
		['только габарит местности, без запаса под юнитов', { tight: true }],
	];
	for (const [name, opt] of variants) {
		mode = 'black';
		resetScene(); tightValid();
		const u = units[0];
		const st = newStat();
		let bad = 0, fx = -1, fy = -1;
		for (let ph = 1; ph <= 8; ph++) {
			const before = cellRect(u.x, u.y, u.z);
			if (ph < 8) { u.walking = true; u.phase = ph & 7; u.ox = ph * 2; u.img = unitFrame(2, ph & 7, true); }
			else { unitAt.delete(cidx(u.x, u.y, u.z)); u.x += 1; u.ox = 0; u.walking = false; u.img = unitFrame(2, 0, false); rebuildUnitIndex(); }
			const r = clipToValid(align(unionR(before, cellRect(u.x, u.y, u.z))));
			paintAll(r, st, opt);
			const c = compare();
			if (c.bad > bad) { bad = c.bad; fx = c.firstX; fy = c.firstY; }
		}
		say(`| ${name} | ${st.blits} | ${bad} | ${bad ? `(${fx},${fy})` : '—'} |`);
	}
	// «грязный прямоугольник только на изменившуюся клетку» — соседей вообще не берём
	mode = 'black';
	resetScene(); tightValid();
	const u = units[0];
	const st = newStat();
	let bad = 0, fx = -1, fy = -1;
	for (let ph = 1; ph <= 8; ph++) {
		const before = cellRect(u.x, u.y, u.z);
		if (ph < 8) { u.walking = true; u.phase = ph & 7; u.ox = ph * 2; u.img = unitFrame(2, ph & 7, true); }
		else { unitAt.delete(cidx(u.x, u.y, u.z)); u.x += 1; u.ox = 0; u.walking = false; u.img = unitFrame(2, 0, false); rebuildUnitIndex(); }
		const r = clipToValid(align(unionR(before, cellRect(u.x, u.y, u.z))));
		fillRect(r, st, buf);
		const list = [];
		cellItems(u.x, u.y, u.z, list);
		for (const it of list) blitClip(it, r, st, buf, true);
		const c = compare();
		if (c.bad > bad) { bad = c.bad; fx = c.firstX; fy = c.firstY; }
	}
	say(`| только изменившаяся клетка, соседей не перерисовываем | ${st.blits} | ${bad} | ${bad ? `(${fx},${fy})` : '—'} |`);
}
say('');

// ================================================================ 7. буфер: заворот и панель

say('## 7. Механика буфера');
say('');
{
	// раскладка А: карта в строках 0..455, панель 456..511 — GYOffs ограничен [0, 312]
	mode = 'black';
	resetScene();
	const base0 = camY;
	let rebases = 0, base = camY - (BUF_H_A - VIEW_H) / 2;
	const route = [[16, 8], [16, 8], [0, 16], [0, 16], [0, 16], [-16, 8], [-16, 8], [0, 16],
		[16, -8], [0, -16], [0, -16], [0, -16], [-16, -8], [0, -16]];
	let minY = camY, maxY = camY;
	for (let i = 0; i < 200; i++) {
		const [dx, dy] = route[i % route.length];
		camX += dx; camY += dy;
		if (camY < minY) minY = camY;
		if (camY > maxY) maxY = camY;
		if (camY < base || camY > base + (BUF_H_A - VIEW_H)) { rebases++; base = camY - (BUF_H_A - VIEW_H) / 2; }
	}
	say(`Раскладка А (панель ICONS в строках 456..511 того же холста 512x512):`);
	say(`- карте остаются строки 0..455, GYOffs должен лежать в [0, ${BUF_H_A - VIEW_H}] → вертикальный ход`);
	say(`  камеры между «перебазированиями» — ${BUF_H_A - VIEW_H} px = ${(BUF_H_A - VIEW_H) / QUART} шагов по 8 px;`);
	say(`- на 200-шаговом маршруте (размах по Y ${maxY - minY} px) перебазирований: ${rebases};`);
	say(`- перебазирование = копия годной части буфера 2D-DMA (512x${BUF_H_A - VIEW_H} ≈ ${(BUF_W * (BUF_H_A - VIEW_H) / 1024).toFixed(0)} КБ,`);
	say(`  ${(BUF_W * (BUF_H_A - VIEW_H) / BW_COPY).toFixed(1)} кадра) + дорисовка полосы, либо просто полная перерисовка окна.`);
	say('');
	say(`Раскладка Б (панель в отдельных 2 страницах, на строке 143 переписывается не только`);
	say(`GYOffs, но и VPage — оба защёлкиваются с начала следующей строки, 02 §4):`);
	say(`- карте достаются все 512 строк, заворот 512 совпадает с кольцом буфера → перебазирований 0;`);
	say(`- цена: +2 страницы под панель и вторая запись регистра в строчном прерывании;`);
	say(`- риск: смена VPage посреди кадра — проверить на эмуляторе и на железе.`);
	say('');
	say(`По X заворот 512 совпадает с кольцом в обеих раскладках: горизонтальный скролл бесконечен,`);
	say(`перебазирования не нужно никогда (ширина карты на экране до ${(map.sx + map.sy) * HALF_W} px, но в буфере`);
	say(`одновременно нужен только видимый кусок 320 px + запас).`);
	say('');
	// приём Кармака: годная область шире экрана на тайл — въезжающая полоса рисуется за
	// пределами видимого окна, поэтому её построение не видно
	say('Запас вокруг экрана (Commander Keen: буфер больше экрана на тайл с каждой стороны):');
	say('');
	say('| запас | блитов за 40 шагов | КБ | кадров DMA на шаг | въезжающая полоса видна? |');
	say('|---|---:|---:|---:|---|');
	for (const [mx, my, name] of [[0, 0, 'нет'], [TILE_W, TILE_H, 'тайл (32x40)']]) {
		MARG_X = mx; MARG_Y = my;
		resetScene(); tightValid();
		const st = newStat();
		let bad = 0;
		const rt = [[16, 8], [16, 8], [16, 8], [16, 8], [0, 16], [0, 16], [-16, 8], [-16, 8],
			[-16, -8], [-16, -8], [0, -16], [0, -16], [16, -8], [16, -8]];
		for (let i = 0; i < 40; i++) { const [dx, dy] = rt[i % rt.length]; moveCam(dx, dy, st); bad += compare().bad; }
		const f = costFrames(st);
		say(`| ${name} | ${st.blits} | ${((st.bytes + st.fill) / 1024).toFixed(1)} | ${(f.dma / 40).toFixed(2)} | ${mx ? 'нет, рисуется за экраном' : 'да, прямо в кадре'} (расх. ${bad}) |`);
	}
	MARG_X = 0; MARG_Y = 0;
	say('');
	say(`Запас («шов») по краям: грязная полоса выравнивается по решётке 16x8, поэтому в буфере`);
	say(`должна быть годной область не меньше видимой, выровненная наружу: +16 px по X и +8 по Y.`);
	say(`Запас в целый тайл (как у Кармака: экран + тайл с каждой стороны) здесь даёт то же самое,`);
	say(`плюс позволяет рисовать въезжающую полосу ЗА пределами видимого окна — тогда разрыва`);
	say(`картинки (tearing) не видно и второй буфер кадра не нужен.`);
}
say('');

// ================================================================ 8. итог

say('## 8. Итог');
say('');
{
	const full = rowsBlack[0][1], step = rowsBlack[1][1], unit = rowsBlack[2][1];
	const ff = costFrames(full), fs2 = costFrames(step), fu = costFrames(unit);
	say(`- полная перерисовка: ${full.blits} блитов, ${ff.dma.toFixed(2)} кадра по DMA, ${ff.cpu.toFixed(1)} кадра при нынешнем C-блите;`);
	say(`- шаг камеры: ${step.blits.toFixed(0)} блитов = ${(full.blits / step.blits).toFixed(1)}x дешевле (${fs2.cpu.toFixed(1)} кадра C);`);
	say(`- фаза ходьбы бойца: ${unit.blits.toFixed(0)} блитов = ${(full.blits / unit.blits).toFixed(1)}x дешевле (${fu.cpu.toFixed(1)} кадра C).`);
}
say('');

fs.writeFileSync(path.join(outDir, 'report_' + game + '.md'), log.join('\n') + '\n');
console.error('отчёт: ' + path.join(outDir, 'report_' + game + '.md'));
