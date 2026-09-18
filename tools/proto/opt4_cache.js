// Тема 4: СКЛЕЙКА И КЕШ. Сколько работы снимает «рисовать готовыми кусками» вместо
// отдельных блитов на каждую часть клетки (16_battlescape_plan.md §8.3, §8.3a, §8.3b).
//
// Проверяются четыре уровня склейки статической местности:
//   part   — как сейчас: пол / зап. стена / сев. стена / объект, до 4 запусков DMA на клетку;
//   cell   — клетка целиком одним куском, ключ кеша = 4 номера тайлов;
//   col    — «колонка»: несколько клеток одной экранной диагонали (x+j, y+j) одной полосой;
//   blockN — квадрат N x N клеток карты одним куском (порядок художника сохраняется, см. ниже);
//   under  — подложка: все уровни ниже текущего сведены в один непрозрачный буфер (копия RAM->RAM),
//            поверх — текущий уровень; under+cell — то же, но верхний уровень склейками клеток;
//   cell+ядро — склейка клетки, разрезанная на непрозрачное ядро (RAM->RAM, 2 цикла) и рамку
//            вокруг него (BLT1, 3 цикла): проверка, что выгоднее — байты или число запусков.
//
// Приём известен: в самом OpenXcom слой местности кешировали поверхностью на тайл
// (форум openxcom.org, тема «Battlescape performance») — там выигрыш дали сэкономленные
// блиты и смены палитры; у нас узкое место другое (постановка DMA и байты переноса).
//
// Порядок художника: внутри уровня клетки можно рисовать в любом порядке возрастания d = x+y
// (клетки с равным d не перекрываются: экранный X отличается на >= 32). Отсюда:
//   * склейка клетки точна всегда (внутри клетки порядок частей сохраняется);
//   * склейка квадрата N x N точна: у всех клеток вне квадрата, чей d попадает внутрь диапазона
//     квадрата, экранный прямоугольник либо не пересекает квадрат, либо пересекает только те
//     клетки квадрата, у которых d меньше (проверяется попиксельно ниже);
//   * склейка «колонки» НЕ точна: между клетками (x,y) и (x+1,y+1) идёт d+1 из соседних
//     диагоналей, а они перекрывают полосу на 16 точек по X (тоже проверяется попиксельно);
//   * подложка точна: уровни рисуются снизу вверх целиком, поэтому «всё, что ниже» — это
//     готовый непрозрачный кусок.
//
//   node tools/proto/opt4_cache.js                       # TFTD/SEABED, 5x5 модулей
//   node tools/proto/opt4_cache.js --game UFO --terrain URBAN
//   node tools/proto/opt4_cache.js --all                 # прогон по списку террейнов обеих игр
'use strict';
const fs = require('fs');
const path = require('path');
const B = require('./battle');

function arg(name, def) {
	const i = process.argv.indexOf('--' + name);
	return i > 0 && i + 1 < process.argv.length ? process.argv[i + 1] : def;
}
const flag = (name) => process.argv.indexOf('--' + name) > 0;

// Каталог UFO в Steam называется «XCom UFO Defense» (в battle.js записан старый вариант)
for (const k of Object.keys(B.GAMES)) if (!fs.existsSync(B.GAMES[k])) {
	const alt = { UFO: 'Steam/XCom UFO Defense/XCOM', TFTD: 'Steam/X-COM Terror from the Deep/TFD' }[k];
	if (alt && fs.existsSync(alt)) B.GAMES[k] = alt;
}

const OUT = arg('out', 'tmp/proto/opt4');
const MODS = parseInt(arg('mods', '5'), 10);          // модулей 10x10 по стороне карты
const COLLEN = parseInt(arg('collen', '4'), 10);      // максимум клеток в «колонке»
const PAGE = 16384;

// Бюджеты (02 §6, 16 §8.3b)
const T_FRAME = 286720;            // тактов Z80 в кадре 50 Гц на 14 МГц
const DMA_CYCLES_FRAME = 111000;   // циклов DRAM, доступных DMA за кадр при 320x200 256c
const CYC_BLT1 = 3, CYC_COPY = 2;  // циклов DRAM на слово: BLT1 с прозрачностью / RAM->RAM
// Стоимость одного запуска блита на CPU. «Сейчас» откалибровано по §8.3b: ~380 блитов дают
// 24–32 кадра => 28*286720/380 ≈ 21 тыс. T (перебор клетки + res-таблица + постановка DMA +
// ожидание). «Асм» — цель §9.3: постановка DMA ~300 T + цикл клетки.
const T_BLIT_NOW = 21000, T_BLIT_ASM = 1500;

// ---------------------------------------------------------------- террейны

function readTerrains(game) {
	const rul = game === 'UFO' ? 'xcom1' : 'xcom2';
	const t = fs.readFileSync(path.join('REF/OpenXcom/bin/standard', rul, 'terrains.rul'), 'utf8');
	const out = {};
	for (const chunk of t.split(/\n(?=  - name:)/)) {
		const n = (chunk.match(/name:\s*(\S+)/) || [])[1];
		const ds = (chunk.match(/mapDataSets:\n((?:\s*- .*\n)+)/) || [])[1];
		if (!n || !ds) continue;
		const sets = ds.trim().split('\n').map(s => s.replace(/^\s*-\s*/, '').trim());
		const blocks = [];
		for (const m of chunk.matchAll(/- name:\s*([A-Z0-9_]+)\s*\n\s*width:\s*(\d+)\s*\n\s*length:\s*(\d+)/g))
			blocks.push({ name: m[1], w: +m[2], l: +m[3] });
		if (blocks.length) out[n] = { sets, blocks };
	}
	return out;
}

// ---------------------------------------------------------------- загрузка данных террейна

function loadTerrain(game, name) {
	const root = B.GAMES[game];
	const info = readTerrains(game)[name];
	if (!info) throw new Error('нет террейна ' + name);
	const parts = [], frames = [];
	for (const s of info.sets) {
		const dir = path.join(root, 'TERRAIN');
		const mcd = B.readMcd(fs.readFileSync(path.join(dir, s + '.MCD')));
		const pck = B.readPck(fs.readFileSync(path.join(dir, s + '.PCK')), fs.readFileSync(path.join(dir, s + '.TAB')));
		const base = frames.length;
		const mcdBase = parts.length;
		for (const r of mcd) { r.set = s; r.base = base; r.mcdBase = mcdBase; parts.push(r); }
		for (const f of pck) frames.push(f);
	}
	return { info, parts, frames, root };
}

// ГПСЧ с зерном — карта должна быть воспроизводимой
function rng(seed) { let s = seed >>> 0; return () => (s = (s * 1664525 + 1013904223) >>> 0) / 4294967296; }

// Замащивание карты блоками по сетке модулей 10x10, как fillArea генератора
function buildMap(game, ter, mods, seed) {
	const dir = path.join(B.GAMES[game], 'MAPS');
	const blocks = [];
	for (const b of ter.info.blocks) {
		const f = path.join(dir, b.name + '.MAP');
		if (!fs.existsSync(f)) continue;
		const m = B.readMap(fs.readFileSync(f));
		blocks.push({ name: b.name, m, mw: Math.ceil(m.sx / 10), mh: Math.ceil(m.sy / 10) });
	}
	if (!blocks.length) throw new Error('нет блоков карты');
	let sz = 0; for (const b of blocks) if (b.m.sz > sz) sz = b.m.sz;
	const sx = mods * 10, sy = mods * 10;
	const cells = new Uint8Array(sx * sy * sz * 4);
	const busy = new Uint8Array(mods * mods);
	const rnd = rng(seed);
	for (let my = 0; my < mods; my++)
		for (let mx = 0; mx < mods; mx++) {
			if (busy[my * mods + mx]) continue;
			const fit = blocks.filter(b => b.mw + mx <= mods && b.mh + my <= mods &&
				(() => { for (let j = 0; j < b.mh; j++) for (let i = 0; i < b.mw; i++) if (busy[(my + j) * mods + mx + i]) return false; return true; })());
			const b = fit[Math.floor(rnd() * fit.length)] || blocks[0];
			for (let j = 0; j < b.mh; j++) for (let i = 0; i < b.mw; i++) busy[(my + j) * mods + mx + i] = 1;
			const m = b.m;
			for (let z = 0; z < m.sz; z++)
				for (let y = 0; y < m.sy; y++)
					for (let x = 0; x < m.sx; x++) {
						const from = ((z * m.sy + y) * m.sx + x) * 4;
						const to = ((z * sy + my * 10 + y) * sx + mx * 10 + x) * 4;
						for (let k = 0; k < 4; k++) cells[to + k] = m.cells[from + k];
					}
		}
	return { sx, sy, sz, cells };
}

// ---------------------------------------------------------------- спрайты и склейки

// Всё, что зависит от набора данных, собирается в объект — чтобы --all прогонял террейны подряд
function makeCtx(game, terName) {
	const ter = loadTerrain(game, terName);
	const map = buildMap(game, ter, MODS, 12345);
	const { parts, frames } = ter;

	// обрезка кадра до непрозрачного прямоугольника с выравниванием DMA (X и ширина — чётные)
	const spr = [];   // spr[id] = {x,y,w,h,px} или null; id — номер как в .MAP (1-based)
	let maxOff = 0;
	for (let id = 1; id <= parts.length; id++) {
		const r = parts[id - 1];
		const img = frames[r.base + r.frame[0]];
		if (!img) { spr[id] = null; continue; }
		let x0 = 99, x1 = -1, y0 = 99, y1 = -1;
		for (let y = 0; y < B.TILE_H; y++)
			for (let x = 0; x < B.TILE_W; x++)
				if (img[y * B.TILE_W + x]) {
					if (x < x0) x0 = x; if (x > x1) x1 = x;
					if (y < y0) y0 = y; if (y > y1) y1 = y;
				}
		if (x1 < 0) { spr[id] = null; continue; }
		x0 &= ~1; const w = ((x1 - x0 + 2) & ~1), h = y1 - y0 + 1;
		const px = new Uint8Array(w * h);
		for (let y = 0; y < h; y++) for (let x = 0; x < w; x++) px[y * w + x] = img[(y0 + y) * B.TILE_W + x0 + x];
		const anim = r.frame.some(f => f !== r.frame[0]);
		spr[id] = { x: x0, y: y0, w, h, px, yoff: r.pLevel, anim, rec: r };
		if (r.pLevel > maxOff) maxOff = r.pLevel;
	}

	const cellParts = (x, y, z) => {
		const o = ((z * map.sy + y) * map.sx + x) * 4;
		return [map.cells[o], map.cells[o + 1], map.cells[o + 2], map.cells[o + 3]];
	};
	return { game, terName, ter, map, parts, frames, spr, maxOff, cellParts };
}

// Рисование обрезанного кадра в произвольный буфер (прозрачность — 0)
function put(dst, dw, dh, s, px, py) {
	if (!s) return;
	const X = px + s.x, Y = py + s.y;
	for (let y = 0; y < s.h; y++) {
		const ty = Y + y; if (ty < 0 || ty >= dh) continue;
		for (let x = 0; x < s.w; x++) {
			const v = s.px[y * s.w + x]; if (!v) continue;
			const tx = X + x; if (tx < 0 || tx >= dw) continue;
			dst[ty * dw + tx] = v;
		}
	}
}

// Обрезка собранной склейки до непрозрачного прямоугольника, с выравниванием DMA
function crop(buf, w, h) {
	let x0 = w, x1 = -1, y0 = h, y1 = -1;
	for (let y = 0; y < h; y++)
		for (let x = 0; x < w; x++)
			if (buf[y * w + x]) {
				if (x < x0) x0 = x; if (x > x1) x1 = x;
				if (y < y0) y0 = y; if (y > y1) y1 = y;
			}
	if (x1 < 0) return null;
	x0 &= ~1; const cw = ((x1 - x0 + 2) & ~1), ch = y1 - y0 + 1;
	const px = new Uint8Array(cw * ch);
	for (let y = 0; y < ch; y++) for (let x = 0; x < cw; x++) px[y * cw + x] = buf[(y0 + y) * w + x0 + x];
	return { x: x0, y: y0, w: cw, h: ch, px };
}

// Склейка одной клетки: канва 32 x (40 + maxOff), начало клетки — (0, maxOff)
function glueCell(ctx, p) {
	const W = B.TILE_W, H = B.TILE_H + ctx.maxOff;
	const buf = new Uint8Array(W * H);
	for (let k = 0; k < 4; k++) {
		const s = ctx.spr[p[k]];
		if (!s) continue;
		put(buf, W, H, s, 0, ctx.maxOff - s.yoff);
	}
	const c = crop(buf, W, H);
	if (c) { c.x -= 0; c.y -= ctx.maxOff; }   // смещение относительно начала клетки
	return c;
}

// Склейка «колонки» из n клеток одной диагонали: (x+j, y+j), шаг по экрану (0, +16)
function glueCol(ctx, list) {
	const n = list.length;
	const W = B.TILE_W, H = 16 * (n - 1) + B.TILE_H + ctx.maxOff;
	const buf = new Uint8Array(W * H);
	for (let j = 0; j < n; j++)
		for (let k = 0; k < 4; k++) {
			const s = ctx.spr[list[j][k]];
			if (!s) continue;
			put(buf, W, H, s, 0, ctx.maxOff + 16 * j - s.yoff);
		}
	const c = crop(buf, W, H);
	if (c) c.y -= ctx.maxOff;
	return c;
}

// Склейка квадрата N x N клеток: клетка (i,j) внутри квадрата -> экран (16(i-j), 8(i+j))
function glueBlock(ctx, n, list) {
	const W = B.TILE_W * n, H = 16 * (n - 1) + B.TILE_H + ctx.maxOff;
	const buf = new Uint8Array(W * H);
	const ox = 16 * (n - 1), oy = ctx.maxOff;
	for (let j = 0; j < n; j++)
		for (let i = 0; i < n; i++) {
			const p = list[j * n + i];
			for (let k = 0; k < 4; k++) {
				const s = ctx.spr[p[k]];
				if (!s) continue;
				put(buf, W, H, s, ox + 16 * (i - j), oy + 8 * (i + j) - s.yoff);
			}
		}
	const c = crop(buf, W, H);
	if (c) { c.x -= ox; c.y -= oy; }
	return c;
}

// ---------------------------------------------------------------- вид

const screenOf = (x, y, z, ox, oy) => ({ sx: (x - y) * B.HALF_W + ox, sy: (x + y) * B.QUART - z * B.LEVEL_H + oy });

// Клетки, попадающие в окно (отсев только по экрану — решение §8.3a п.2)
function visibleCells(ctx, level, ox, oy) {
	const { map } = ctx;
	const out = [];
	for (let z = 0; z <= level; z++)
		for (let d = 0; d <= map.sx + map.sy - 2; d++)
			for (let x = Math.max(0, d - map.sy + 1); x <= Math.min(map.sx - 1, d); x++) {
				const y = d - x;
				const s = screenOf(x, y, z, ox, oy);
				if (s.sx <= -B.TILE_W || s.sx >= B.VIEW_W) continue;
				if (s.sy <= -(B.TILE_H + ctx.maxOff) || s.sy >= B.VIEW_H) continue;
				const p = ctx.cellParts(x, y, z);
				if (!p[0] && !p[1] && !p[2] && !p[3]) continue;
				out.push({ x, y, z, d, p, s });
			}
	return out;   // уже в порядке художника: z, затем d
}

function newBuf() { return new Uint8Array(B.VIEW_W * B.VIEW_H); }
function blitTo(buf, s, px, py, st) {
	if (!s) return;
	st.blits++; st.bytes += s.w * s.h;
	put(buf, B.VIEW_W, B.VIEW_H, s, px, py);
}

// эталон: по одному блиту на часть клетки (как сейчас в движке)
function renderParts(ctx, cells) {
	const buf = newBuf(), st = { blits: 0, bytes: 0 };
	for (const c of cells)
		for (let k = 0; k < 4; k++) {
			const s = ctx.spr[c.p[k]];
			if (!s) continue;
			blitTo(buf, s, c.s.sx, c.s.sy - s.yoff, st);
		}
	return { buf, st };
}

function sigKey(p) { return (p[0] << 24 | p[1] << 16 | p[2] << 8 | p[3]) >>> 0; }

// склейка клетки, кеш по сигнатуре
function renderCells(ctx, cells, cache) {
	const buf = newBuf(), st = { blits: 0, bytes: 0, hit: 0, miss: 0 };
	for (const c of cells) {
		const k = sigKey(c.p);
		let g = cache.get(k);
		if (g === undefined) { g = glueCell(ctx, c.p); cache.set(k, g); st.miss++; } else st.hit++;
		if (!g) continue;
		blitTo(buf, g, c.s.sx, c.s.sy, st);
	}
	return { buf, st };
}

// «колонка»: пробегаем клетки уровня по диагоналям экрана, склеиваем подряд идущие (x+j, y+j)
// Наибольший полностью непрозрачный прямоугольник внутри склейки (координаты и ширина — чётные,
// как требует DMA). Классический «наибольший прямоугольник в гистограмме» по строкам.
function opaqueCore(g) {
	const { w, h, px } = g;
	const up = new Int16Array(w);
	let best = null, bestA = 0;
	for (let y = 0; y < h; y++) {
		for (let x = 0; x < w; x++) up[x] = px[y * w + x] ? up[x] + 1 : 0;
		const st = [];
		for (let x = 0; x <= w; x++) {
			const cur = x < w ? up[x] : 0;
			while (st.length && up[st[st.length - 1]] >= cur) {
				const top = st.pop();
				const left = st.length ? st[st.length - 1] + 1 : 0;
				let x0 = left, x1 = x;                       // [x0, x1)
				x0 = (x0 + 1) & ~1; x1 &= ~1;                // выравнивание DMA
				const ww = x1 - x0, hh = up[top];
				if (ww > 0 && ww * hh > bestA) { bestA = ww * hh; best = { x: x0, y: y - hh + 1, w: ww, h: hh }; }
			}
			st.push(x);
		}
	}
	return best;
}

// Та же склейка клетки, но непрозрачное ядро переносится быстрым RAM->RAM, а рамка вокруг —
// четырьмя BLT1. Считаем оба потока байт отдельно.
function renderCellsSplit(ctx, cells, cache, cores) {
	const st = { blits: 0, bytes: 0, copy: 0, full: 0 };
	for (const c of cells) {
		const k = sigKey(c.p);
		let g = cache.get(k);
		if (g === undefined) { g = glueCell(ctx, c.p); cache.set(k, g); }
		if (!g) continue;
		let core = cores.get(k);
		if (core === undefined) { core = opaqueCore(g); cores.set(k, core); }
		if (!core) { st.blits++; st.bytes += g.w * g.h; continue; }
		if (core.w === g.w && core.h === g.h) { st.blits++; st.copy += g.w * g.h; st.full++; continue; }
		st.blits++; st.copy += core.w * core.h;
		// рамка: верх, низ (во всю ширину), лево, право (в строках ядра)
		const bands = [
			{ w: g.w, h: core.y },
			{ w: g.w, h: g.h - core.y - core.h },
			{ w: core.x, h: core.h },
			{ w: g.w - core.x - core.w, h: core.h },
		];
		for (const b of bands) if (b.w > 0 && b.h > 0) { st.blits++; st.bytes += b.w * b.h; }
	}
	return { st };
}

function renderCols(ctx, cells, cache, maxLen) {
	const buf = newBuf(), st = { blits: 0, bytes: 0, hit: 0, miss: 0, runs: 0 };
	const have = new Map();
	for (const c of cells) have.set((c.z * 4096) + c.y * 64 + c.x, c);
	const used = new Set();
	const order = [];
	for (const c of cells) {
		const id = (c.z * 4096) + c.y * 64 + c.x;
		if (used.has(id)) continue;
		const run = [c]; used.add(id);
		for (let j = 1; j < maxLen; j++) {
			const nid = (c.z * 4096) + (c.y + j) * 64 + (c.x + j);
			const nc = have.get(nid);
			if (!nc || used.has(nid)) break;
			run.push(nc); used.add(nid);
		}
		order.push(run);
	}
	order.sort((a, b) => (a[0].z - b[0].z) || (a[0].d - b[0].d));
	for (const run of order) {
		st.runs++;
		const key = run.map(c => sigKey(c.p).toString(36)).join('.');
		let g = cache.get(key);
		if (g === undefined) { g = glueCol(ctx, run.map(c => c.p)); cache.set(key, g); st.miss++; } else st.hit++;
		if (!g) continue;
		blitTo(buf, g, run[0].s.sx, run[0].s.sy, st);
	}
	return { buf, st };
}

// квадрат N x N, привязанный к сетке карты
function renderBlocks(ctx, cells, cache, n, level, ox, oy) {
	const buf = newBuf(), st = { blits: 0, bytes: 0, hit: 0, miss: 0 };
	const seen = new Map();   // ключ квадрата -> true, если внутри есть видимая клетка
	for (const c of cells) {
		const bx = Math.floor(c.x / n), by = Math.floor(c.y / n);
		seen.set(c.z * 65536 + by * 256 + bx, true);
	}
	const list = [...seen.keys()].map(k => ({ z: (k / 65536) | 0, by: ((k >> 8) & 255), bx: (k & 255) }));
	list.sort((a, b) => (a.z - b.z) || ((a.bx + a.by) - (b.bx + b.by)));
	for (const b of list) {
		const cellsIn = [];
		let empty = true;
		for (let j = 0; j < n; j++)
			for (let i = 0; i < n; i++) {
				const x = b.bx * n + i, y = b.by * n + j;
				const p = (x < ctx.map.sx && y < ctx.map.sy) ? ctx.cellParts(x, y, b.z) : [0, 0, 0, 0];
				if (p[0] || p[1] || p[2] || p[3]) empty = false;
				cellsIn.push(p);
			}
		if (empty) continue;
		const key = b.z * 0 + cellsIn.map(p => sigKey(p).toString(36)).join('.');
		let g = cache.get(key);
		if (g === undefined) { g = glueBlock(ctx, n, cellsIn); cache.set(key, g); st.miss++; } else st.hit++;
		if (!g) continue;
		const s = screenOf(b.bx * n, b.by * n, b.z, ox, oy);
		blitTo(buf, g, s.sx, s.sy, st);
	}
	return { buf, st };
}

// подложка: уровни < level одним непрозрачным куском, поверх — текущий уровень по частям
function renderUnder(ctx, cells, level, cache) {
	const buf = newBuf(), st = { blits: 0, bytes: 0, copy: 0, under: { blits: 0, bytes: 0 } };
	const low = cells.filter(c => c.z < level), top = cells.filter(c => c.z === level);
	const r = cache ? renderCells(ctx, low, cache) : renderParts(ctx, low);
	buf.set(r.buf);
	st.under = r.st;
	if (low.length) { st.blits += 1; st.copy += B.VIEW_W * B.VIEW_H; }   // копия готовой подложки RAM->RAM
	if (cache) {
		for (const c of top) {
			const k = sigKey(c.p);
			let g = cache.get(k);
			if (g === undefined) { g = glueCell(ctx, c.p); cache.set(k, g); }
			if (g) blitTo(buf, g, c.s.sx, c.s.sy, st);
		}
	} else {
		for (const c of top)
			for (let k = 0; k < 4; k++) {
				const s = ctx.spr[c.p[k]];
				if (!s) continue;
				blitTo(buf, s, c.s.sx, c.s.sy - s.yoff, st);
			}
	}
	return { buf, st };
}

function diff(a, b) {
	let n = 0;
	for (let i = 0; i < a.length; i++) if (a[i] !== b[i]) n++;
	return n;
}

// ---------------------------------------------------------------- вывод PNG

function savePng(file, buf, w, h, pal) {
	const rgb = Buffer.alloc(w * h * 3);
	for (let i = 0; i < w * h; i++) { const c = pal[buf[i]]; rgb[i * 3] = c[0]; rgb[i * 3 + 1] = c[1]; rgb[i * 3 + 2] = c[2]; }
	B.writePng(file, w, h, rgb);
}

// лист самых частых склеек клетки
function saveSheet(file, ctx, top, pal) {
	const CW = 40, CH = 56, COLS = 16;
	const rows = Math.ceil(top.length / COLS);
	const w = CW * COLS, h = CH * rows;
	const buf = new Uint8Array(w * h);
	top.forEach((t, i) => {
		const g = glueCell(ctx, t.p);
		if (!g) return;
		const bx = (i % COLS) * CW + 4, by = ((i / COLS) | 0) * CH + 4 + ctx.maxOff;
		put(buf, w, h, g, bx, by);
	});
	savePng(file, buf, w, h, pal);
}

// ---------------------------------------------------------------- прогон одного террейна

function run(game, terName, report) {
	const ctx = makeCtx(game, terName);
	const { map } = ctx;
	const level = Math.min(parseInt(arg('level', '99'), 10), map.sz - 1);
	const pal = B.readPalette(game, 6);

	// --- 1. сигнатуры по всей карте
	const cnt = new Map();
	let nonEmpty = 0, animCells = 0, doorCells = 0, destrCells = 0, lateObj = 0;
	for (let z = 0; z < map.sz; z++)
		for (let y = 0; y < map.sy; y++)
			for (let x = 0; x < map.sx; x++) {
				const p = ctx.cellParts(x, y, z);
				if (!p[0] && !p[1] && !p[2] && !p[3]) continue;
				nonEmpty++;
				const k = sigKey(p);
				const e = cnt.get(k);
				if (e) e.n++; else cnt.set(k, { n: 1, p });
				let anim = false, door = false, destr = false;
				for (let i = 0; i < 4; i++) {
					const s = ctx.spr[p[i]]; if (!s) continue;
					if (s.anim) anim = true;
					if (s.rec.door || s.rec.ufoDoor) door = true;
					if (s.rec.armor < 255) destr = true;
					if (i === 3 && s.rec.bigWall >= 6 && s.rec.bigWall !== 9) lateObj++;
				}
				if (anim) animCells++;
				if (door) doorCells++;
				if (destr) destrCells++;
			}
	const sigs = [...cnt.values()].sort((a, b) => b.n - a.n);

	// размер кеша клеточных склеек: все сигнатуры карты
	let allBytes = 0, glueOk = 0;
	for (const s of sigs) { const g = glueCell(ctx, s.p); if (g) { allBytes += g.w * g.h + 6; glueOk++; } }

	// --- 2. кадры: камера в нескольких местах
	const cams = [];
	const step = Math.max(4, Math.floor(map.sx / 6));
	for (let cy = step; cy < map.sy - step; cy += step)
		for (let cx = step; cx < map.sx - step; cx += step) cams.push([cx, cy]);

	const cacheCell = new Map(), cacheCol = new Map(), cacheB2 = new Map(), cacheB3 = new Map(), cores = new Map();
	let fullOpaque = 0;
	const acc = {};
	const add = (name, st, cells) => {
		const a = acc[name] || (acc[name] = { blits: 0, bytes: 0, cells: 0, hit: 0, miss: 0, n: 0, sigFrame: 0 });
		a.blits += st.blits; a.bytes += st.bytes; a.copy = (a.copy || 0) + (st.copy || 0); a.cells += cells; a.n++;
		a.hit += st.hit || 0; a.miss += st.miss || 0;
	};
	let firstShots = null, sigInFrame = 0, cellsInFrame = 0, badCol = 0, badBlock2 = 0, badBlock3 = 0, badUnder = 0, badCell = 0, badUnderCell = 0;

	for (const [cx, cy] of cams) {
		const ox = Math.round(B.VIEW_W / 2 - (cx - cy) * B.HALF_W);
		const oy = Math.round(B.VIEW_H / 2 - ((cx + cy) * B.QUART - level * B.LEVEL_H));
		const cells = visibleCells(ctx, level, ox, oy);
		const uniq = new Set(cells.map(c => sigKey(c.p)));
		sigInFrame += uniq.size; cellsInFrame += cells.length;

		const rp = renderParts(ctx, cells); add('part', rp.st, cells.length);
		const rc = renderCells(ctx, cells, cacheCell); add('cell', rc.st, cells.length);
		const rs = renderCellsSplit(ctx, cells, cacheCell, cores); add('cellsplit', rs.st, cells.length);
		fullOpaque += rs.st.full;
		const rl = renderCols(ctx, cells, cacheCol, COLLEN); add('col', rl.st, cells.length);
		const r2 = renderBlocks(ctx, cells, cacheB2, 2, level, ox, oy); add('block2', r2.st, cells.length);
		const r3 = renderBlocks(ctx, cells, cacheB3, 3, level, ox, oy); add('block3', r3.st, cells.length);
		const ru = renderUnder(ctx, cells, level); add('under', ru.st, cells.length);
		const ruc = renderUnder(ctx, cells, level, cacheCell); add('undercell', ruc.st, cells.length);
		badUnderCell += diff(rp.buf, ruc.buf);
		acc.under.underBlits = (acc.under.underBlits || 0) + ru.st.under.blits;
		acc.under.underBytes = (acc.under.underBytes || 0) + ru.st.under.bytes;

		badCell += diff(rp.buf, rc.buf);
		badCol += diff(rp.buf, rl.buf);
		badBlock2 += diff(rp.buf, r2.buf);
		badBlock3 += diff(rp.buf, r3.buf);
		badUnder += diff(rp.buf, ru.buf);
		if (!firstShots) firstShots = { part: rp.buf.slice(), cell: rc.buf.slice(), col: rl.buf.slice(), b2: r2.buf.slice() };
	}

	// --- 1б. что станет с сигнатурой после разрушения: часть заменяется на dieMCD
	// (Tile.cpp:466-494). Считаем, попадёт ли новая сигнатура в уже собранный кеш карты.
	const destr = { tries: 0, inCache: 0, newSig: new Set() };
	{
		const have = new Set(cnt.keys());
		for (const s of sigs) {
			for (let i = 0; i < 4; i++) {
				const id = s.p[i]; if (!id) continue;
				const rec = ctx.parts[id - 1]; if (!rec || rec.armor >= 255) continue;
				const die = rec.dieMcd;
				const q = s.p.slice();
				// dieMCD — индекс внутри своего набора (Tile.cpp:466-494); сквозной номер = база набора + die + 1
				q[i] = die ? rec.mcdBase + die + 1 : 0;
				const k = sigKey(q);
				destr.tries += s.n;
				if (have.has(k)) destr.inCache += s.n; else destr.newSig.add(k);
			}
		}
	}

	// --- 2б. то же по уровням камеры (в бою камера чаще стоит на нижнем ярусе)
	const byLevel = [];
	for (let L = 0; L < map.sz; L++) {
		const a = { part: { b: 0, by: 0 }, cell: { b: 0, by: 0 }, under: { b: 0, by: 0, ub: 0, uby: 0 }, cells: 0, n: 0 };
		for (const [cx, cy] of cams) {
			const ox = Math.round(B.VIEW_W / 2 - (cx - cy) * B.HALF_W);
			const oy = Math.round(B.VIEW_H / 2 - ((cx + cy) * B.QUART - L * B.LEVEL_H));
			const cells = visibleCells(ctx, L, ox, oy);
			const rp = renderParts(ctx, cells);
			const rc = renderCells(ctx, cells, cacheCell);
			const ru = renderUnder(ctx, cells, L);
			a.part.b += rp.st.blits; a.part.by += rp.st.bytes;
			a.cell.b += rc.st.blits; a.cell.by += rc.st.bytes;
			a.under.b += ru.st.blits; a.under.by += ru.st.bytes + (ru.st.copy || 0);
			a.under.ub += ru.st.under.blits; a.under.uby += ru.st.under.bytes;
			a.cells += cells.length; a.n++;
		}
		byLevel.push(a);
	}

	// --- 3. «грязный прямоугольник»: перерисовка местности под шагнувшим бойцом.
	// Считаем клетки, попадающие в квадрат 3x3 карты вокруг точки, и стоимость их местности
	// тремя способами: по частям, склейками клетки, копией готовой подложки.
	const dirty = { part: { b: 0, by: 0 }, cell: { b: 0, by: 0 }, under: { b: 0, by: 0 }, n: 0 };
	{
		// уровень, где стоят бойцы, — самый заполненный
		let ground = 0, best = -1;
		for (let z = 0; z < map.sz; z++) {
			let c = 0;
			for (let y = 0; y < map.sy; y++) for (let x = 0; x < map.sx; x++) { const p = ctx.cellParts(x, y, z); if (p[0] || p[1] || p[2] || p[3]) c++; }
			if (c > best) { best = c; ground = z; }
		}
		dirty.level = ground;
		const level = ground;
		const ox = Math.round(B.VIEW_W / 2), oy = Math.round(B.VIEW_H / 2);
		const rnd = rng(777);
		for (let t = 0; t < 200; t++) {
			const ux = 2 + Math.floor(rnd() * (map.sx - 4)), uy = 2 + Math.floor(rnd() * (map.sy - 4));
			let bp = 0, byp = 0, bc = 0, byc = 0;
			let x0 = 1e9, x1 = -1e9, y0 = 1e9, y1 = -1e9;
			for (let dy = -1; dy <= 1; dy++)
				for (let dx = -1; dx <= 1; dx++) {
					const x = ux + dx, y = uy + dy;
					const p = ctx.cellParts(x, y, level);
					if (!p[0] && !p[1] && !p[2] && !p[3]) continue;
					const s = screenOf(x, y, level, ox, oy);
					for (let k = 0; k < 4; k++) { const sp = ctx.spr[p[k]]; if (!sp) continue; bp++; byp += sp.w * sp.h; }
					const g = glueCell(ctx, p);
					if (g) {
						bc++; byc += g.w * g.h;
						x0 = Math.min(x0, s.sx + g.x); x1 = Math.max(x1, s.sx + g.x + g.w);
						y0 = Math.min(y0, s.sy + g.y); y1 = Math.max(y1, s.sy + g.y + g.h);
					}
				}
			if (x1 < x0) continue;
			dirty.n++;
			dirty.part.b += bp; dirty.part.by += byp;
			dirty.cell.b += bc; dirty.cell.by += byc;
			dirty.under.b += 1; dirty.under.by += (x1 - x0) * (y1 - y0);
		}
	}

	// --- 4. окупаемость склейки клетки: сборка = P блитов + FILL канвы, показ = 1 блит
	const pay = (() => {
		const a0 = acc.part, a1 = acc.cell, m = cams.length;
		const cellsF = cellsInFrame / m;
		const partsPerCell = (a0.blits / m) / cellsF;
		const bytesPart = (a0.bytes / m) / cellsF;
		const bytesGlue = (a1.bytes / m) / cellsF;
		const canvas = B.TILE_W * (B.TILE_H + ctx.maxOff);
		// сборка: P блитов BLT1 в канву + FILL канвы (1 цикл/слово); показ — 1 блит
		const buildDma = bytesPart / 2 * CYC_BLT1 + canvas / 2 * 1;
		const useDma = bytesGlue / 2 * CYC_BLT1, usePart = bytesPart / 2 * CYC_BLT1;
		const saveDma = usePart - useDma;
		// окупаемость по CPU не зависит от цены блита: (P+1)/(P-1)
		const nCpu = partsPerCell > 1 ? (partsPerCell + 1) / (partsPerCell - 1) : Infinity;
		// предсборка всего кеша карты при загрузке
		const preCpuNow = glueOk * (partsPerCell + 1) * T_BLIT_NOW, preCpuAsm = glueOk * (partsPerCell + 1) * T_BLIT_ASM;
		return {
			partsPerCell, bytesPart, bytesGlue, buildDma,
			buildCpuNow: (partsPerCell + 1) * T_BLIT_NOW, buildCpuAsm: (partsPerCell + 1) * T_BLIT_ASM,
			nCpu, nDma: saveDma > 0 ? buildDma / saveDma : Infinity,
			usesPerFrame: cellsF / (sigInFrame / m),
			preFramesNow: preCpuNow / T_FRAME, preFramesAsm: preCpuAsm / T_FRAME,
			preDmaFrames: glueOk * buildDma / DMA_CYCLES_FRAME,
		};
	})();

	const n = cams.length;
	const R = {
		dirty, pay, byLevel, destr, cores, cacheCellMap: cacheCell, cacheColMap: cacheCol, cacheB2Map: cacheB2, cacheB3Map: cacheB3, fullOpaque: fullOpaque / cams.length,
		game, terName, mapSize: `${map.sx}x${map.sy}x${map.sz}`, level,
		nonEmpty, sigTotal: sigs.length, sigTop: sigs.slice(0, 20),
		animCells, doorCells, destrCells, lateObj,
		allBytes, glueOk,
		cellsFrame: cellsInFrame / n, sigFrame: sigInFrame / n,
		cams: n, acc,
		bad: { cell: badCell / n, col: badCol / n, block2: badBlock2 / n, block3: badBlock3 / n, under: badUnder / n, 'under+cell': badUnderCell / n },
		cacheSizes: {
			cell: [...cacheCell.values()].reduce((s, g) => s + (g ? g.w * g.h + 6 : 0), 0),
			col: [...cacheCol.values()].reduce((s, g) => s + (g ? g.w * g.h + 8 : 0), 0),
			block2: [...cacheB2.values()].reduce((s, g) => s + (g ? g.w * g.h + 8 : 0), 0),
			block3: [...cacheB3.values()].reduce((s, g) => s + (g ? g.w * g.h + 8 : 0), 0),
			cellN: cacheCell.size, colN: cacheCol.size, b2N: cacheB2.size, b3N: cacheB3.size,
		},
	};

	if (report) {
		fs.mkdirSync(OUT, { recursive: true });
		const tag = `${game}_${terName}`;
		savePng(path.join(OUT, `${tag}_part.png`), firstShots.part, B.VIEW_W, B.VIEW_H, pal);
		savePng(path.join(OUT, `${tag}_cell.png`), firstShots.cell, B.VIEW_W, B.VIEW_H, pal);
		savePng(path.join(OUT, `${tag}_col.png`), firstShots.col, B.VIEW_W, B.VIEW_H, pal);
		savePng(path.join(OUT, `${tag}_block2.png`), firstShots.b2, B.VIEW_W, B.VIEW_H, pal);
		// карта расхождений склейки «колонкой»: верный кадр приглушён, расхождения — красным
		{
			const rgb = Buffer.alloc(B.VIEW_W * B.VIEW_H * 3);
			for (let i = 0; i < B.VIEW_W * B.VIEW_H; i++) {
				const c = pal[firstShots.part[i]];
				if (firstShots.part[i] !== firstShots.col[i]) { rgb[i * 3] = 255; rgb[i * 3 + 1] = 0; rgb[i * 3 + 2] = 0; }
				else { rgb[i * 3] = c[0] >> 2; rgb[i * 3 + 1] = c[1] >> 2; rgb[i * 3 + 2] = c[2] >> 2; }
			}
			B.writePng(path.join(OUT, `${tag}_col_diff.png`), B.VIEW_W, B.VIEW_H, rgb);
		}
		saveSheet(path.join(OUT, `${tag}_sheet.png`), ctx, sigs.slice(0, 64), pal);
	}
	return R;
}

// ---------------------------------------------------------------- печать

function frames(blits, bytes, cyc, tblit) {
	const cpu = blits * tblit / T_FRAME;
	const dma = bytes / 2 * cyc / DMA_CYCLES_FRAME;
	return { cpu, dma, f: Math.max(cpu, dma) };
}

function printOne(R) {
	const a = R.acc, n = R.cams;
	console.log(`\n=== ${R.game} / ${R.terName}: карта ${R.mapSize}, уровень ${R.level}, камер ${n}`);
	console.log(`клеток непустых на карте ${R.nonEmpty}, уникальных сигнатур ${R.sigTotal} ` +
		`(${(100 * R.sigTotal / R.nonEmpty).toFixed(1)} % — столько же раз пришлось бы собирать склейку)`);
	console.log(`в кадре клеток ${R.cellsFrame.toFixed(0)}, из них уникальных сигнатур ${R.sigFrame.toFixed(0)} ` +
		`=> из кеша берётся ${(100 * (1 - R.sigFrame / R.cellsFrame)).toFixed(0)} % клеток кадра`);
	console.log(`кеш всех склеек карты: ${(R.allBytes / 1024).toFixed(1)} КБ = ${(R.allBytes / PAGE).toFixed(2)} стр. (${R.glueOk} непустых)`);
	console.log(`изменяемость: анимированных клеток ${R.animCells} (${(100 * R.animCells / R.nonEmpty).toFixed(1)} %), ` +
		`с дверями ${R.doorCells} (${(100 * R.doorCells / R.nonEmpty).toFixed(1)} %), ` +
		`разрушаемых ${R.destrCells} (${(100 * R.destrCells / R.nonEmpty).toFixed(1)} %), ` +
		`объектов «позднего» bigWall ${R.lateObj}`);
	console.log('');
	console.log('способ      блитов  байт/кадр  DMA цикл  кадров(сейчас)  кадров(асм)  промах%  ошибок точек');
	const rows = [['part', 'part', CYC_BLT1], ['cell', 'cell', CYC_BLT1], ['col', 'col', CYC_BLT1],
	['block2', 'block2', CYC_BLT1], ['block3', 'block3', CYC_BLT1],
	['under', 'under', CYC_BLT1], ['under+cell', 'undercell', CYC_BLT1], ['cell+ядро', 'cellsplit', CYC_BLT1]];
	for (const [name, key, cyc] of rows) {
		const s = a[key]; if (!s) continue;
		const bl = s.blits / n, by = (s.bytes + (s.copy || 0)) / n;
		const cycles = (s.bytes / n) / 2 * cyc + ((s.copy || 0) / n) / 2 * CYC_COPY;
		const fNow = { f: Math.max(bl * T_BLIT_NOW / T_FRAME, cycles / DMA_CYCLES_FRAME) };
		const fAsm = { f: Math.max(bl * T_BLIT_ASM / T_FRAME, cycles / DMA_CYCLES_FRAME) };
		const miss = s.miss + s.hit ? (100 * s.miss / (s.miss + s.hit)).toFixed(1) : '—';
		const bad = R.bad[name] !== undefined ? R.bad[name].toFixed(0) : '—';
		console.log(`${name.padEnd(10)} ${bl.toFixed(0).padStart(6)} ${by.toFixed(0).padStart(10)} ` +
			`${cycles.toFixed(0).padStart(9)} ${fNow.f.toFixed(1).padStart(15)} ${fAsm.f.toFixed(1).padStart(12)} ` +
			`${String(miss).padStart(8)} ${String(bad).padStart(13)}`);
	}
	const u = a.under;
	console.log(`  (под «under» — 1 копия окна 320x144 + текущий уровень; сборка самой подложки: ` +
		`${(u.underBlits / n).toFixed(0)} блитов, ${(u.underBytes / n / 1024).toFixed(1)} КБ — один раз на этаж/сдвиг)`);
	console.log('');
	console.log(`кеши за весь прогон (${n} камер): клетка ${R.cacheSizes.cellN} шт / ${(R.cacheSizes.cell / 1024).toFixed(1)} КБ ` +
		`(${(R.cacheSizes.cell / PAGE).toFixed(2)} стр.), колонка ${R.cacheSizes.colN} / ${(R.cacheSizes.col / 1024).toFixed(1)} КБ ` +
		`(${(R.cacheSizes.col / PAGE).toFixed(2)} стр.), блок2 ${R.cacheSizes.b2N} / ${(R.cacheSizes.block2 / 1024).toFixed(1)} КБ ` +
		`(${(R.cacheSizes.block2 / PAGE).toFixed(2)} стр.), блок3 ${R.cacheSizes.b3N} / ${(R.cacheSizes.block3 / 1024).toFixed(1)} КБ ` +
		`(${(R.cacheSizes.block3 / PAGE).toFixed(2)} стр.)`);
	{
		let area = 0, core = 0, fullN = 0, n2 = 0;
		for (const [k, g] of R.cacheCellMap) {
			if (!g) continue;
			const c = R.cores.get(k); n2++;
			area += g.w * g.h;
			if (c) { core += c.w * c.h; if (c.w === g.w && c.h === g.h) fullN++; }
		}
		console.log(`непрозрачность склеек: ядро клетки занимает ${(100 * core / area).toFixed(0)} % площади; ` +
			`полностью непрозрачных склеек ${fullN} из ${n2} (${(100 * fullN / n2).toFixed(0)} %), ` +
			`в кадре таких ${R.fullOpaque.toFixed(0)} из ${R.cellsFrame.toFixed(0)} клеток`);
		const share = (m) => {
			let a = 0, c = 0;
			for (const g of m.values()) { if (!g) continue; a += g.w * g.h; const k = opaqueCore(g); if (k) c += k.w * k.h; }
			return a ? (100 * c / a).toFixed(0) + ' %' : '—';
		};
		console.log(`  та же доля у других склеек: колонка ${share(R.cacheColMap)}, блок2 ${share(R.cacheB2Map)}, ` +
			`блок3 ${share(R.cacheB3Map)}; подложка этажа — 100 % (сплошной прямоугольник)`);
	}
	console.log(`кеш клеток с тенью: 1 уровень ${(R.allBytes / 1024).toFixed(0)} КБ, 5 уровней (§8.4) ` +
		`${(5 * R.allBytes / 1024).toFixed(0)} КБ = ${(5 * R.allBytes / PAGE).toFixed(1)} стр., ` +
		`16 уровней ${(16 * R.allBytes / PAGE).toFixed(1)} стр.`);

	const p = R.pay;
	console.log(`\nокупаемость склейки клетки: частей на клетку ${p.partsPerCell.toFixed(2)}, ` +
		`байт по частям ${p.bytesPart.toFixed(0)} -> склейкой ${p.bytesGlue.toFixed(0)} ` +
		`(${(100 * (1 - p.bytesGlue / p.bytesPart)).toFixed(0)} % меньше)`);
	console.log(`  сборка одной склейки: ${p.buildDma.toFixed(0)} циклов DMA + ${p.buildCpuNow.toFixed(0)} T CPU (сейчас) / ` +
		`${p.buildCpuAsm.toFixed(0)} T (асм); окупается за ${p.nCpu.toFixed(1)} показов по CPU, ${p.nDma.toFixed(1)} по DMA`);
	console.log(`  средний показ одной склейки в кадре: ${p.usesPerFrame.toFixed(1)} раз => окупается внутри одного вида`);
	console.log(`  предсборка всего кеша карты при загрузке: ${p.preFramesNow.toFixed(1)} кадров CPU (сейчас) / ` +
		`${p.preFramesAsm.toFixed(1)} (асм), ${p.preDmaFrames.toFixed(1)} кадров DMA`);

	console.log('\nпо уровням камеры (блитов / КБ, среднее по камерам):');
	console.log('уровень  клеток   part блитов/КБ    cell блитов/КБ    under блитов/КБ   сборка подложки');
	R.byLevel.forEach((a, L) => {
		const m = a.n;
		console.log(String(L).padEnd(8) + (a.cells / m).toFixed(0).padStart(6) +
			`   ${(a.part.b / m).toFixed(0)} / ${(a.part.by / m / 1024).toFixed(0)}`.padEnd(19) +
			`${(a.cell.b / m).toFixed(0)} / ${(a.cell.by / m / 1024).toFixed(0)}`.padEnd(18) +
			`${(a.under.b / m).toFixed(0)} / ${(a.under.by / m / 1024).toFixed(0)}`.padEnd(18) +
			`${(a.under.ub / m).toFixed(0)} блитов / ${(a.under.uby / m / 1024).toFixed(0)} КБ`);
	});

	const de = R.destr;
	console.log('\nразрушение одной части клетки (замена на dieMCD): ' +
		`${de.inCache} из ${de.tries} случаев (${(100 * de.inCache / de.tries).toFixed(0)} %) дают сигнатуру, уже лежащую в кеше; ` +
		`новых сигнатур максимум ${de.newSig.size}`);

	const d = R.dirty;
	console.log(`\nгрязный прямоугольник 3x3 клетки (шаг бойца) на уровне ${d.level}, среднее по ${d.n} точкам:`);
	console.log(`  по частям  ${(d.part.b / d.n).toFixed(1)} блитов, ${(d.part.by / d.n).toFixed(0)} байт BLT1`);
	console.log(`  склейками  ${(d.cell.b / d.n).toFixed(1)} блитов, ${(d.cell.by / d.n).toFixed(0)} байт BLT1`);
	console.log(`  подложкой  ${(d.under.b / d.n).toFixed(1)} блит,  ${(d.under.by / d.n).toFixed(0)} байт RAM->RAM ` +
		`(${((d.under.by / d.n) / 2 * CYC_COPY).toFixed(0)} циклов против ${((d.part.by / d.n) / 2 * CYC_BLT1).toFixed(0)})`);
}

// ---------------------------------------------------------------- main

const LIST = [
	['TFTD', 'SEABED'], ['TFTD', 'ALART'], ['TFTD', 'PORT'], ['TFTD', 'CARGO'],
	['TFTD', 'XBASES'], ['TFTD', 'GRUNGE'], ['TFTD', 'ISLAND'],
	['UFO', 'CULTA'], ['UFO', 'URBAN'], ['UFO', 'UBASE'], ['UFO', 'XBASE'], ['UFO', 'DESERT'],
];

if (flag('all')) {
	const all = [];
	for (const [g, t] of LIST) {
		try { const R = run(g, t, true); printOne(R); all.push(R); }
		catch (e) { console.log(`!! ${g}/${t}: ${e.message}`); }
	}
	// сводка
	console.log('\n================ СВОДКА (среднее по камерам) ================');
	console.log('игра/террейн     клеток  сигн.карты  сигн.кадр  кеш КБ  part→cell блитов  part→cell байт  ошибок');
	for (const R of all) {
		const a = R.acc, n = R.cams;
		console.log(`${(R.game + '/' + R.terName).padEnd(16)} ${R.cellsFrame.toFixed(0).padStart(6)} ` +
			`${String(R.sigTotal).padStart(11)} ${R.sigFrame.toFixed(0).padStart(10)} ${(R.allBytes / 1024).toFixed(0).padStart(7)} ` +
			`${(a.part.blits / n).toFixed(0)}→${(a.cell.blits / n).toFixed(0)}`.padStart(18) +
			`${(a.part.bytes / n / 1024).toFixed(0)}→${(a.cell.bytes / n / 1024).toFixed(0)} КБ`.padStart(17) +
			`${R.bad.cell.toFixed(0)}`.padStart(8));
	}
} else {
	printOne(run(arg('game', 'TFTD'), arg('terrain', 'SEABED'), true));
}
