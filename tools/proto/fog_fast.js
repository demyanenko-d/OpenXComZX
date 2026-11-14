// Туман войны: быстрый расчёт разведанности вместо точного TileEngine::calculateFOV (16 §5.2).
// Оригинал на каждую клетку сектора обзора (все этажи) ведёт тайловую линию calculateLine от бойца
// и помечает разведанными её клетки до преграды. Здесь — точная модель (логика линии один в один как
// в src/battlescape/vision.c: маска клетки, hblk / vblk / obj_blk) и быстрые варианты; для каждого —
// сколько клеток разошлось с точным и сколько шагов линии (вызовов hblk+vblk) потрачено.
//
//   node tools/proto/fog_fast.js                       # TFTD, наборы и карта по умолчанию
//   node tools/proto/fog_fast.js --game UFO --sets BLANKS,ROADS,URBITS,URBAN,FRNITURE --map URBAN --blocks 5
//   node tools/proto/fog_fast.js --samples 400
//
// Варианты: exact — линия на каждую клетку сектора (как в оригинале); rim — только на клетки края
// сектора; rimfill — край, затем своя линия каждой клетке сектора, которую край не открыл (подмножество
// линий оригинала: лишних клеток не бывает, только недостающие).
'use strict';
const fs = require('fs');
const path = require('path');
const B = require('./battle');

function arg(name, def) {
	const i = process.argv.indexOf('--' + name);
	return i >= 0 && i + 1 < process.argv.length ? process.argv[i + 1] : def;
}
const game = arg('game', 'TFTD');
const root = arg('root', null) || [B.GAMES[game], 'Steam/XCom UFO Defense/XCOM'].find(p => fs.existsSync(p));
const sets = arg('sets', 'BLANKS,SAND,ROCKS,WEEDS,PYRAMID,UFOBITS').split(',');
const mapName = arg('map', 'ALART');
const nBlocks = parseInt(arg('blocks', '5'), 10);
const samples = parseInt(arg('samples', '200'), 10);

// ---------------------------------------------------------------- карта (как в opt6_fog.js)
const parts = [];
for (const s of sets) {
	const mcd = B.readMcd(fs.readFileSync(path.join(root, 'TERRAIN', s + '.MCD')));
	for (const r of mcd) parts.push(r);
}
function buildMap(pattern, n) {
	const dir = path.join(root, 'MAPS');
	const names = fs.readdirSync(dir).filter(f => f.startsWith(pattern) && f.endsWith('.MAP')).sort();
	const blocks = [];
	for (const f of names) {
		const b = B.readMap(fs.readFileSync(path.join(dir, f)));
		if (blocks.length && (b.sx !== blocks[0].sx || b.sy !== blocks[0].sy)) continue;
		blocks.push(b);
	}
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
const map = buildMap(mapName, nBlocks);
const { sx, sy, sz } = map;
const N = sx * sy * sz;
const idx = (x, y, z) => (z * sy + y) * sx + x;
const inMap = (x, y, z) => x >= 0 && y >= 0 && z >= 0 && x < sx && y < sy && z < sz;
const rec = id => (id ? parts[id - 1] : null);

// ---------------------------------------------------------------- маска обзора (vision.c cell_mask)
const VM_W = 1, VM_N = 2, VM_O = 4, VM_F = 8;
const vm = new Uint8Array(N);
for (let i = 0; i < N; i++) {
	const f = rec(map.cells[i * 4]), w = rec(map.cells[i * 4 + 1]), n = rec(map.cells[i * 4 + 2]), o = rec(map.cells[i * 4 + 3]);
	let m = 0;
	if (w && w.stopLOS) m |= VM_W;
	if (n && n.stopLOS) m |= VM_N;
	if (o) { if (o.stopLOS) m |= VM_O; m |= (o.bigWall & 15) << 4; }
	if (f && (f.stopLOS || !f.noFloor)) m |= VM_F;
	vm[i] = m;
}
const mk = (x, y, z) => (inMap(x, y, z) ? vm[idx(x, y, z)] : 0);

const vdX = [0, 1, 1, 1, 0, -1, -1, -1], vdY = [-1, -1, 0, 1, 1, 1, 0, -1];
function objBlk(m, dir, origin) {
	if (!(m & VM_O)) return 0;
	const w = m >> 4;
	if (origin && (w === 2 || w === 3)) return 0;
	switch (dir) {
	case 0: if (w === 4 || w === 6 || w === 7 || w === 8) return 0; break;
	case 1: if (w === 4 || w === 7) return 0; break;
	case 2: if (w === 5 || w === 7 || w === 4 || w === 9) return 0; break;
	case 3: if (w === 5 || w === 4 || w === 9) return 0; break;
	case 4: if (w === 4 || w === 6 || w === 5 || w === 9) return 0; break;
	case 5: if (w === 5 || w === 6) return 0; break;
	case 6: if (w === 5 || w === 7 || w === 6 || w === 8) return 0; break;
	case 7: if (w === 7 || w === 6 || w === 8) return 0; break;
	default: if (w !== 0 && w !== 1) return 0; break;
	}
	return 255;
}
const WB = m => (m & VM_W ? 255 : 0), NB = m => (m & VM_N ? 255 : 0), FB = m => (m & VM_F ? 256 : 0);
function hblk(sx_, sy_, z, ex, ey, skip) {
	if (!inMap(sx_, sy_, z) || !inMap(ex, ey, z)) return 0;
	const dx = ex - sx_, dy = ey - sy_;
	let dir = 0;
	for (; dir < 8; dir++) if (vdX[dir] === dx && vdY[dir] === dy) break;
	if (dir === 8) return 0;
	const s = mk(sx_, sy_, z), e = mk(ex, ey, z);
	let b = 0, t;
	switch (dir) {
	case 0: b = NB(s); break;
	case 1:
		t = mk(sx_, sy_ - 1, z); b = NB(s) + WB(e) + ((t >> 4) !== 2 ? objBlk(t, 3, 0) : 0);
		if (!b) break;
		t = mk(sx_ + 1, sy_, z); b = NB(t) + WB(t) + ((t >> 4) !== 2 ? objBlk(t, 7, 0) : 0); break;
	case 2: b = WB(e); break;
	case 3:
		t = mk(sx_, sy_ + 1, z); b = NB(t) + WB(e) + ((t >> 4) !== 3 ? objBlk(t, 1, 0) : 0);
		if (!b) break;
		t = mk(sx_ + 1, sy_, z); b = WB(t) + NB(e) + ((t >> 4) !== 3 ? objBlk(t, 5, 0) : 0); break;
	case 4: b = NB(e); break;
	case 5:
		t = mk(sx_, sy_ + 1, z); b = NB(t) + WB(t) + ((t >> 4) !== 2 ? objBlk(t, 7, 0) : 0);
		if (!b) break;
		t = mk(sx_ - 1, sy_, z); b = WB(s) + NB(e) + ((t >> 4) !== 2 ? objBlk(t, 3, 0) : 0); break;
	case 6: b = WB(s); break;
	default:
		t = mk(sx_, sy_ - 1, z); b = NB(s) + WB(t) + ((t >> 4) !== 3 ? objBlk(t, 5, 0) : 0);
		if (!b) break;
		t = mk(sx_ - 1, sy_, z); b = WB(s) + NB(t) + ((t >> 4) !== 3 ? objBlk(t, 1, 0) : 0); break;
	}
	if (!skip || (s >> 4)) b += objBlk(s, dir, 0);
	if (b <= 127 && objBlk(e, (dir + 4) & 7, 1) > 127) return -1;
	return b;
}
function vblk(sx_, sy_, sz_, ex, ey, ez) {
	let b = 0;
	if (!inMap(sx_, sy_, sz_) || !inMap(ex, ey, ez) || ez === sz_) return 0;
	if (ez < sz_) {
		let m = mk(sx_, sy_, sz_);
		b = FB(m) + objBlk(m, 9, 0);
		if (sx_ !== ex || sy_ !== ey) { m = mk(ex, ey, sz_); b += hblk(sx_, sy_, sz_, ex, ey, 0) + FB(m) + objBlk(m, 9, 0); }
	} else {
		let m = mk(sx_, sy_, sz_ + 1);
		b = FB(m) + objBlk(m, 8, 0);
		if (sx_ !== ex || sy_ !== ey) { m = mk(ex, ey, sz_ + 1); b += FB(m) + objBlk(m, 8, 0); }
	}
	return b;
}

// calculateLine (тайловая) с траекторией: мечет клетки до преграды; клетка, на которой линия
// упёрлась (> 127), не открывается (TileEngine.cpp:321), большая стена (-1) — открывается
let steps = 0;
function line(x0, y0, z0, x1, y1, z1, mark) {
	let t, lx = x0, ly = y0, lz = z0;
	const swXY = Math.abs(y1 - y0) > Math.abs(x1 - x0);
	if (swXY) { t = x0; x0 = y0; y0 = t; t = x1; x1 = y1; y1 = t; }
	const swXZ = Math.abs(z1 - z0) > Math.abs(x1 - x0);
	if (swXZ) { t = x0; x0 = z0; z0 = t; t = x1; x1 = z1; z1 = t; }
	const ddx = Math.abs(x1 - x0), ddy = Math.abs(y1 - y0), ddz = Math.abs(z1 - z0);
	let dxy = ddx >> 1, dxz = ddx >> 1;
	const stx = x0 > x1 ? -1 : 1, sty = y0 > y1 ? -1 : 1, stz = z0 > z1 ? -1 : 1;
	let y = y0, z = z0, n = 0;
	for (let x = x0; ; x += stx) {
		let cx = x, cy = y, cz = z;
		if (swXZ) { t = cx; cx = cz; cz = t; }
		if (swXY) { t = cx; cx = cy; cy = t; }
		steps++;
		const tv = vblk(lx, ly, lz, cx, cy, cz);
		let r = hblk(lx, ly, lz, cx, cy, n < 2);
		n++;
		if (r === -1) {
			if (tv <= 127) { mark(cx, cy, cz); return; }   // большая стена: клетка в траектории
			r = 0;
		}
		if (r + tv > 127) return;
		mark(cx, cy, cz);
		lx = cx; ly = cy; lz = cz;
		if (x === x1) break;
		dxy -= ddy; dxz -= ddz;
		if (dxy < 0) { y += sty; dxy += ddx; }
		if (dxz < 0) { z += stz; dxz += ddx; }
	}
}

// ---------------------------------------------------------------- сектор
const MAXV = 20;
const sgX = [1, 1, 1, 1, -1, -1, -1, -1], sgY = [-1, -1, -1, 1, 1, 1, -1, -1];
// клетки сектора в локальных (x, y) и признак края (за ним по x или y клетки сектора нет)
function cone(dir) {
	const out = [];
	const inC = (x, y) => {
		if (x < 0 || x > MAXV) return false;
		if (dir & 1) { if (y < 0 || y > MAXV) return false; } else if (y < -x || y > x) return false;
		return x * x + y * y <= MAXV * MAXV;
	};
	for (let x = 0; x <= MAXV; x++) {
		const y1 = dir & 1 ? 0 : -x, y2 = dir & 1 ? MAXV : x;
		for (let y = y1; y <= y2; y++) {
			if (!inC(x, y)) continue;
			const rim = !inC(x + 1, y) || !inC(x, y + 1) || !inC(x, y - 1);
			out.push({ x, y, rim });
		}
	}
	return out;
}
const cones = [0, 1, 2, 3, 4, 5, 6, 7].map(cone);
function world(dir, ux, uy, c) {
	const swap = dir === 0 || dir === 4;
	return [ux + sgX[dir] * (swap ? c.y : c.x), uy + sgY[dir] * (swap ? c.x : c.y)];
}

// ---------------------------------------------------------------- дерево путей
// Путь линии calculateLine зависит только от разности (dx, dy, dz) — карта на него не влияет; шаг
// линии решают предыдущая клетка, текущая и номер шага (steps < 2). Значит, линии ко всем клеткам
// сектора (всех этажей) — одно дерево с общими началами: обход в глубину проверяет каждое ребро
// один раз, а за преградой отсекает всё поддерево. Дерево — в локальных координатах сектора (как
// cone()); направления получаются знаками и обменом осей (линия симметрична: решения берутся по
// модулям разностей, ничья |dy| = |dx| — по X и при обмене осей даёт ту же диагональ).
function linePath(x0, y0, z0, x1, y1, z1) {
	const out = [];
	let t;
	const swXY = Math.abs(y1 - y0) > Math.abs(x1 - x0);
	if (swXY) { t = x0; x0 = y0; y0 = t; t = x1; x1 = y1; y1 = t; }
	const swXZ = Math.abs(z1 - z0) > Math.abs(x1 - x0);
	if (swXZ) { t = x0; x0 = z0; z0 = t; t = x1; x1 = z1; z1 = t; }
	const ddx = Math.abs(x1 - x0), ddy = Math.abs(y1 - y0), ddz = Math.abs(z1 - z0);
	let dxy = ddx >> 1, dxz = ddx >> 1;
	const stx = x0 > x1 ? -1 : 1, sty = y0 > y1 ? -1 : 1, stz = z0 > z1 ? -1 : 1;
	let y = y0, z = z0;
	for (let x = x0; ; x += stx) {
		let cx = x, cy = y, cz = z;
		if (swXZ) { t = cx; cx = cz; cz = t; }
		if (swXY) { t = cx; cx = cy; cy = t; }
		out.push([cx, cy, cz]);
		if (x === x1) break;
		dxy -= ddy; dxz -= ddz;
		if (dxy < 0) { y += sty; dxy += ddx; }
		if (dxz < 0) { z += stz; dxz += ddx; }
	}
	return out;
}
const DZ = 3;   // этажей до 4: разность по z — -3..3
function buildTree(dir) {
	const root = { k: '0,0,0', p: [0, 0, 0], ch: new Map(), tgt: [] };
	let nodes = 1;
	for (const c of cones[dir]) {
		// линия в мировых знаках этого направления, затем обратно в локальные — так ловится асимметрия
		const swap = dir === 0 || dir === 4;
		const wx = sgX[dir] * (swap ? c.y : c.x), wy = sgY[dir] * (swap ? c.x : c.y);
		for (let dz = -DZ; dz <= DZ; dz++) {
			const path = linePath(0, 0, 0, wx, wy, dz);
			let n = root;
			for (let i = 1; i < path.length; i++) {
				const k = path[i].join(',');
				let m = n.ch.get(k);
				if (!m) { m = { k, p: path[i], ch: new Map(), tgt: [] }; n.ch.set(k, m); nodes++; }
				n = m;
			}
			n.tgt.push(dz);
		}
	}
	return { root, nodes };
}
const trees = [0, 1, 2, 3, 4, 5, 6, 7].map(buildTree);
console.log('узлов дерева по направлениям: ' + trees.map(t => t.nodes).join(' '));

// обход: rel — мировые смещения узла; ребро (родитель -> узел) проверяется как шаг линии номер depth
function treeFov(ux, uy, uz, dir, disc) {
	const mark = (x, y, z) => {
		const i = idx(x, y, z);
		disc[i] |= 4;
		if (inMap(x + 1, y, z)) disc[i + 1] |= 1;
		if (inMap(x, y + 1, z)) disc[i + sx] |= 2;
	};
	mark(ux, uy, uz);
	steps++;   // первый шаг линии (клетка бойца) — как в точной модели
	const walk = (n, lx, ly, lz, depth) => {
		for (const m of n.ch.values()) {
			const cx = ux + m.p[0], cy = uy + m.p[1], cz = uz + m.p[2];
			if (!inMap(cx, cy, cz)) continue;
			steps++;
			const tv = vblk(lx, ly, lz, cx, cy, cz);
			let r = hblk(lx, ly, lz, cx, cy, depth < 2);
			if (r === -1) {
				if (tv <= 127) { mark(cx, cy, cz); continue; }
				r = 0;
			}
			if (r + tv > 127) continue;
			mark(cx, cy, cz);
			walk(m, cx, cy, cz, depth + 1);
		}
	};
	walk(trees[dir].root, ux, uy, uz, 1);
}

// Список линий в порядке обхода дерева: у каждой — цель (dx, dy, dz, мировые смещения) и L — длина
// общего начала с предыдущей в списке. Обход: если предыдущая упёрлась на шаге b < L — эта упирается
// там же (ничего нового); иначе шаги с L заново. Пропуск целей вне карты: L берётся минимумом по
// пропущенным (общее начало с последней посчитанной). Дерево хранить не нужно — только список.
// order3d — порядок обхода дерева 3D (оптимум, как tree); perdz — по этажам отдельно, порядок целей
// из дерева dz = 0 (таблица меньше: порядок целей общий, L — на каждый dz).
function dfsOrder(dir, dzList) {
	const swap = dir === 0 || dir === 4;
	const lines = [];
	for (const c of cones[dir]) {
		const wx = sgX[dir] * (swap ? c.y : c.x), wy = sgY[dir] * (swap ? c.x : c.y);
		for (const dz of dzList) lines.push({ wx, wy, dz, path: linePath(0, 0, 0, wx, wy, dz) });
	}
	// порядок обхода в глубину = сортировка путей лексикографически по клеткам
	const key = p => p.path.map(q => q.join(',')).join(';');
	lines.sort((a, b) => { const pa = a.path, pb = b.path;
		for (let i = 0; i < Math.min(pa.length, pb.length); i++) {
			const d = (pa[i][0] - pb[i][0]) || (pa[i][1] - pb[i][1]) || (pa[i][2] - pb[i][2]);
			if (d) return d;
		}
		return pa.length - pb.length; });
	for (let i = 0; i < lines.length; i++) {
		if (!i) { lines[i].L = 0; continue; }
		const pa = lines[i - 1].path, pb = lines[i].path;
		let l = 0;
		while (l < pa.length && l < pb.length && pa[l].join() === pb[l].join()) l++;
		lines[i].L = l;
	}
	return lines;
}
const order3d = [0, 1, 2, 3, 4, 5, 6, 7].map(d => dfsOrder(d, [-3, -2, -1, 0, 1, 2, 3]));
const order2d = [0, 1, 2, 3, 4, 5, 6, 7].map(d => dfsOrder(d, [0]));
console.log(`список 3D: ${order3d[0].length} линий, 2D: ${order2d[0].length}`);

function listFov(lines, ux, uy, uz, disc, perdz) {
	const mark = (x, y, z) => {
		const i = idx(x, y, z);
		disc[i] |= 4;
		if (inMap(x + 1, y, z)) disc[i + 1] |= 1;
		if (inMap(x, y + 1, z)) disc[i + sx] |= 2;
	};
	const run = (lst, dz) => {
		let prev = null, prevB = 0, Leff = 0;   // путь и место упора последней посчитанной
		for (let k = 0; k < lst.length; k++) {
			const ln = lst[k];
			const dzz = dz === undefined ? ln.dz : dz;
			const path = dz === undefined ? ln.path : linePath(0, 0, 0, ln.wx, ln.wy, dz);
			const L = dz === undefined ? ln.L : (k ? commonLen(lst[k - 1], ln, dz) : 0);
			if (k) Leff = Math.min(Leff, L);
			const tx = ux + ln.wx, ty = uy + ln.wy, tz = uz + dzz;
			if (!inMap(tx, ty, tz)) continue;
			let start = 0;
			if (prev) {
				const l = Leff;
				if (prevB < l) { Leff = 1e9; continue; }        // упор внутри общего начала
				start = l;
			}
			let b = path.length;                            // шаг упора (path.length — дошла)
			for (let i = start; i < path.length; i++) {
				const [px, py, pz] = path[i];
				const cx = ux + px, cy = uy + py, cz = uz + pz;
				const [qx, qy, qz] = i ? path[i - 1] : path[0];
				const lx = ux + qx, ly = uy + qy, lz = uz + qz;
				steps++;
				const tv = vblk(lx, ly, lz, cx, cy, cz);
				let r = hblk(lx, ly, lz, cx, cy, i < 2);
				if (r === -1) {
					if (tv <= 127) { mark(cx, cy, cz); b = i; break; }   // большая стена: дальше не идём
					r = 0;
				}
				if (r + tv > 127) { b = i; break; }
				mark(cx, cy, cz);
			}
			prev = ln; prevB = b; Leff = 1e9;
		}
	};
	if (perdz) for (let dz = -uz; dz < sz - uz; dz++) run(lines, dz);
	else run(lines);
}
const clCache = new Map();
function commonLen(a, b, dz) {
	const k = a.wx + ',' + a.wy + ',' + b.wx + ',' + b.wy + ',' + dz;
	let v = clCache.get(k);
	if (v === undefined) {
		const pa = linePath(0, 0, 0, a.wx, a.wy, dz), pb = linePath(0, 0, 0, b.wx, b.wy, dz);
		v = 0;
		while (v < pa.length && v < pb.length && pa[v].join() === pb[v].join()) v++;
		clCache.set(k, v);
	}
	return v;
}

// Канонические таблицы: только направления 0 (прямо) и 1 (наискось) в локальных координатах сектора
// (x вдоль, y поперёк — как cone()); остальные направления — теми же записями через знаки и обмен
// осей. Верно, если линия симметрична (проверяет вариант canon: расхождение с точным должно быть 0).
function toLocal(dir, wx, wy) {           // обратное к world(): мировые смещения -> локальные
	const swap = dir === 0 || dir === 4;
	return swap ? { x: wy * sgY[dir], y: wx * sgX[dir] } : { x: wx * sgX[dir], y: wy * sgY[dir] };
}
const canon = [0, 1].map(d => order3d[d].map(ln => ({ ...toLocal(d, ln.wx, ln.wy), dz: ln.dz, L: ln.L })));
function canonLines(dir) {
	const swap = dir === 0 || dir === 4;
	return canon[dir & 1].map(e => {
		const wx = sgX[dir] * (swap ? e.y : e.x), wy = sgY[dir] * (swap ? e.x : e.y);
		return { wx, wy, dz: e.dz, L: e.L, path: linePath(0, 0, 0, wx, wy, e.dz) };
	});
}
const canonByDir = [0, 1, 2, 3, 4, 5, 6, 7].map(canonLines);
{
	let maxL = 0, maxX = 0, minY = 0, maxY = 0;
	for (const t of canon) for (const e of t) { maxL = Math.max(maxL, e.L); maxX = Math.max(maxX, e.x); minY = Math.min(minY, e.y); maxY = Math.max(maxY, e.y); }
	console.log(`канонические таблицы: ${canon[0].length} + ${canon[1].length} записей; L до ${maxL}, x до ${maxX}, y ${minY}..${maxY}`);
}

// Обход с прыжками (как vision.c fov_unit): J — следующая запись с L меньше, чем у этой
for (const t of canonByDir) {
	const st = [];
	for (let i = 0; i < t.length; i++) t[i].J = t.length;
	for (let i = 0; i < t.length; i++) {
		while (st.length && t[st[st.length - 1]].L > t[i].L) t[st.pop()].J = i;
		st.push(i);
	}
}
let visits = 0;
function jumpFov(lines, ux, uy, uz, disc) {
	const mark = (x, y, z) => {
		const i = idx(x, y, z);
		disc[i] |= 4;
		if (inMap(x + 1, y, z)) disc[i + 1] |= 1;
		if (inMap(x, y + 1, z)) disc[i + sx] |= 2;
	};
	let have = false, pb = 0, leff = 0, k = 0;
	while (k < lines.length) {
		visits++;
		const ln = lines[k];
		if (ln.L < leff) leff = ln.L;
		if (have && pb < leff) { k = ln.J; continue; }
		k++;
		if (!inMap(ux + ln.wx, uy + ln.wy, uz + ln.dz)) continue;
		const path = ln.path, start = have ? leff : 0;
		let b = 255;
		for (let i = start; i < path.length; i++) {
			const cx = ux + path[i][0], cy = uy + path[i][1], cz = uz + path[i][2];
			const q = i ? path[i - 1] : path[0];
			const lx = ux + q[0], ly = uy + q[1], lz = uz + q[2];
			steps++;
			const tv = vblk(lx, ly, lz, cx, cy, cz);
			let r = hblk(lx, ly, lz, cx, cy, i < 2);
			if (r === -1) {
				if (tv <= 127) { mark(cx, cy, cz); b = i; break; }
				r = 0;
			}
			if (r + tv > 127) { b = i; break; }
			mark(cx, cy, cz);
		}
		pb = b; have = true; leff = 255;
	}
}

function fov(variant, ux, uy, uz, dir, disc) {
	if (variant === 'tree') { treeFov(ux, uy, uz, dir, disc); return; }
	if (variant === 'jump') { visits = 0; jumpFov(canonByDir[dir], ux, uy, uz, disc); visitTot += visits; return; }
	if (variant === 'canon') { listFov(canonByDir[dir], ux, uy, uz, disc, false); return; }
	if (variant === 'order3d') { listFov(order3d[dir], ux, uy, uz, disc, false); return; }
	if (variant === 'perdz') { listFov(order2d[dir], ux, uy, uz, disc, true); return; }
	const mark = (x, y, z) => {
		const i = idx(x, y, z);
		disc[i] |= 4;
		if (inMap(x + 1, y, z)) disc[i + 1] |= 1;
		if (inMap(x, y + 1, z)) disc[i + sx] |= 2;
	};
	const c = cones[dir];
	for (const t of c) {
		if (variant !== 'exact' && !t.rim) continue;
		const [tx, ty] = world(dir, ux, uy, t);
		if (tx < 0 || ty < 0 || tx >= sx || ty >= sy) continue;
		for (let z = 0; z < sz; z++) line(ux, uy, uz, tx, ty, z, mark);
	}
	if (variant === 'rimfill') {
		for (const t of c) {
			if (t.rim) continue;
			const [tx, ty] = world(dir, ux, uy, t);
			if (tx < 0 || ty < 0 || tx >= sx || ty >= sy) continue;
			for (let z = 0; z < sz; z++) if (!(disc[idx(tx, ty, z)] & 4)) line(ux, uy, uz, tx, ty, z, mark);
		}
	}
}

// ---------------------------------------------------------------- прогон: случайные точки на полу
let seed = 12345;
const rnd = n => { seed = (seed * 1103515245 + 12345) & 0x7fffffff; return seed % n; };
const spots = [];
for (let z = 0; z < sz; z++)
	for (let y = 0; y < sy; y++)
		for (let x = 0; x < sx; x++) {
			const i = idx(x, y, z), f = rec(map.cells[i * 4]), o = rec(map.cells[i * 4 + 3]);
			if (f && !f.noFloor && !(o && o.stopLOS)) spots.push([x, y, z]);
		}
console.log(`${game} ${mapName}: карта ${sx}x${sy}x${sz}, клеток с полом ${spots.length}, проб ${samples}`);
console.log(`сектор: прямо ${cones[0].length} клеток (край ${cones[0].filter(c => c.rim).length}), наискось ${cones[1].length} (край ${cones[1].filter(c => c.rim).length})`);

const variants = (arg('variants', 'exact,rim,rimfill,tree,order3d,perdz,canon,jump')).split(',');
let visitTot = 0;
const tot = {};
for (const v of variants) tot[v] = { steps: 0, miss: 0, extra: 0, missW: 0, worst: 0, disc: 0 };
for (let s = 0; s < samples; s++) {
	const [x, y, z] = spots[rnd(spots.length)], dir = rnd(8);
	const ref = new Uint8Array(N);
	for (const v of variants) {
		const d = v === 'exact' ? ref : new Uint8Array(N);
		steps = 0;
		fov(v, x, y, z, dir, d);
		tot[v].steps += steps;
		if (v === 'exact') { for (let i = 0; i < N; i++) if (ref[i] & 4) tot[v].disc++; continue; }
		let miss = 0, extra = 0, missW = 0;
		for (let i = 0; i < N; i++) {
			if ((ref[i] & 4) && !(d[i] & 4)) miss++;
			if (!(ref[i] & 4) && (d[i] & 4)) extra++;
			if ((ref[i] & 3) !== (d[i] & 3) && !(ref[i] & 4)) missW++;
		}
		tot[v].miss += miss; tot[v].extra += extra; tot[v].missW += missW;
		if (miss > tot[v].worst) tot[v].worst = miss;
	}
}
if (visitTot) console.log(`jump: записей таблицы перебрано в среднем ${(visitTot / samples).toFixed(0)} (без прыжков — все 2303..2345)`);
for (const v of variants) {
	const t = tot[v];
	console.log(`${v.padEnd(8)} шагов линии в среднем ${(t.steps / samples).toFixed(0).padStart(6)}` +
		(v === 'exact' ? `, открыто клеток в среднем ${(t.disc / samples).toFixed(0)}` :
			`, не хватает клеток ${(t.miss / samples).toFixed(2)} (худшая проба ${t.worst}), лишних ${(t.extra / samples).toFixed(2)}, стен ${(t.missW / samples).toFixed(2)}`));
}
