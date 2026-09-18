// Во что обходятся разные схемы хранения кадра тайла (17_battle_render.md §2): байты DMA и число
// запусков для одного вида карты. Схемы:
//   crop   — как сейчас: кадр обрезан по непрозрачным пикселям (своя ширина и высота у каждого);
//   rows   — ширина всегда 32, обрезаны только строки сверху и снизу;
//   half   — две половины по 16 пикселей, у каждой обрезаны строки;
//   full   — кадр целиком 32x40 (ничего не обрезано).
//   node tools/proto/blitsize.js [--game TFTD] [--terrain SEABED] [--sets ...] [--level 1]
'use strict';
const fs = require('fs');
const path = require('path');
const B = require('./battle');

function arg(name, def) {
	const i = process.argv.indexOf('--' + name);
	return i > 0 && i + 1 < process.argv.length ? process.argv[i + 1] : def;
}

const game = arg('game', 'TFTD');
const root = B.GAMES[game];
const sets = arg('sets', 'BLANKS,SAND,ROCKS,WEEDS,DEBRIS,UFOBITS').split(',');
const mapPattern = arg('map', 'SEABED');
const level = parseInt(arg('level', '1'), 10);

const parts = [], frames = [];
for (const s of sets) {
	const dir = path.join(root, 'TERRAIN');
	if (!fs.existsSync(path.join(dir, s + '.MCD'))) continue;
	const mcd = B.readMcd(fs.readFileSync(path.join(dir, s + '.MCD')));
	const pck = B.readPck(fs.readFileSync(path.join(dir, s + '.PCK')), fs.readFileSync(path.join(dir, s + '.TAB')));
	const base = frames.length;
	for (const r of mcd) { r.base = base; parts.push(r); }
	for (const f of pck) frames.push(f);
}

// поле 4x4 блока, как у нашего конвертера
const dir = path.join(root, 'MAPS');
const names = fs.readdirSync(dir).filter(f => f.startsWith(mapPattern) && f.endsWith('.MAP')).sort().slice(0, 16);
const blocks = names.map(f => B.readMap(fs.readFileSync(path.join(dir, f))));
const bw = blocks[0].sx, bh = blocks[0].sy, n = 4;
const sz = Math.max(...blocks.map(b => b.sz));
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

// Габариты кадра в четырёх схемах
const boxCache = new Map();
function boxes(img) {
	if (boxCache.has(img)) return boxCache.get(img);
	let x0 = B.TILE_W, x1 = -1, y0 = B.TILE_H, y1 = -1;
	const halves = [{ y0: B.TILE_H, y1: -1 }, { y0: B.TILE_H, y1: -1 }];
	for (let y = 0; y < B.TILE_H; y++)
		for (let x = 0; x < B.TILE_W; x++) {
			if (!img[y * B.TILE_W + x]) continue;
			if (x < x0) x0 = x;
			if (x > x1) x1 = x;
			if (y < y0) y0 = y;
			if (y > y1) y1 = y;
			const h = halves[x < 16 ? 0 : 1];
			if (y < h.y0) h.y0 = y;
			if (y > h.y1) h.y1 = y;
		}
	const v = x1 < 0
		? { crop: [0, 0], rows: [0, 0], halves: [[0, 0], [0, 0]] }
		: {
			crop: [((x1 - (x0 & ~1) + 1) + 1) & ~1, y1 - y0 + 1],   // ширина чётная, как в SPRSET
			rows: [B.TILE_W, y1 - y0 + 1],
			halves: halves.map(h => (h.y1 < 0 ? [0, 0] : [16, h.y1 - h.y0 + 1])),
		};
	boxCache.set(img, v);
	return v;
}

function frameOf(id) {
	const r = parts[id - 1];
	return r ? frames[r.base + r.frame[0]] : null;
}

// Проход по видимым клеткам окна 320x144, камера в центре
const cx = sx / 2, cy = sy / 2;
const ox = Math.round(B.VIEW_W / 2 - (cx - cy) * 16);
const oy = Math.round(B.VIEW_H / 2 - ((cx + cy) * 8 - level * 24));
const st = { crop: [0, 0], rows: [0, 0], half: [0, 0], full: [0, 0] };   // [байт, запусков]
let nparts = 0;
for (let z = 0; z <= level && z < sz; z++)
	for (let x = 0; x < sx; x++)
		for (let y = 0; y < sy; y++) {
			const px = (x - y) * 16 + ox, py = (x + y) * 8 - z * 24 + oy;
			if (px <= -B.TILE_W || px >= B.VIEW_W || py <= -B.TILE_H || py >= B.VIEW_H) continue;
			const o = ((z * sy + y) * sx + x) * 4;
			for (let k = 0; k < 4; k++) {
				const id = cells[o + k];
				if (!id) continue;
				const img = frameOf(id);
				if (!img) continue;
				const b = boxes(img);
				if (!b.crop[1]) continue;
				nparts++;
				st.crop[0] += b.crop[0] * b.crop[1]; st.crop[1]++;
				st.rows[0] += b.rows[0] * b.rows[1]; st.rows[1]++;
				for (const h of b.halves) if (h[1]) { st.half[0] += h[0] * h[1]; st.half[1]++; }
				st.full[0] += B.TILE_W * B.TILE_H; st.full[1]++;
			}
		}

// Кадр 50 Гц: 71680 тактов; DMA BLT1 — 72 КБ за кадр (замер §8.3f), запуск — 130 T (дисплей-лист)
const KBF = 72 * 1024, T = 71680, SETUP = 130;
console.log(`${game} ${mapPattern}, поле ${sx}x${sy}x${sz}, уровень ${level}, частей в виде ${nparts}`);
console.log('схема   байт DMA  запусков  кадров DMA  кадров CPU  итого');
for (const [name, v] of Object.entries(st)) {
	const fd = v[0] / KBF, fc = v[1] * SETUP / T;
	console.log(`${name.padEnd(7)} ${String(v[0]).padStart(8)} ${String(v[1]).padStart(9)} ${fd.toFixed(2).padStart(11)} ${fc.toFixed(2).padStart(11)} ${Math.max(fd, fc).toFixed(2).padStart(6)}`);
}
