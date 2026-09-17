// Путь «рёбра вида» (globe.md §12.11): для вида хранятся готовые куски границ карты —
// старт X в первой строке (пары, 8.8), наклон dX (пары на строку, 8.8), длина в строках,
// текстура справа от границы. Граница — дуга большого круга (как точные виды GVIEW): она режется
// на куски так, что X0 + k·dX даёт в каждой строке ту же пару, что и точная дуга, поэтому
// картинка совпадает с точным растеризатором. Куски связаны цепочками (продолжение — только
// поправка X), у начала цепочки — готовое место в упорядоченном списке активных рёбер: движок
// ничего не сравнивает и не сортирует, на строку — вставки по месту, вывод отрезков и сложения.
// Текстура левого края строки — список «с какой строки какая».
//
//   node tools/globe_edges.js [зумы 0,1,2] [шаг выборки видов для сверки, 0 — без сверки]
//   GAME=UFO; TILTMAX=27 (градусы; 90 — полный наклон); DEBUG=lon,lat,zoom [ROW=y]
//
// Формат вида (байты, little-endian):
//   u8 nSeed; nSeed x { u8 строка, u8 текстура } — текстура левого края с этой строки
//   группы начал по строкам: { u8 шаг строки от прошлой группы (первая — от строки 0), u8 число
//     начал; начала: u16 X0 + 64 пары, u8 место в списке активных, затем кусок }; группа 0, 0 — конец;
//     шаг больше 255 — пустые группы { 255, 0 }
//   кусок: u8 флаги: бит 7 — есть продолжение, бит 6 — длинная запись, биты 3..0 — текстура справа;
//     короткая: u8 (длина − 1) << 4 | старшие 4 бита dX, u8 младшие 8 бит dX (dX — 12 бит со знаком)
//     длинная: u8 длина − 1, i16 dX
//   продолжение (после куска с битом 7): i8 поправка X (−128 — дальше i16), затем кусок
'use strict';
const fs = require('fs');
const M = require('./globe_model.js');
const game = process.env.GAME || 'TFTD';
const pak = fs.readFileSync(`tmp/sd/OXZ/${game}/GEO.PAK`);
function res(id) {
	const n = pak.readUInt16LE(6);
	for (let i = 0; i < n; i++) {
		const e = 16 + i * 16;
		if (pak.readUInt16LE(e) === id) { const s = pak.readUInt16LE(e + 4), z = pak.readUInt32LE(e + 6); return pak.subarray(s * 512, s * 512 + z); }
	}
	throw 'no res ' + id;
}
const G = res(0x0145);
const nCell = G.readUInt16LE(0), ct = 6, bt = ct + nCell * 16;
const rdv = o => { const r = []; for (let k = 0; k < 3; k++) { const lo = G[o + k * 2], hi = G.readInt8(o + k * 2 + 1); r.push((hi * 128 + lo) / 16384); } return r; };
const ZR = [90, 120, 180, 280, 450, 720];
const XOFF = 64 * 256;                // X хранится со сдвигом +64 пары: u16 — пары −64..191 (диск z2 шире окна)
const BIAS = 192;                     // пара границы: (X + BIAS) >> 8 — центр точки и округление до пары (путь А)

// границы карты: концы (ключи вершин — байты ресурса), текстуры сторон
const edges = [];
for (let c = 0; c < nCell; c++) {
	const co = ct + c * 16, cb = bt + G.readUInt16LE(co);
	if (!G.readUInt16LE(co + 2)) continue;
	for (let k = 0; k < G[cb]; k++) {
		const r = cb + 2 + k * 16, vn = G.readUInt16LE(r + 2), en = G.readUInt16LE(r + 4), vb = cb + G.readUInt16LE(r);
		for (let i = 0; i < en; i++) {
			const eo = vb + vn * 6 + i * 6, oa = vb + G.readUInt16LE(eo), ob = vb + G.readUInt16LE(eo + 2);
			edges.push({ a: rdv(oa), b: rdv(ob), ka: G.subarray(oa, oa + 6).toString('hex'), kb: G.subarray(ob, ob + 6).toString('hex'), tl: G[eo + 4], tr: G[eo + 5] });
		}
	}
}

// пары диска строки — как tools/gen_globe_tab.js
function discPairs(R, y) {
	const dy = 2 * y + 1 - 200, rr = 4 * R * R - dy * dy;
	if (rr <= 0) return null;
	let s = 0;
	while ((s + 1) * (s + 1) < rr) s++;
	const lo = Math.max(0, (256 - s) >> 1), hi = Math.min(255, (255 + s) >> 1);
	if (hi < lo) return null;
	return [lo >> 1, hi >> 1];
}

// ---- куски границ вида
// Дуга проецируется частыми точками (8 на градус), видимая часть — до z = 0, монотонные по y участки;
// X в центре каждой строки (8.8 пар) — линейно между соседними точками. Затем точная подгонка кусков.
function viewPieces(lonD, latD, zoom) {
	const R = ZR[zoom], l0 = lonD * Math.PI / 180, C = latD * Math.PI / 180;
	const cl = Math.cos(l0), sl = Math.sin(l0), cc = Math.cos(C), sc = Math.sin(C);
	const P = q => { const W = q[0] * cl + q[1] * sl; return [128 + R * (q[1] * cl - q[0] * sl), 100 + R * (cc * q[2] - sc * W), cc * W + sc * q[2]]; };
	const pieces = [];
	for (const e of edges) {
		const a = e.a, b = e.b;
		const ang = Math.acos(Math.max(-1, Math.min(1, a[0] * b[0] + a[1] * b[1] + a[2] * b[2])));
		const N = Math.max(2, Math.ceil(ang * 180 / Math.PI * 8)), sn = Math.sin(ang);
		const pts = [];
		for (let i = 0; i <= N; i++) {
			const t = i / N;
			let p = a;
			if (sn > 1e-9) { const wa = Math.sin((1 - t) * ang) / sn, wb = Math.sin(t * ang) / sn; p = [a[0] * wa + b[0] * wb, a[1] * wa + b[1] * wb, a[2] * wa + b[2] * wb]; }
			const q = P(p); q.key = i === 0 ? e.ka : i === N ? e.kb : null; pts.push(q);
		}
		const segs = [];
		let cur = [];
		const cut = (r, q) => { const t = r[2] / (r[2] - q[2]); const m = [r[0] + (q[0] - r[0]) * t, r[1] + (q[1] - r[1]) * t, 0]; m.key = null; return m; };
		for (let i = 0; i < pts.length; i++) {
			const q = pts[i];
			if (q[2] >= 0) { if (!cur.length && i > 0) cur.push(cut(pts[i - 1], q)); cur.push(q); }
			else if (cur.length) { cur.push(cut(pts[i - 1], q)); segs.push(cur); cur = []; }
		}
		if (cur.length) segs.push(cur);
		for (const s0 of segs) {
			const mono = [];
			let pc = [s0[0]];
			for (let i = 1; i < s0.length; i++) {
				if (pc.length >= 2) {
					const d1 = pc[pc.length - 1][1] - pc[pc.length - 2][1], d2 = s0[i][1] - pc[pc.length - 1][1];
					if (d1 * d2 < 0) { mono.push(pc); const t = pc[pc.length - 1]; const m = [t[0], t[1], t[2]]; m.key = null; pc = [m]; }
				}
				pc.push(s0[i]);
			}
			mono.push(pc);
			for (let arc of mono) {
				const down = arc[arc.length - 1][1] > arc[0][1];
				if (!down) arc = arc.slice().reverse();
				const tex = down ? e.tl : e.tr;              // вниз по экрану: справа от границы — texL
				const r0 = Math.max(0, Math.ceil(arc[0][1] - 0.5)), r1 = Math.min(199, Math.ceil(arc[arc.length - 1][1] - 0.5) - 1);
				if (r1 < r0) continue;
				const xs = [];
				let j = 0;
				for (let r = r0; r <= r1; r++) {
					const yc = r + 0.5;
					while (j < arc.length - 2 && arc[j + 1][1] < yc) j++;
					const p1 = arc[j], p2 = arc[j + 1], t = (yc - p1[1]) / ((p2[1] - p1[1]) || 1e-9);
					xs.push((p1[0] + (p2[0] - p1[0]) * t) / 2 * 256);
				}
				let mn = Infinity, mx = -Infinity;
				for (const x of xs) { if (x < mn) mn = x; if (x > mx) mx = x; }
				if (mx < -256 || mn >= 128 * 256) continue;          // целиком вне окна
				fit(pieces, r0, xs, tex, arc[0].key, arc[arc.length - 1].key);
			}
		}
	}
	return pieces;
}

// точная подгонка: кусок [k0, k1) — целые X0, dX, при которых пара (X + BIAS) >> 8 в каждой строке
// та же, что у точной дуги; куски дуги сразу связаны цепочкой
function fit(out, r0, xs, tex, ka, kb) {
	const n = xs.length, pair = xs.map(x => Math.floor((Math.round(x) + BIAS) / 256));
	const L = pair.map(v => v * 256 - BIAS), U = pair.map(v => v * 256 - BIAS + 255);   // X в [L, U]
	let k0 = 0, prev = null;
	while (k0 < n) {
		let best = { k1: k0 + 1, X0: Math.min(U[k0], Math.max(L[k0], Math.round(xs[k0]))), dX: 0 };
		for (let k1 = k0 + 2; k1 <= n; k1++) {
			const m = k1 - 1 - k0;
			const dlo = Math.ceil((L[k1 - 1] - U[k0]) / m), dhi = Math.floor((U[k1 - 1] - L[k0]) / m);
			const dm = Math.round((xs[k1 - 1] - xs[k0]) / m);
			let found = null;
			for (let s = 0; s <= dhi - dlo && !found; s++) for (const d of [dm + s, dm - s]) {
				if (d < dlo || d > dhi) continue;
				let lo = -1e9, hi = 1e9;
				for (let k = k0; k < k1; k++) { lo = Math.max(lo, L[k] - (k - k0) * d); hi = Math.min(hi, U[k] - (k - k0) * d); if (lo > hi) break; }
				if (lo <= hi) { found = { k1, X0: Math.min(hi, Math.max(lo, Math.round(xs[k0]))), dX: d }; break; }
			}
			if (!found) break;
			best = found;
		}
		const p = { r0: r0 + k0, rows: best.k1 - k0, X0: best.X0, dX: best.dX, tex, xs: xs.slice(k0, best.k1), ka: k0 === 0 ? ka : null, kb: best.k1 === n ? kb : null };
		if (prev) { prev.next = p; p.prev = prev; }
		out.push(p);
		prev = p;
		k0 = best.k1;
	}
}

// цепочки между дугами: в вершине (и строке) кончающиеся и начинающиеся куски — по порядку слева
function chains(list) {
	const ends = new Map(), starts = new Map();
	for (const e of list) {
		if (e.kb && !e.next) { const k = e.kb + ':' + (e.r0 + e.rows); if (!ends.has(k)) ends.set(k, []); ends.get(k).push(e); }
		if (e.ka && !e.prev) { const k = e.ka + ':' + e.r0; if (!starts.has(k)) starts.set(k, []); starts.get(k).push(e); }
	}
	for (const [k, es] of ends) {
		const fs_ = starts.get(k);
		if (!fs_) continue;
		es.sort((p, q) => p.xs[p.rows - 1] - q.xs[q.rows - 1]);
		fs_.sort((p, q) => p.xs[0] - q.xs[0]);
		for (let i = 0; i < Math.min(es.length, fs_.length); i++) { es[i].next = fs_[i]; fs_[i].prev = es[i]; }
	}
	return list.filter(e => !e.prev);
}

// текстура левого края строки: эталон в центре первой пары диска
function seeds(lonD, latD, zoom) {
	const R = ZR[zoom], l0 = lonD * Math.PI / 180, C = latD * Math.PI / 180;
	const cl = Math.cos(l0), sl = Math.sin(l0), cc = Math.cos(C), sc = Math.sin(C);
	const ex = [-sl, cl, 0], ey = [-sc * cl, -sc * sl, cc], ez = [cc * cl, cc * sl, sc];
	const s = [];
	for (let y = 0; y < 200; y++) {
		const pr = discPairs(R, y);
		if (!pr) { s.push(-1); continue; }
		const px = (pr[0] * 2 + 0.5 - 128) / R, py = (y + 0.5 - 100) / R;
		const zz = Math.sqrt(Math.max(0, 1 - px * px - py * py));
		const p = [px * ex[0] + py * ey[0] + zz * ez[0], px * ex[1] + py * ey[1] + zz * ez[1], px * ex[2] + py * ey[2] + zz * ez[2]];
		const t = M.truthAt(p);
		s.push(t === 255 ? 13 : t);
	}
	return s;
}

// ---- места вставки: проход строк с точным порядком (как будет в движке, но по точным x)
function places(heads) {
	const byRow = new Map();
	for (const h of heads) { if (!byRow.has(h.r0)) byRow.set(h.r0, []); byRow.get(h.r0).push(h); }
	const active = [];
	let bad = 0;
	const xAt = (a, y) => a.xs[y - a.r0];
	for (let y = 0; y < 200; y++) {
		const hs = (byRow.get(y) || []).sort((p, q) => p.xs[0] - q.xs[0]);
		for (const h of hs) {
			let k = 0;
			while (k < active.length && xAt(active[k], y) <= h.xs[0]) k++;
			h.pos = k;
			active.splice(k, 0, h);
		}
		for (let k = 0; k < active.length; k++) {
			const a = active[k];
			if (k > 0 && xAt(active[k - 1], y) > xAt(a, y) + 1) bad++;
			if (y === a.r0 + a.rows - 1) { if (a.next) active[k] = a.next; else { active.splice(k, 1); k--; } }
		}
	}
	return bad;
}

// ---- упаковка
function pack(heads, seed) {
	const b = [], sd = [];
	let last = -2;
	for (let y = 0; y < 200; y++) if (seed[y] >= 0 && seed[y] !== last) { sd.push([y, seed[y]]); last = seed[y]; }
	b.push(sd.length);
	for (const [y, t] of sd) b.push(y, t);
	const byRow = new Map();
	for (const h of heads) { if (!byRow.has(h.r0)) byRow.set(h.r0, []); byRow.get(h.r0).push(h); }
	const stat = { pieces: 0, long: 0, cont: 0, contLong: 0, heads: heads.length };
	const piece = e => {
		const shortOk = e.rows <= 16 && e.dX >= -2048 && e.dX < 2048;
		b.push((e.next ? 0x80 : 0) | (shortOk ? 0 : 0x40) | e.tex);
		if (shortOk) b.push(((e.rows - 1) << 4) | ((e.dX >> 8) & 15), e.dX & 255);
		else { b.push(e.rows - 1, e.dX & 255, (e.dX >> 8) & 255); stat.long++; }
		stat.pieces++;
	};
	let prevRow = 0;
	for (const r of [...byRow.keys()].sort((a, c) => a - c)) {
		const hs = byRow.get(r).sort((p, q) => p.pos - q.pos);   // вставки по возрастанию места
		let d = r - prevRow; prevRow = r;
		while (d > 255) { b.push(255, 0); d -= 255; }
		b.push(d, hs.length);
		for (const h of hs) {
			const Xs = h.X0 + XOFF;
			if (Xs < 0 || Xs > 65535) throw 'X вне диапазона';
			b.push(Xs & 255, (Xs >> 8) & 255, h.pos);
			for (let e = h; ; ) {
				piece(e);
				if (!e.next) break;
				const f = e.next, corr = f.X0 - (e.X0 + e.rows * e.dX);
				if (corr > -128 && corr < 128) b.push(corr & 255); else { b.push(0x80, corr & 255, (corr >> 8) & 255); stat.contLong++; }
				stat.cont++;
				e = f;
			}
		}
	}
	b.push(0, 0);
	return { bytes: Buffer.from(b), stat };
}

// ---- распаковка и рендер (так делает движок)
function render(buf, zoom) {
	const R = ZR[zoom];
	const img = new Int16Array(256 * 200).fill(-1);
	let p = 0;
	const nSeed = buf[p++], seedRow = [], seedTex = [];
	for (let i = 0; i < nSeed; i++) { seedRow.push(buf[p++]); seedTex.push(buf[p++]); }
	const readPiece = s => {
		const f = buf[s.p++];
		s.more = !!(f & 0x80); s.tex = f & 15;
		if (f & 0x40) { s.rows = buf[s.p++] + 1; s.dX = (buf[s.p] | (buf[s.p + 1] << 8)) << 16 >> 16; s.p += 2; }
		else { const h = buf[s.p++], l = buf[s.p++]; s.rows = (h >> 4) + 1; s.dX = ((((h & 15) << 8) | l) << 20) >> 20; }
	};
	let groupRow = buf[p], groupN = buf[p + 1];
	p += 2;
	if (groupN === 0 && groupRow === 0) groupRow = 999;
	const active = [];
	let seedI = 0, cur = 13;
	const ops = { ins: 0, add: 0, runs: 0, cont: 0 };
	for (let y = 0; y < 200; y++) {
		while (groupRow === y) {
			for (let i = 0; i < groupN; i++) {
				const s = { X: (buf[p] | (buf[p + 1] << 8)) - XOFF, p: p + 3 };
				const pos = buf[p + 2];
				readPiece(s);
				active.splice(pos, 0, s); ops.ins++;
				p = skipChain(buf, s.p, s.more);
			}
			const d = buf[p++], n = buf[p++];
			if (d === 0 && n === 0) { groupRow = 999; break; }
			groupRow += d; groupN = n;
		}
		while (seedI < nSeed && seedRow[seedI] === y) cur = seedTex[seedI++];
		const pr = discPairs(R, y);
		if (global.DBGROW === y) console.log('active', JSON.stringify(active.map(a => [(a.X / 256).toFixed(2), a.tex, a.rows])));
		let t = cur, x0 = pr ? pr[0] : 0;
		const put = (xa, xb, tex) => { for (let x = xa * 2; x < xb * 2; x++) if (x >= 0 && x < 256) img[y * 256 + x] = tex; };
		for (let k = 0; k < active.length; k++) {
			const s = active[k];
			if (pr) {
				let bx = (s.X + BIAS) >> 8;
				if (bx < pr[0]) bx = pr[0];
				if (bx > pr[1] + 1) bx = pr[1] + 1;
				if (bx > x0) { put(x0, bx, t); x0 = bx; ops.runs++; }
			}
			t = s.tex;
			s.X += s.dX; ops.add++;
			if (--s.rows === 0) {
				if (s.more) {
					let c = buf[s.p++] << 24 >> 24;
					if (c === -128) { c = (buf[s.p] | (buf[s.p + 1] << 8)) << 16 >> 16; s.p += 2; }
					s.X += c; readPiece(s); ops.cont++;
				} else { active.splice(k, 1); k--; }
			}
		}
		if (pr && x0 <= pr[1]) { put(x0, pr[1] + 1, t); ops.runs++; }
	}
	return { img, ops };
}
function skipChain(buf, p, more) {
	while (more) {
		if (buf[p++] === 0x80) p += 2;
		const f = buf[p++];
		more = !!(f & 0x80);
		p += (f & 0x40) ? 3 : 2;
	}
	return p;
}

function buildView(lon, lat, z, withSeeds) {
	const list = viewPieces(lon, lat, z);
	const heads = chains(list);
	const bad = places(heads);
	const seed = withSeeds ? seeds(lon, lat, z) : new Array(200).fill(13);
	return { ...pack(heads, seed), bad, list };
}

module.exports = { buildView, render, discPairs, ZR, BIAS };

if (require.main === module && process.env.DEBUG) {
	const [lon, lat, z] = process.env.DEBUG.split(',').map(Number);
	const v = buildView(lon, lat, z, true);
	global.DBGROW = +(process.env.ROW || -1);
	const r = render(v.bytes, z), t = M.render(lon, lat, z, true);
	let d = 0, n = 0;
	for (let q = 0; q < 256 * 200; q++) { if (t.truth[q] < 0) continue; n++; if (r.img[q] !== (t.truth[q] === 255 ? 13 : t.truth[q])) d++; }
	console.log(`вид ${v.bytes.length} байт, кусков ${v.stat.pieces}, начал ${v.stat.heads}; расхождение ${(100 * d / n).toFixed(3)} %; нарушений порядка ${v.bad}`);
	process.exit(0);
}

// ---- сетка и прогон
if (require.main === module) {
	const TILTMAX = +(process.env.TILTMAX ?? 27);
	const grid = z => { const lonStep = 15 / (z + 1), tiltStep = lonStep * 0.6, k = Math.floor(TILTMAX / tiltStep + 1e-9); return { lonStep, tiltStep, nLon: Math.round(360 / lonStep), k, nTilt: 2 * k + 1 }; };
	const zooms = (process.argv[2] || '0,1,2').split(',').map(Number);
	const CHECK = +(process.argv[3] ?? 0);
	let allBytes = 0, allViews = 0;
	for (const z of zooms) {
		const g = grid(z);
		let tot = 0, max = 0, nv = 0, nP = 0, nH = 0, nCont = 0, nLong = 0, bad = 0, checked = 0, dTru = 0, dMax = 0, nPix = 0;
		const sumOps = { ins: 0, add: 0, runs: 0, cont: 0 };
		for (let j = -g.k; j <= g.k; j++)
			for (let i = 0; i < g.nLon; i++) {
				const lon = i * g.lonStep, lat = j * g.tiltStep;
				const doCheck = CHECK > 0 && i % CHECK === 0 && (j + g.k) % CHECK === 0;
				const v = buildView(lon, lat, z, doCheck);
				tot += v.bytes.length; if (v.bytes.length > max) max = v.bytes.length; nv++;
				nP += v.stat.pieces; nH += v.stat.heads; nCont += v.stat.cont; nLong += v.stat.long; bad += v.bad;
				if (doCheck) {
					const r = render(v.bytes, z);
					for (const k in sumOps) sumOps[k] += r.ops[k];
					const t = M.render(lon, lat, z, true);
					let d = 0, n = 0;
					for (let q = 0; q < 256 * 200; q++) { if (t.truth[q] < 0) continue; n++; if (r.img[q] !== (t.truth[q] === 255 ? 13 : t.truth[q])) d++; }
					dTru += d; nPix += n; checked++; if (d / n > dMax) dMax = d / n;
				}
			}
		allBytes += tot; allViews += nv;
		console.log(`${game} z${z}: видов ${g.nLon} x ${g.nTilt} = ${nv}; кусков ср ${(nP / nv).toFixed(0)}, начал ${(nH / nv).toFixed(0)}, продолжений ${(nCont / nv).toFixed(0)}, длинных ${(nLong / nv).toFixed(0)}; ` +
			`вид ср ${(tot / nv / 1024).toFixed(2)} КБ, макс ${(max / 1024).toFixed(2)} КБ; всего ${(tot / 1048576).toFixed(2)} МБ; нарушений порядка ${bad}`);
		if (checked) console.log(`   сверка ${checked} видов: расхождение ср ${(100 * dTru / nPix).toFixed(3)} %, худший ${(100 * dMax).toFixed(2)} %; на вид: вставок ${(sumOps.ins / checked).toFixed(0)}, сложений ${(sumOps.add / checked).toFixed(0)}, отрезков ${(sumOps.runs / checked).toFixed(0)}, продолжений ${(sumOps.cont / checked).toFixed(0)}`);
	}
	console.log(`${game} итого: видов ${allViews}, ${(allBytes / 1048576).toFixed(2)} МБ`);
}
