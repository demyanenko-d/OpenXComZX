// Путь Б (globe.md §12.2): списки границ текстур по строкам экрана — образец для конвертера.
// Строит списки из ресурса GLOBE, рисует кадр и сверяет с моделью рёберного рендера и с
// попиксельным эталоном. При наклоне 0 пересечение ребра со строкой даёт x = R·c·sin(фаза − λ0)
// и z = c·cos(фаза − λ0) — с одними и теми же амплитудой и фазой, поэтому видимость границы это
// |фаза − λ0| < 90°, а порядок границ вдоль строки — это порядок фаз.
//   node toolsglobe_rows.js [зумы]; GAME=UFO
'use strict';
const fs = require('fs');
const M = require('./globe_model.js');
const game = process.env.GAME || 'TFTD';
const ROWOFF = +(process.env.ROWOFF ?? 0.5);        // где в строке берётся Z: 0 — верх, 0.5 — центр
const PAIR = +(process.env.PAIR ?? 1);              // 1 — границы по парам пикселей (как DMA)
const SHIFT = +(process.env.SHIFT ?? 0.5);              // 1 — границы по парам пикселей (как DMA)
const pak = fs.readFileSync(`tmp/sd/OXZ/${game}/GEO.PAK`);
function res(id) {
	const n = pak.readUInt16LE(6);
	for (let i = 0; i < n; i++) {
		const e = 16 + i * 16;
		if (pak.readUInt16LE(e) === id) { const sec = pak.readUInt16LE(e + 4), size = pak.readUInt32LE(e + 6); return pak.subarray(sec * 512, sec * 512 + size); }
	}
	throw 'no res ' + id;
}
const G = res(0x0145);
const nCell = G.readUInt16LE(0), nVert = G.readUInt16LE(2), nEdge = G.readUInt16LE(4);
const ct = 6, bt = ct + nCell * 16;
const ZR = [90, 120, 180, 280, 450, 720];
const rdv = o => { const r = []; for (let k = 0; k < 3; k++) { const lo = G[o + k * 2], hi = G.readInt8(o + k * 2 + 1); r.push((hi * 128 + lo) / 16384); } return r; };

// все рёбра карты: концы — единичные векторы, текстуры сторон
const edges = [];
for (let c = 0; c < nCell; c++) {
	const co = ct + c * 16, cb = bt + G.readUInt16LE(co);   // ресурс v6: подблоки ячейки (Globe.cs)
	if (G.readUInt16LE(co + 2)) for (let k = 0; k < G[cb]; k++) {
	const o = cb + 2 + k * 16, bo = cb + G.readUInt16LE(o), vn = G.readUInt16LE(o + 2), en = G.readUInt16LE(o + 4);
	for (let e = 0; e < en; e++) {
		const eo = bo + vn * 6 + e * 6;
		edges.push({ A: rdv(bo + G.readUInt16LE(eo)), B: rdv(bo + G.readUInt16LE(eo + 2)), tl: G[eo + 4], tr: G[eo + 5] });
	}
	}
}

// Списки границ строк зума: запись { ph — фаза λC, c — амплитуда (доли R), tex — текстура
// справа }. Пересечение ребра со строкой при наклоне 0: x = R·c·sin(λC − λ0), z = c·cos(λC − λ0)
function rowLists(zoom) {
	const R = ZR[zoom], rows = [];
	for (let y = 0; y < 200; y++) {
		const Zr = (y + ROWOFF - 100) / R, list = [];
		if (Math.abs(Zr) < 1) for (const e of edges) {
			let A = e.A, B = e.B, right;
			if ((A[2] - Zr) * (B[2] - Zr) >= 0) continue;
			if (B[2] > A[2]) right = e.tl; else { right = e.tr; const t = A; A = B; B = t; }
			const t = (Zr - A[2]) / (B[2] - A[2]);
			const px = A[0] + (B[0] - A[0]) * t, py = A[1] + (B[1] - A[1]) * t;
			list.push({ ph: Math.atan2(py, px), c: Math.hypot(px, py), tex: right });
		}
		list.sort((u, v) => u.ph - v.ph);
		rows.push(list);
	}
	return rows;
}

function draw(rows, lonD, zoom) {
	const R = ZR[zoom], l0 = lonD * Math.PI / 180, out = new Int16Array(256 * 200).fill(-1);
	const TWO = Math.PI * 2, wrap = a => { a = (a + Math.PI) % TWO; return (a < 0 ? a + TWO : a) - Math.PI; };
	for (let y = 0; y < 200; y++) {
		const list = rows[y];
		if (!list.length) continue;
		const vis = [];
		for (let i = 0; i < list.length; i++) {
			const d = wrap(list[i].ph - l0);
			if (Math.abs(d) < Math.PI / 2) vis.push({ i, d, tex: list[i].tex, c: list[i].c });
		}
		if (!vis.length) continue;
		vis.sort((u, v) => u.d - v.d);          // порядок по Δ = порядок по x (x монотонен на передней стороне)
		// затравка: текстура справа от границы, предшествующей самой левой видимой (по кругу)
		let cur = list[(vis[0].i - 1 + list.length) % list.length].tex, x0 = 0;
		for (const b of vis) {
			let x = 128 + R * b.c * Math.sin(b.d) + SHIFT;   // +0.5 — эталон проверяет центры пикселей
			const RULE = process.env.RULE || "round";
			if (!PAIR) x = Math.round(x);
			else if (RULE === "floor") x = 2 * Math.floor(x / 2);
			else if (RULE === "ceil") x = 2 * Math.ceil(x / 2);
			else if (RULE === "half") x = 2 * Math.floor((x + 1) / 2);
			else x = 2 * Math.round(x / 2);
			for (let px = Math.max(0, x0); px < Math.min(256, x); px++) out[y * 256 + px] = cur;
			if (x > x0) x0 = x;
			cur = b.tex;
		}
		for (let px = Math.max(0, x0); px < 256; px++) out[y * 256 + px] = cur;
	}
	return out;
}

const zooms = (process.argv[2] || '0,2,5').split(',').map(Number);
for (const z of zooms) {
	const rows = rowLists(z);
	let cnt = 0;
	for (const l of rows) cnt += l.length;
	console.log(`зум ${z}: границ ${cnt}, строк с границами ${rows.filter(l => l.length).length}`);
	for (const lon of [0, 37, 120, 255]) {
		const mine = draw(rows, lon, z);
		const r = M.render(lon, 0, z, true);
		const norm = v => (v === 255 ? 13 : v);
		let dImg = 0, dTru = 0, dEng = 0, n = 0;
		for (let i = 0; i < 256 * 200; i++) {
			if (r.truth[i] < 0) continue;
			n++;
			const a = mine[i], b = norm(r.img[i]), t = norm(r.truth[i]);
			if (a !== b) dImg++;
			if (a !== t) dTru++;
			if (b !== t) dEng++;
		}
		console.log(`   λ0 = ${lon}°: прототип-эталон ${dTru} (${(100 * dTru / n).toFixed(2)} %), движок-эталон ${dEng} (${(100 * dEng / n).toFixed(2)} %), прототип-движок ${dImg}`);
	}
}

// разбор: худшие строки последнего вида
if (process.env.ROWS) {
	const z = zooms[0], rows = rowLists(z), lon = +process.env.ROWS;
	const mine = draw(rows, lon, z), r = M.render(lon, 0, z, true);
	const norm = v => (v === 255 ? 13 : v);
	const bad = [];
	for (let y = 0; y < 200; y++) {
		let d = 0, n = 0;
		for (let x = 0; x < 256; x++) { const i = y * 256 + x; if (r.truth[i] < 0) continue; n++; if (mine[i] !== norm(r.truth[i])) d++; }
		if (d) bad.push({ y, d, n, gr: rows[y].length });
	}
	bad.sort((a, b) => b.d - a.d);
	console.log('худшие строки (y, ошибок, пикселей, границ в списке):');
	for (const b of bad.slice(0, 12)) console.log(`   y=${b.y}: ${b.d}/${b.n}, границ ${b.gr}`);
	console.log('строк с ошибками:', bad.length, 'из них без границ:', bad.filter(b => !b.gr).length);
}
