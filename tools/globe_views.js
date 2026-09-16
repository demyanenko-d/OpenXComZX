// Путь А (globe.md §12.5): точный растеризатор вида глобуса — образец для конвертера.
// Для вида (λ0, наклон C, зум) строит по каждой строке экрана список отрезков (длина, текстура)
// и сверяет картинку с попиксельным эталоном (tools/globe_model.js truth).
//
// Геометрия. Оси вида: ex = (−sinλ0, cosλ0, 0), ey = (−sinC·cosλ0, −sinC·sinλ0, cosC),
// ez = (cosC·cosλ0, cosC·sinλ0, sinC); экран x = 128 + R·(ex·P), y = 100 + R·(ey·P),
// видно при ez·P > 0. Строка экрана — окружность на шаре: ey·P = v = (y + 0.5 − 100)/R,
// P = v·ey + c·(cosθ·ez + sinθ·ex), c = √(1 − v²); видно |θ| < 90°, x = 128 + R·c·sinθ + 0.5,
// и x монотонен по θ. Граница карты — дуга большого круга с нормалью N = A × B: N·P = 0 даёт
// a·cosθ + b·sinθ = d (a = N·ez, b = N·ex, d = −v·(N·ey)/c) — до двух корней, каждый
// проверяется на попадание внутрь дуги. Текстура со стороны большего x: если дуга в точке идёт
// вниз по экрану (ey·(N × P) > 0) — texL, иначе texR.
//
//   node tools/globe_views.js [зумы] ; GAME=UFO ; VIEWS="lon,lat;lon,lat" ; PPM=tmp/v.ppm
'use strict';
const fs = require('fs');
const M = require('./globe_model.js');
const game = process.env.GAME || 'TFTD';
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
const nCell = G.readUInt16LE(0);
const ct = 6, bt = ct + nCell * 16;
const ZR = [90, 120, 180, 280, 450, 720];
const PAIR = +(process.env.PAIR ?? 1);              // границы по парам пикселей (гранулярность DMA)
const SHIFT = +(process.env.SHIFT ?? 0.5);          // эталон проверяет центры пикселей

const cross = (a, b) => [a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]];
const dot = (a, b) => a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
const unit = a => { const l = Math.hypot(a[0], a[1], a[2]); return [a[0] / l, a[1] / l, a[2] / l]; };
const rdv = o => { const r = []; for (let k = 0; k < 3; k++) { const lo = G[o + k * 2], hi = G.readInt8(o + k * 2 + 1); r.push((hi * 128 + lo) / 16384); } return r; };

// все границы карты: концы дуги — единичные векторы, нормаль, текстуры сторон
const edges = [];
for (let c = 0; c < nCell; c++) {
	const o = ct + c * 16, bo = bt + G.readUInt16LE(o), vn = G.readUInt16LE(o + 2), en = G.readUInt16LE(o + 4);
	for (let e = 0; e < en; e++) {
		const eo = bo + vn * 6 + e * 6;
		const A = rdv(bo + G.readUInt16LE(eo)), B = rdv(bo + G.readUInt16LE(eo + 2));
		const N = cross(A, B);
		if (Math.hypot(N[0], N[1], N[2]) < 1e-12) continue;
		const n = unit(N);
		// P(t) = A·cos t + M·sin t, t от 0 до tAB — параметризация дуги (для отсева по строкам)
		const M2 = cross(n, A);
		edges.push({ A, B, N: n, M: M2, tab: Math.atan2(dot(cross(A, B), n), dot(A, B)), tl: G[eo + 4], tr: G[eo + 5] });
	}
}

// Отсев: диапазон ey·P вдоль дуги (синусоида p·cos t + q·sin t — концы плюс экстремум внутри)
function vRange(e, ey) {
	const p = dot(ey, e.A), q = dot(ey, e.M);
	let lo = Math.min(p, dot(ey, e.B)), hi = Math.max(p, dot(ey, e.B));
	const m = Math.hypot(p, q);
	if (m > 1e-12) {
		const t0 = Math.atan2(q, p);
		for (const t of [t0, t0 + Math.PI, t0 - Math.PI, t0 + 2 * Math.PI]) {
			if (t <= 1e-12 || t >= e.tab - 1e-12) continue;
			const v = p * Math.cos(t) + q * Math.sin(t);
			if (v < lo) lo = v;
			if (v > hi) hi = v;
		}
	}
	return [lo, hi];
}

// Пары диска строки (globe.c rows_init): пиксель внутри, если (2i + 1 − 256)² + (2j + 1 − 200)² < 4R²
function discPairs(R, y) {
	const dy = 2 * y + 1 - 200, rr = 4 * R * R - dy * dy;
	if (rr <= 0) return null;
	const h = Math.sqrt(rr);
	let lo = Math.ceil((255 - h) / 2), hi = Math.floor((255 + h) / 2);   // пиксели внутри
	if (lo < 0) lo = 0;
	if (hi > 255) hi = 255;
	if (hi < lo) return null;
	return [lo >> 1, hi >> 1];                                          // пара — если внутри хоть один пиксель
}

// Список отрезков вида: для каждой строки — [pl, pr, [ [len, tex], ... ] ] в парах пикселей
function viewRuns(lonD, latD, zoom) {
	const R = ZR[zoom], l0 = lonD * Math.PI / 180, C = latD * Math.PI / 180;
	const cl = Math.cos(l0), sl = Math.sin(l0), cc = Math.cos(C), sc = Math.sin(C);
	const ex = [-sl, cl, 0], ey = [-sc * cl, -sc * sl, cc], ez = [cc * cl, cc * sl, sc];
	const rows = [];
	const bucket = [];                                   // рёбра, задевающие строку
	for (let y = 0; y < 200; y++) bucket.push([]);
	for (const e of edges) {
		const [lo, hi] = vRange(e, ey);
		let y0 = Math.ceil(99.5 + R * lo), y1 = Math.floor(99.5 + R * hi);
		if (y0 < 0) y0 = 0;
		if (y1 > 199) y1 = 199;
		for (let y = y0; y <= y1; y++) bucket[y].push(e);
	}
	for (let y = 0; y < 200; y++) {
		const pr = discPairs(R, y);
		if (!pr) { rows.push(null); continue; }
		const v = (y + 0.5 - 100) / R;
		const c = Math.sqrt(Math.max(0, 1 - v * v));
		const list = [];
		if (c > 1e-9) for (const e of bucket[y]) {
			const a = dot(e.N, ez), b = dot(e.N, ex), d = -v * dot(e.N, ey) / c;
			const m = Math.hypot(a, b);
			if (m < 1e-12 || Math.abs(d) > m) continue;
			const ph = Math.atan2(b, a), w = Math.acos(Math.max(-1, Math.min(1, d / m)));
			for (const s of [w, -w]) {
				const th = ph + s;
				const ct_ = Math.cos(th), st = Math.sin(th);
				const P = [v * ey[0] + c * (ct_ * ez[0] + st * ex[0]),
				           v * ey[1] + c * (ct_ * ez[1] + st * ex[1]),
				           v * ey[2] + c * (ct_ * ez[2] + st * ex[2])];
				if (dot(cross(e.A, P), e.N) <= 0 || dot(cross(P, e.B), e.N) <= 0) continue;
				const T = cross(e.N, P);                     // касательная дуги A -> B
				list.push({ th: th > Math.PI ? th - 2 * Math.PI : th < -Math.PI ? th + 2 * Math.PI : th,
				            tex: dot(ey, T) > 0 ? e.tl : e.tr });
			}
		}
		list.sort((u, w) => u.th - w.th);
		// видимая часть окружности — |θ| < 90°; затравка — текстура перед первой видимой по кругу
		let i0 = -1;
		for (let i = 0; i < list.length; i++) if (list[i].th > -Math.PI / 2) { i0 = i; break; }
		let cur;
		if (!list.length) cur = texAt([v * ey[0] + c * ez[0], v * ey[1] + c * ez[1], v * ey[2] + c * ez[2]]);
		else if (i0 < 0) cur = list[list.length - 1].tex;
		else cur = list[(i0 - 1 + list.length) % list.length].tex;
		const runs = [];
		let x0 = pr[0] * 2;
		if (i0 >= 0) for (let i = i0; i < list.length; i++) {
			const b = list[i];
			if (b.th >= Math.PI / 2) break;
			let x = 128 + R * c * Math.sin(b.th) + SHIFT;
			x = PAIR ? 2 * Math.round(x / 2) : Math.round(x);
			if (x > x0) {
				const xe = Math.min(x, pr[1] * 2 + 2);
				if (xe > x0) { runs.push([(xe - x0) / 2, cur]); x0 = xe; }
			}
			cur = b.tex;
		}
		if (x0 <= pr[1] * 2 + 1) runs.push([(pr[1] * 2 + 2 - x0) / 2, cur]);
		rows.push([pr[0], pr[1], runs]);
	}
	return rows;
}

// текстура точки (для строк совсем без границ) — эталонной пробой
const TEXAT = require('./globe_model.js');
function texAt(p) { return TEXAT.truthAt ? TEXAT.truthAt(p) : 13; }

function draw(rows) {
	const out = new Int16Array(256 * 200).fill(-1);
	for (let y = 0; y < 200; y++) {
		const r = rows[y];
		if (!r) continue;
		let x = r[0] * 2;
		for (const [len, tex] of r[2]) {
			for (let k = 0; k < len * 2; k++, x++) if (x >= 0 && x < 256) out[y * 256 + x] = tex;
		}
	}
	return out;
}

const zooms = (process.argv[2] || '0,2,5').split(',').map(Number);

// Проверка GVIEW.PAK конвертера: разбор вида из пакета и сверка с эталоном (PAK=1 [шаг])
if (process.env.PAK) {
	const gv = fs.readFileSync(`tmp/sd/OXZ/${game}/GVIEW.PAK`);
	if (gv.readUInt32LE(0) !== 0x31575647) throw 'не GVW1';
	const nZ = gv.readUInt16LE(4), z0 = gv.readUInt16LE(6);
	const step = +(process.env.PAK) || 1;
	let worst = 0, worstAt = '', nv = 0;
	for (let zi = 0; zi < nZ; zi++) {
		const o = 8 + zi * 16;
		const nLon = gv.readUInt16LE(o), nTilt = gv.readUInt16LE(o + 2), tsQ = gv.readUInt16LE(o + 4);
		const R = gv.readUInt16LE(o + 6), idxSec = gv.readUInt32LE(o + 8), datSec = gv.readUInt32LE(o + 12);
		const z = z0 + zi, k = (nTilt - 1) >> 1, lonStep = 360 / nLon, tiltStep = tsQ * 360 / 65536;
		let sum = 0, cnt = 0;
		for (let j = 0; j < nTilt; j += step)
			for (let i = 0; i < nLon; i += step) {
				const vi = j * nLon + i, sOff = gv.readUInt16LE(idxSec * 512 + vi * 2);
				let p = (datSec + sOff) * 512;
				const img = new Int16Array(256 * 200).fill(-1);
				for (let y = 0; y < 200; y++) {
					const nr = gv[p++];
					if (!nr) continue;
					const pr = discPairs(R, y);
					let x = pr[0] * 2;
					for (let r = 0; r < nr; r++) {
						const len = gv[p++] + 1, tex = gv[p++];
						for (let q = 0; q < len * 2; q++, x++) if (x >= 0 && x < 256) img[y * 256 + x] = tex;
					}
				}
				const t = M.render(i * lonStep, (j - k) * tiltStep, z, true);
				let d = 0, n = 0;
				for (let q = 0; q < 256 * 200; q++) {
					if (t.truth[q] < 0) continue;
					n++;
					if (img[q] !== (t.truth[q] === 255 ? 13 : t.truth[q])) d++;
				}
				const pc = 100 * d / n;
				sum += pc; cnt++; nv++;
				if (pc > worst) { worst = pc; worstAt = `зум ${z}, λ0 ${(i * lonStep).toFixed(1)}°, наклон ${((j - k) * tiltStep).toFixed(1)}°`; }
			}
		console.log(`зум ${z}: ${nLon} x ${nTilt}, R ${R}; проверено ${cnt} видов, среднее расхождение ${(sum / cnt).toFixed(3)} %`);
	}
	console.log(`всего ${nv} видов, худший ${worst.toFixed(2)} % (${worstAt})`);
	process.exit(0);
}

// Сетка видов: поворот 15° / (зум + 1) (scr_geo.c rotate), наклон — 0.6 от шага поворота,
// в пределах ±TILTMAX (по умолчанию 27° — «30 % от полного»)
const TILTMAX = +(process.env.TILTMAX ?? 27), TILTMIN = +(process.env.TILTMIN ?? 3);
const grid = z => {
	const lonStep = 15 / (z + 1), tiltStep = Math.max(lonStep * 0.6, TILTMIN);
	const k = Math.floor(TILTMAX / tiltStep + 1e-9);
	return { lonStep, tiltStep, nLon: Math.round(360 / lonStep), k, nTilt: 2 * k + 1 };
};
if (process.env.SIZE) {
	let all = 0, allV = 0;
	for (const z of zooms) {
		const g = grid(z);
		let tot = 0, max = 0, nv = 0;
		for (let i = 0; i < g.nLon; i++)
			for (let j = -g.k; j <= g.k; j++) {
				const rows = viewRuns(i * g.lonStep, j * g.tiltStep, z);
				let b = 200;                                  // байт счётчиков строк
				for (const r of rows) if (r) b += 2 + 2 * r[2].length;   // pl, pr + отрезки
				tot += b;
				if (b > max) max = b;
				nv++;
			}
		const sec = Math.ceil(max / 512);
		console.log(`зум ${z}: шаг ${g.lonStep}° / ${g.tiltStep.toFixed(2)}°, видов ${g.nLon} x ${g.nTilt} = ${nv}; ` +
			`вид средн. ${(tot / nv / 1024).toFixed(1)} КБ, макс ${(max / 1024).toFixed(1)} КБ (${sec} сект.); ` +
			`всего ${(nv * sec * 512 / 1048576).toFixed(1)} МБ с выравниванием`);
		all += nv * sec * 512;
		allV += nv;
	}
	console.log(`итого: видов ${allV}, данных ${(all / 1048576).toFixed(1)} МБ`);
	process.exit(0);
}
const views = (process.env.VIEWS || '0,0;37,0;120,18;255,-27').split(';').map(s => s.split(',').map(Number));
for (const z of zooms) {
	for (const [lon, lat] of views) {
		const rows = viewRuns(lon, lat, z);
		let nr = 0, bytes = 200;
		for (const r of rows) if (r) { nr += r[2].length; bytes += 2 * r[2].length; }
		const mine = draw(rows);
		const t = M.render(lon, lat, z, true);
		const norm = q => (q === 255 ? 13 : q);
		let dTru = 0, dEng = 0, n = 0;
		for (let i = 0; i < 256 * 200; i++) {
			if (t.truth[i] < 0) continue;
			n++;
			if (mine[i] !== norm(t.truth[i])) dTru++;
			if (norm(t.img[i]) !== norm(t.truth[i])) dEng++;
		}
		console.log(`зум ${z} λ0 = ${lon}° наклон ${lat}°: отрезков ${nr}, вид ${(bytes / 1024).toFixed(1)} КБ; ` +
			`растеризатор-эталон ${(100 * dTru / n).toFixed(2)} %, движок-эталон ${(100 * dEng / n).toFixed(2)} %`);
		if (process.env.ROWS) {
			const bad = [];
			for (let y = 0; y < 200; y++) {
				let d = 0, nn = 0, out = 0;
				for (let x = 0; x < 256; x++) {
					const i = y * 256 + x;
					if (t.truth[i] < 0) continue;
					nn++;
					if (mine[i] < 0) out++;
					else if (mine[i] !== norm(t.truth[i])) d++;
				}
				if (d + out) bad.push({ y, d, out, nn, r: rows[y] ? rows[y][2].length : -1 });
			}
			bad.sort((a, b) => (b.d + b.out) - (a.d + a.out));
			for (const b of bad.slice(0, +process.env.ROWS)) console.log(`   y=${b.y}: ошибок ${b.d}, не закрашено ${b.out}, пикселей ${b.nn}, отрезков ${b.r}`);
			console.log(`   строк с ошибками: ${bad.length}`);
		}
	}
}
