// Модель рендера глобуса М4в (плоская карта GLOBE, src/ui/globe_s.s) и сверка с эталоном —
// попиксельным «художником» по многоугольникам WORLD.DAT (как OpenXcom). Данные — tmp/sd после
// сборки. node tools\globe_model.js [lon lat zoom] — PNG в tmp/ (кадр, эталон, расхождения);
// GAME=UFO — вторая игра; SWEEP=1 — прогон по сетке видов; ключи алгоритма по умолчанию —
// как в движке (OLD=1 — выключить все и включать по одному: EXACT=1 Z16=1 …).
'use strict';
let nRep = 0;
const fs = require('fs'), zlib = require('zlib');
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
const G = res(0x0145), TEX = res(0x0142);
const dir = game === 'TFTD' ? 'Steam/X-COM Terror from the Deep/TFD' : 'Steam/XCom UFO Defense/XCOM';
const pb = fs.readFileSync(dir + '/GEODATA/PALETTES.DAT');
const pal = []; for (let i = 0; i < 256; i++) pal.push([pb[i * 3] * 255 / 63 | 0, pb[i * 3 + 1] * 255 / 63 | 0, pb[i * 3 + 2] * 255 / 63 | 0]);
const OCEAN = game === 'TFTD' ? 16 : 192;
const nCell = G.readUInt16LE(0), nVert = G.readUInt16LE(2), nEdge = G.readUInt16LE(4);
const ct = 6, bt = ct + nCell * 16, gt = bt + nVert * 6 + nEdge * 6;
const rdv = o => { const r = []; for (let k = 0; k < 3; k++) { const lo = G[o + k * 2], hi = G.readInt8(o + k * 2 + 1); r.push((hi * 128 + lo) / 16384); } return r; };
const ZR = [90, 120, 180, 280, 450, 720];
// По умолчанию — как движок (globe_s.s); OLD=1 — все ключи выключены (опыты): тогда их
// включают по одному (EXACT=1 Z16=1 …)
const ENG = !process.env.OLD;
const flag = (n, d) => process.env[n] !== undefined ? !!+process.env[n] || process.env[n] === 'true' : d;
const MODE = process.env.MODE || 'sort';    // sort — пересортировка по x каждую строку; topo — порядок вставки
const HITOL = flag('HITOL', ENG);           // допуск сортировки по старшим байтам u (разница > 1)
const TOL = +(process.env.TOL ?? 64);
const LOCAL = flag('LOCAL', false), IMM = flag('IMM', false);
const GATE = flag('GATE', ENG);             // полная починка только в строке со вставками, после сдвига сортировкой, при событии края
const CHAIN = flag('CHAIN', false);         // починка кучек цепочкой по текстурам (опыт)
const NOLC = flag('NOLC', false), ITH = +(process.env.ITH || (ENG ? 128 : 256)), INSLIMB = flag('INSLIMB', ENG);
const INSFIX2 = flag('INSFIX2', ENG);       // вставка в кучку (соседи ближе ITH) на место, согласованное по текстурам; INSLIMB — только зумы 0–1
const INS2P = flag('INS2P', ENG);           // INSFIX2 после вставки всех новых рёбер строки и починки пар (ai_redo)
const LDEFER = flag('LDEFER', ENG);         // левая текстура неизвестна — первую пару чинить последней (ael_fix)
const DELGATE = flag('DELGATE', ENG);       // починка и после удаления ребра, если новые соседи несогласованы (ael_step)
const LIMBKEY = flag('LIMBKEY', false);     // события края диска в строке — по ходу вдоль края (опыт)
const BANDGRID = flag('BANDGRID', ENG);     // полосы строк без рёбер — текстура по сетке 5° в точке полосы (globe.c bands)
const bandStat = { n: 0, ok: 0 };
const INSFIX = flag('INSFIX', false);       // вставка ищет согласованное место среди близких (опыт)
const Z16 = flag('Z16', ENG);               // z вершин в Q12 (16 бит), иначе Q6
const EXACT = flag('EXACT', ENG);           // целочисленная арифметика движка
const REPAIR = !flag('NOREPAIR', false), LOOK = !flag('NOLOOK', ENG);   // LOOK — «tl следующего» при несогласованности (опыт)

// ---- эталон: многоугольники файла, проба «последний содержащий» (чёт-нечет в гномонической)
const dat = fs.readFileSync(dir + '/GEODATA/WORLD.DAT');
const polys = [];
for (let o = 0; o + 20 <= dat.length; o += 20) {
	const v = []; for (let i = 0; i < 10; i++) v.push(dat.readInt16LE(o + i * 2));
	const n = v[6] !== -1 ? 4 : 3, P = [];
	for (let i = 0; i < n; i++) { const l = v[i * 2] / 8 * Math.PI / 180, a = v[i * 2 + 1] / 8 * Math.PI / 180; P.push([Math.cos(a) * Math.cos(l), Math.cos(a) * Math.sin(l), Math.sin(a)]); }
	const c = [0, 0, 0]; for (const q of P) for (let k = 0; k < 3; k++) c[k] += q[k];
	const cl = Math.hypot(...c); for (let k = 0; k < 3; k++) c[k] /= cl;
	let cosR = 1; for (const q of P) cosR = Math.min(cosR, q[0] * c[0] + q[1] * c[1] + q[2] * c[2]);
	const t = Math.abs(c[2]) < 0.9 ? [0, 0, 1] : [1, 0, 0];
	const cr = (a, b) => [a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]];
	let U = cr(t, c); const ul = Math.hypot(...U); U = U.map(x => x / ul); const W = cr(c, U);
	const dot = (a, b) => a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
	polys.push({ c, cosR: cosR - 1e-7, U, W, px: P.map(q => dot(q, U) / dot(q, c)), py: P.map(q => dot(q, W) / dot(q, c)), tex: v[8] & 15 });
}
function truthAt(p) {
	for (let i = polys.length - 1; i >= 0; i--) {
		const P = polys[i], d = p[0] * P.c[0] + p[1] * P.c[1] + p[2] * P.c[2];
		if (d < P.cosR) continue;
		const x = (p[0] * P.U[0] + p[1] * P.U[1] + p[2] * P.U[2]) / d, y = (p[0] * P.W[0] + p[1] * P.W[1] + p[2] * P.W[2]) / d;
		let ins = false;
		for (let a = 0, b = P.px.length - 1; a < P.px.length; b = a++)
			if ((P.py[a] > y) !== (P.py[b] > y) && x < (P.px[b] - P.px[a]) * (y - P.py[a]) / (P.py[b] - P.py[a]) + P.px[a]) ins = !ins;
		if (ins) return P.tex;
	}
	return 13;
}

// ---- арифметика движка (globe.c tables, globe_s.s gl_ktab/gl_ztab/gl_project, eslope)
const SINQ = []; for (let i = 0; i <= 1024; i++) SINQ.push(Math.round(Math.sin(i / 1024 * Math.PI / 2) * 16384));
const sin16 = a => { a &= 0xFFFF; const i = (a >> 4) & 1023; const v = (a & 0x4000) ? SINQ[1024 - i] : SINQ[i]; return (a & 0x8000) ? -v : v; };
const cos16 = a => sin16(a + 0x4000);
const RECIP = [0, 0]; for (let d = 2; d < 512; d++) RECIP.push(Math.round(65536 / d));
const shr14 = v => Math.floor(v * 4 / 65536);
const hiw = acc => { let h = Math.floor(acc / 65536); h = ((h % 65536) + 65536) % 65536; return h >= 32768 ? h - 65536 : h; };
function ktHi(K) {                                  // T_hi: шаг K >> 3, значение — старшее слово
	const st = Math.floor(K / 8), T = new Int32Array(256);
	let acc = 0x8000; for (let j = 0; j < 128; j++) { T[j] = hiw(acc); acc += st; }
	acc = 0x8000; for (let j = 1; j <= 128; j++) { acc -= st; T[256 - j] = hiw(acc); }
	return T;
}
function ktLo(K) {                                  // T_lo: шаг K >> 10, i = 0..127
	const st = Math.floor(K / 1024), T = new Int32Array(128);
	let acc = 0x8000; for (let i = 0; i < 128; i++) { T[i] = hiw(acc); acc += st; }
	return T;
}
function ztab(k) {                                  // z: шаг 2k, младший байт значения (со знаком)
	const T = ktHi(2 * k * 8);                      // ktHi делит на 8
	return T.map(v => ((v & 255) >= 128 ? (v & 255) - 256 : (v & 255)));
}
const s16 = v => { v = ((v % 65536) + 65536) % 65536; return v >= 32768 ? v - 65536 : v; };
const s8 = v => { v = ((v % 256) + 256) % 256; return v >= 128 ? v - 256 : v; };
function slopeE(dx, dy) {                           // eslope / быстрый путь e_fast
	const neg = dx < 0; let ax = Math.abs(dx), d = dy, B = 1;
	while (d >= 512) { d >>= 1; B++; }
	const r = RECIP[d] || 0;
	let s;
	if (!r) s = ax < 256 ? ax * 128 : 32767;
	else if (ax < 256) s = (ax * (r >> 8) + ((ax * (r & 255)) >> 8) + 1) >> B;
	else { let p = ax * r + 256; p = Math.floor(p / 256); p >>= B; s = p > 32767 ? 32767 : p; }
	return neg ? -s : s;
}
const lerpT = (d, a, den) => { const q = Math.floor(Math.abs(d) * a / den); return d < 0 ? -q : q; };

function render(lonD, latD, zoom, wantTruth) {
	const lon0 = lonD * Math.PI / 180, lat0 = latD * Math.PI / 180, R = ZR[zoom];
	const cl = Math.cos(lon0), sl = Math.sin(lon0), cc = Math.cos(lat0), sc = Math.sin(lat0);
	const proj = ([X, Y, Z]) => { const W = X * cl + Y * sl; return { x: Math.floor(512 + 4 * R * (Y * cl - X * sl)), y: Math.floor(400 + 4 * R * (cc * Z - sc * W)), z: cc * W + sc * Z }; };
	const lon16 = Math.round(lonD * 65536 / 360) & 0xFFFF, lat16 = Math.round(latD * 65536 / 360);
	let projE = null;
	if (EXACT) {
		const cl = cos16(lon16), sl = sin16(lon16), cc = cos16(lat16), sc = sin16(lat16);
		const K = [R * -sl, R * cl, R * -shr14(sc * cl), R * -shr14(sc * sl), R * cc];
		const TH = K.map(ktHi), TL = K.map(ktLo);
		const TZ = [ztab(shr14(cc * cl)), ztab(shr14(cc * sl)), ztab(sc)];
		const KZ = [shr14(cc * cl) * 1024, shr14(cc * sl) * 1024, sc * 1024];
		const TZH = KZ.map(ktHi), TZL = KZ.map(ktLo);
		projE = (o, noz) => {                       // вершина — 6 байт по смещению o в G
			const lo = [G[o], G[o + 2], G[o + 4]], hi = [G[o + 1], G[o + 3], G[o + 5]];
			const x = s16(512 + TH[0][hi[0]] + TL[0][lo[0]] + TH[1][hi[1]] + TL[1][lo[1]]);
			const y = s16(400 + TH[2][hi[0]] + TL[2][lo[0]] + TH[3][hi[1]] + TL[3][lo[1]] + TH[4][hi[2]] + TL[4][lo[2]]);
			const z = Z16 ? (noz ? 4096 : s16(TZH[0][hi[0]] + TZL[0][lo[0]] + TZH[1][hi[1]] + TZL[1][lo[1]] + TZH[2][hi[2]] + TZL[2][lo[2]]))
				: (noz ? 64 : s8(TZ[0][hi[0]] + TZ[1][hi[1]] + TZ[2][hi[2]]));
			return { x, y, z };
		};
	}
	// диск по строкам (пары), как rows_init
	const PL = new Int16Array(200), PR = new Int16Array(200);
	for (let y = 0; y < 200; y++) {
		const v = 2 * y + 1 - 200, d = 4 * R * R - v * v;
		PL[y] = 1; PR[y] = 0;
		if (d <= 0) continue;
		const s = Math.floor(Math.sqrt(d - 1e-9));
		const a = Math.max(0, (256 - s) >> 1), b = Math.min(255, (255 + s) >> 1);
		if (a > b) continue;
		PL[y] = a >> 1; PR[y] = b >> 1;
	}
	const border = zoom < 2;
	// ---- ячейки
	const buckets = Array.from({ length: 200 }, () => []);
	const EVA = new Int16Array(200).fill(-1), EVB = new Int16Array(200).fill(-1), EVY = [], EVYa = [];
	let nE = 0, nCells = 0, nV = 0;
	for (let c = 0; c < nCell; c++) {
		const o = ct + c * 16;
		const bo = bt + G.readUInt16LE(o), vn = G.readUInt16LE(o + 2), en = G.readUInt16LE(o + 4);
		if (!en) continue;
		let noz = false;
		if (EXACT) {
			const sri = G.readInt16LE(o + 14), q = projE(o + 8, false);
			if (sri < 16384) {
				if (Z16 ? q.z + (sri >> 2) < -64 : s8(q.z + (sri >> 8)) < -1) continue;
				let mm = shr14(R * sri); mm += (mm >> 3) + 2;
				const xc = q.x >> 2, yc = q.y >> 2;
				if (xc + mm < 0 || xc - mm >= 256 || yc + mm < 0 || yc - mm >= 200) continue;
			}
			if (vn > 150 || en > 150) continue;
			noz = sri < 16384 && (Z16 ? q.z - (sri >> 2) > 64 : s8(q.z - (sri >> 8)) > 1);
		} else {
			const sr = G.readInt16LE(o + 14) / 16384, q = proj(rdv(o + 8));
			if (sr < 1) {
				if (q.z < -sr - 1 / 64) continue;
				let m = R * sr; m += m / 8 + 2;
				const xc = q.x / 4, yc = q.y / 4;
				if (xc + m < 0 || xc - m >= 256 || yc + m < 0 || yc - m >= 200) continue;
			}
		}
		nCells++;
		const P = []; for (let v = 0; v < vn; v++) P.push(EXACT ? projE(bo + v * 6, noz) : proj(rdv(bo + v * 6)));
		nV += vn;
		for (let e = 0; e < en; e++) {
			const eo = bo + vn * 6 + e * 6;
			let A = { ...P[G.readUInt16LE(eo) / 6], lb: 0 }, B = { ...P[G.readUInt16LE(eo + 2) / 6], lb: 0 };
			const texL = G[eo + 4], texR = G[eo + 5];
			if (A.z < 0 && B.z < 0) continue;
			if (A.z < 0 || B.z < 0) {                    // горизонт: линейно в 3D
				const [F, K] = A.z >= 0 ? [A, B] : [B, A];
				let Q;
				if (EXACT) Q = { x: F.x + lerpT(K.x - F.x, F.z, F.z - K.z), y: F.y + lerpT(K.y - F.y, F.z, F.z - K.z), z: 0, lb: 0 };
				else { const t = F.z / (F.z - K.z); Q = { x: Math.trunc(F.x + (K.x - F.x) * t), y: Math.trunc(F.y + (K.y - F.y) * t), z: 0, lb: 0 }; }
				Q.lb = zoom < 2 && Q.x < 512 ? 1 : 0;          // на левом краю диска (зумы 0–1)
				if (A.z < 0) A = Q; else B = Q;
			}
			// стороны на экране: вниз — слева texR; вверх — слева texL; горизонтальное вправо — снизу texR
			let tl, tr, hb = -1, ha = -1;
			if (A.y === B.y) { hb = B.x > A.x ? texR : texL; ha = B.x > A.x ? texL : texR; }
			if (B.y > A.y) { tl = texR; tr = texL; } else { tl = texL; tr = texR; const t = A; A = B; B = t; }
			// окно по x: [0, 1024] в Q2
			for (const lim of [0, 1024]) {
				const outA = lim === 0 ? A.x < 0 : A.x > 1024, outB = lim === 0 ? B.x < 0 : B.x > 1024;
				if (outA && outB) { A = null; break; }
				if (outA || outB) {
					const yc = A.y + Math.trunc((lim - A.x) * (B.y - A.y) / (B.x - A.x));
					const lb = lim === 0 ? 1 : 0;              // на левом краю окна
					if (outA) A = { x: lim, y: yc, lb }; else B = { x: lim, y: yc, lb };
				}
			}
			if (!A) continue;
			// пересечение левого края (окна или диска) — событие для строк без рёбер: ниже / выше
			// точки на краю текстура другой стороны ребра
			// (в одной точке края — вершина на x = 0: сначала ребро, у которого часть вне окна выше — отсечён верх, потом отсечён низ)
			// порядок событий строки: по y; LIMBKEY (зумы 0–1, край диска) — по ходу вдоль края вниз:
			// в верхней половине x убывает, в нижней растёт (точки в одной строке с разницей в 1 Q2)
			const ev = (yq, below, above, ord, xq) => { const r = Math.floor((yq + 1) / 4); if (r < 0 || r > 199) return;
				const kb = LIMBKEY && zoom < 2 ? ((yq < 400 ? 1024 - xq : xq + 1024 - 2 * (512 - 4 * R)) * 4 + (yq & 3)) * 4 + ord : yq * 4 + ord; if (EVY[r] === undefined || kb >= EVY[r]) { EVB[r] = below; EVY[r] = kb; } if (EVYa[r] === undefined || kb <= EVYa[r]) { EVA[r] = above; EVYa[r] = kb; } };
			if (hb >= 0) { if (A.lb || B.lb) ev(A.y, hb, ha, 1, A.lb ? A.x : B.x); continue; }
			if (A.lb) ev(A.y, tl, tr, 0, A.x);
			if (B.lb) ev(B.y, tr, tl, 2, B.x);
			if (B.y <= A.y) continue;
			const dx = B.x - A.x, dy = B.y - A.y;
			let r0 = Math.floor((A.y + 1) / 4), r1 = Math.floor((B.y + 1) / 4) - 1;
			if (r1 < r0) continue;
			let s, u;
			if (EXACT) {
				s = slopeE(dx, dy);
				const off = (2 - (A.y & 3)) & 3;
				const t = off === 0 ? 0 : off === 1 ? s >> 2 : off === 2 ? s >> 1 : (s >> 1) + (s >> 2);
				u = (32 * A.x + 192 + t) & 0xFFFF;
				if (r0 < 0) { u = (u + (-r0) * s) & 0xFFFF; r0 = 0; }
			} else {
				s = Math.round(128 * dx / dy);        // пары / строку, 8.8
				u = 32 * A.x + 192 + Math.trunc(((4 * r0 + 2 - A.y) * s) / 4);
				if (r0 < 0) { u += -r0 * s; r0 = 0; }
			}
			if (r1 > 199) r1 = 199;
			if (r1 < r0) continue;
			buckets[r0].push({ u, s, last: r1, tl, tr, top: r0, lt: A.lb, lbt: B.lb, id: nE });
			nE++;
		}
	}
	// BANDGRID: строка без рёбер — одна область: для полосы таких строк текстура по точке
	// посередине (обратная проекция) из сетки 5°, если клетка однородна (иначе — по событиям края)
	const BT = new Int16Array(200).fill(-1);
	if (BANDGRID) {
		const cov = new Int16Array(201);
		for (let r = 0; r < 200; r++) for (const e of buckets[r]) { cov[r]++; cov[e.last + 1]--; }
		for (let r = 1; r < 200; r++) cov[r] += cov[r - 1];
		const ex = [-sl, cl, 0], ey = [-sc * cl, -sc * sl, cc], ez = [cc * cl, cc * sl, sc];
		const sample = (y, p) => {
			const px = (2 * p + 0.5 - 128) / R, py = (y + 0.5 - 100) / R, rr = px * px + py * py;
			if (rr >= 1) return -1;
			const z = Math.sqrt(1 - rr), q = [0, 1, 2].map(k => px * ex[k] + py * ey[k] + z * ez[k]);
			const lon = ((Math.atan2(q[1], q[0]) * 180 / Math.PI) + 360) % 360, lat = Math.asin(Math.max(-1, Math.min(1, q[2]))) * 180 / Math.PI;
			const g = G[gt + Math.min(35, Math.floor((lat + 90) / 5)) * 72 + Math.floor(lon / 5) % 72];
			return g === 0xFE ? -1 : g;
		};
		for (let r = 0; r < 200; ) {
			if (cov[r] || PL[r] > PR[r]) { r++; continue; }
			let r1 = r; while (r1 + 1 < 200 && !cov[r1 + 1] && PL[r1 + 1] <= PR[r1 + 1]) r1++;
			const ym = (r + r1) >> 1, a = PL[ym], b = PR[ym];
			let t = -1;
			for (const p of [(a + b) >> 1, a + ((b - a) >> 2), b - ((b - a) >> 2)]) { t = sample(ym, p); if (t >= 0) break; }
			if (t >= 0) for (let y = r; y <= r1; y++) BT[y] = t;
			bandStat.n++; if (t >= 0) bandStat.ok++;
			r = r1 + 1;
		}
	}
	// ---- строки
	const img = new Int16Array(256 * 200).fill(-1);
	let ael = [], ltex = -1, pend = -1, runs = 0, badIns = 0, dirtyAll = false; nRep = 0;
	const put = (y, a, b, t) => { if (a > b) return; runs++; for (let p = a; p <= b; p++) img[y * 256 + 2 * p] = img[y * 256 + 2 * p + 1] = t; };
	const fillRow = (y, t) => put(y, PL[y], PR[y], t);
	for (let y = 0; y < 200; y++) {
		const bk = buckets[y].sort((a, b) => a.u - b.u || a.s - b.s);
		const insNew = [];
		for (const e of bk) {
			let k = 0; while (k < ael.length && (ael[k].u < e.u || (ael[k].u === e.u && ael[k].s <= e.s))) k++;
			// место согласовано: слева текстура совпадает (tr левого = tl нового); у края (слева
			// нет ребра — край диска или окна, текстура там меняется) — справа (tr нового = tl правого)
			// или новое ребро открывает область внутри (tl нового = tl правого)
			const ok = j => j > 0 ? ael[j - 1].tr === e.tl : (!ael.length || ael[0].tl === e.tl || ael[0].tl === e.tr);
			if ((MODE === 'topo' || INSFIX) && !ok(k)) {
				let f = -1;
				for (const d of [-1, 1, -2, 2, -3, 3]) {
					const j = k + d;
					if (j < 0 || j > ael.length) continue;
					const nb = ael[Math.min(j, ael.length - 1)];
					if (Math.abs(nb.u - e.u) > 512) continue;
					if (ok(j)) { f = j; break; }
				}
				if (f >= 0) k = f; else badIns++;
			}
			if (INSFIX2 && !INS2P && (!INSLIMB || zoom < 2)) {          // место в кучке (соседи ближе пары) по текстурам
				const lc = EVB[y] >= 0 ? EVB[y] : ltex;
				const okL = j => j > 0 ? ael[j - 1].tr === e.tl : (NOLC || lc < 0 || lc === e.tl);
				const okR = j => j >= ael.length || ael[j].tl === e.tr;
				if (!(okL(k) && okR(k))) {
					let lo = k, hi = k;
					while (lo > 0 && Math.abs(ael[lo - 1].u - e.u) < ITH) lo--;
					while (hi < ael.length && Math.abs(ael[hi].u - e.u) < ITH) hi++;
					let best = -1, bs = 0;
					for (let j = lo; j <= hi; j++) {
						const sc = (okL(j) ? 2 : 0) + (okR(j) ? 1 : 0);
						if (sc > bs || (sc === bs && best >= 0 && Math.abs(j - k) < Math.abs(best - k))) { bs = sc; best = j; }
					}
					const cur = (okL(k) ? 2 : 0) + (okR(k) ? 1 : 0);
					if (best >= 0 && bs > cur) k = best;
				}
			}
			if (process.env.DBGROWS) console.log("  ins row", y, "u", e.u, "s", e.s, "(" + e.tl + "|" + e.tr + ") at", k, "of", ael.length, ael.slice(Math.max(0, k - 2), k + 2).map(q => q.u + "/" + q.s + "(" + q.tl + "|" + q.tr + ")").join(" "));
			ael.splice(k, 0, e);
			insNew.push(e);
		}
		if (IMM) for (const e of insNew) {
			const k = ael.indexOf(e);
			if (IMM) { const lc = EVB[y] >= 0 ? EVB[y] : ltex; for (let d = -2; d <= 1; d++) { const kk = k + d; if (kk < 0 || kk + 1 >= ael.length) continue; const p = ael[kk], q = ael[kk + 1]; if (Math.abs(p.u - q.u) >= 256) continue; const L = kk > 0 ? ael[kk - 1].tr : lc, R = kk + 2 < ael.length ? ael[kk + 2].tl : -1; const eq = (x, y) => x < 0 || y < 0 || x === y; const cur = eq(L, p.tl) && p.tr === q.tl && eq(q.tr, R), sw = eq(L, q.tl) && q.tr === p.tl && eq(p.tr, R); if (!cur && sw) { ael[kk] = q; ael[kk + 1] = p; nRep++; } } }
		}
		if (process.env.DBGROWS && ael.length) { const bad = []; for (let k = 0; k + 1 < ael.length; k++) if (ael[k].tr !== ael[k + 1].tl) bad.push(k); if (bad.length) console.log("row", y, ael.map((e, k) => (bad.includes(k) ? "*" : "") + (e.u >> 8) + "(" + e.tl + "|" + e.tr + ")").join(" ")); }
		// починка порядка: соседние рёбра ближе пары меняются местами, если так согласуются
		// текстуры с левым соседом (у первого — текстура края прошлой строки или событие), друг с
		// другом и с правым соседом, а сейчас нет (кучки почти совпавших рёбер после округления)
		if (REPAIR) {
			const lc = EVB[y] >= 0 ? EVB[y] : ltex;
			const fixPair = k => {
				if (k < 0 || k + 1 >= ael.length) return;
				const p = ael[k], q = ael[k + 1];
				if (Math.abs(p.u - q.u) >= 256) return;
				const L = k > 0 ? ael[k - 1].tr : lc, R = k + 2 < ael.length ? ael[k + 2].tl : -1;
				const eq = (x, y) => x < 0 || y < 0 || x === y;
				const cur = eq(L, p.tl) && p.tr === q.tl && eq(q.tr, R), sw = eq(L, q.tl) && q.tr === p.tl && eq(p.tr, R);
				if (!cur && sw) { ael[k] = q; ael[k + 1] = p; nRep++; }
			};
			// CHAIN — кучка соседей ближе пары с несогласованностью внутри переставляется цепочкой:
			// от текстуры левее кучки — ребро с такой «слева» (левейшее), дальше — от его «справа»
			const fixChain = () => {
				for (let k0 = 0; k0 + 1 < ael.length; ) {
					let k1 = k0;
					while (k1 + 1 < ael.length && Math.abs(ael[k1 + 1].u - ael[k1].u) < 256) k1++;
					if (k1 > k0) {
						let bad = false;
						const L0 = k0 > 0 ? ael[k0 - 1].tr : lc;
						if (L0 >= 0 && ael[k0].tl !== L0) bad = true;
						for (let k = k0; k < k1; k++) if (ael[k].tr !== ael[k + 1].tl) bad = true;
						if (bad) {
							let cur = L0;
							if (cur < 0) {                          // левее — «слева», которой нет среди «справа»
								const trs = ael.slice(k0, k1 + 1).map(e => e.tr);
								const c = ael.slice(k0, k1 + 1).find(e => !trs.includes(e.tl));
								cur = c ? c.tl : ael[k0].tl;
							}
							for (let i = k0; i <= k1; i++) {
								let j = i; while (j <= k1 && ael[j].tl !== cur) j++;
								if (j > k1) break;
								if (j !== i) { const e = ael[j]; ael.splice(j, 1); ael.splice(i, 0, e); nRep++; }
								cur = ael[i].tr;
							}
						}
					}
					k0 = k1 + 1;
				}
			};
			// LDEFER: левая текстура неизвестна (первая строка с рёбрами после ждущих) — первую
			// пару последней: у неё нет левого контекста
			const fixAll = () => { const k0 = LDEFER && lc < 0 ? 1 : 0; for (let k = k0; k + 1 < ael.length; k++) fixPair(k); if (k0) fixPair(0); };
			if ((!LOCAL && !GATE) || dirtyAll || EVB[y] >= 0 || (GATE && insNew.length)) { if (CHAIN) fixChain(); else fixAll(); }
			else if (!IMM) for (const e of insNew) { const k = ael.indexOf(e); for (let d = -2; d <= 1; d++) fixPair(k + d); }
			dirtyAll = false;
			// INS2P: INSFIX2 после вставки всех новых рёбер строки и починки пар — новое ребро,
			// всё ещё несогласованное с соседями, — на лучшее место в своей кучке, потом починка снова
			if (INSFIX2 && INS2P && (!INSLIMB || zoom < 2)) {
				let moved = false;
				for (const e of [...insNew].sort((a, b) => b.id - a.id)) {         // порядок корзины движка (новые записи — в начало списка)
					let k = ael.indexOf(e);
					const okL = j => j > 0 ? ael[j - 1].tr === e.tl : (NOLC || lc < 0 || lc === e.tl);
					const okR = j => j >= ael.length || ael[j].tl === e.tr;
					ael.splice(k, 1);
					if (!(okL(k) && okR(k))) {
						let lo = k, hi = k;
						while (lo > 0 && Math.abs(ael[lo - 1].u - e.u) < ITH) lo--;
						while (hi < ael.length && Math.abs(ael[hi].u - e.u) < ITH) hi++;
						let best = -1, bs = 0;
						for (let j = lo; j <= hi; j++) {
							const sc = (okL(j) ? 2 : 0) + (okR(j) ? 1 : 0);
							if (sc > bs || (sc === bs && best >= 0 && Math.abs(j - k) < Math.abs(best - k))) { bs = sc; best = j; }
						}
						const cur = (okL(k) ? 2 : 0) + (okR(k) ? 1 : 0);
						if (best >= 0 && bs > cur) { k = best; moved = true; }
					}
					ael.splice(k, 0, e);
				}
				if (moved) fixAll();
			}
		}
		if (PL[y] <= PR[y]) {
			if (!ael.length) {
				if (EVB[y] >= 0) ltex = EVB[y];
				if (BT[y] >= 0) {                          // текстура полосы по сетке; ждущие выше — ею же
					ltex = BT[y];
					if (pend >= 0) { for (let r = pend; r < y; r++) fillRow(r, ltex); pend = -1; }
				}
				if (ltex < 0) { if (pend < 0) pend = y; }
				else fillRow(y, ltex);
			} else {
				const t0 = ael[0].tl;             // текстура у самого края (левее всех рёбер)
				if (pend >= 0) {
					// над первой строкой с рёбрами: рёбра, начинающиеся на левом краю, — выше края
					// текстура справа от них
					let t = t0;
					for (let r = y - 1; r >= pend; r--) { if (EVA[r + 1] >= 0) t = EVA[r + 1]; fillRow(r, t); }
					pend = -1;
				}
				// под строкой: рёбра, кончающиеся на левом краю, — ниже края текстура справа от них
				ltex = t0;
				let cur = PL[y], tex = t0;
				const pr = PR[y], n = ael.length;
				for (let k = 0; k < n; k++) {
					const e = ael[k], b = e.u >> 8;
					// текстура за ребром: tr, но если сосед справа с ним не согласен (порядок сбит
					// в кучке почти совпавших рёбер) — tl соседа, когда тот согласен со своим соседом
					let t = e.tr;
					if (MODE !== 'topo' && LOOK && k + 1 < n && ael[k + 1].tl !== t && (k + 2 >= n || ael[k + 1].tr === ael[k + 2].tl)) t = ael[k + 1].tl;
					if (b <= cur) { tex = t; continue; }
					if (b > pr) break;
					if (t !== tex) { put(y, cur, b - 1, tex); cur = b; tex = t; }
				}
				put(y, cur, pr, tex);
			}
		}
		for (const e of ael) e.u = EXACT ? (e.u + e.s) & 0xFFFF : e.u + e.s;
		if (DELGATE) {                                    // после удаления рёбер новые соседи несогласованы — починка в следующей строке
			let lt = ltex, del = false;
			for (const e of ael) {
				if (e.last === y) { del = true; continue; }
				if (del && lt >= 0 && lt !== e.tl) dirtyAll = true;
				del = false; lt = e.tr;
			}
		}
		ael = ael.filter(e => e.last !== y);
		if (MODE !== 'topo')                              // сортировка вставками по x (устойчивая)
			for (let i = 1; i < ael.length; i++) { const e = ael[i]; let j = i; while (j > 0 && (HITOL ? (ael[j - 1].u >> 8) > (e.u >> 8) + 1 : ael[j - 1].u > e.u + TOL)) { ael[j] = ael[j - 1]; j--; dirtyAll = true; } ael[j] = e; }
	}
	let gridTex = -1;
	if (pend >= 0) {                                   // рёбер в окне нет: сетка 5° в центре вида
		const gx = EXACT ? (lon16 * 72) >> 16 : Math.floor((((lonD % 360) + 360) % 360) / 5) % 72, gy = EXACT ? Math.min(35, ((lat16 + 16384) * 36) >> 15) : Math.min(35, Math.max(0, Math.floor((latD + 90) / 5)));
		gridTex = G[gt + gy * 72 + gx];
		for (let r = pend; r < 200; r++) if (PL[r] <= PR[r]) fillRow(r, gridTex === 0xFE ? 13 : gridTex);
	}
	// ---- эталон (по левому пикселю пары)
	let diff = 0, total = 0, truth = null, gross = 0;
	if (wantTruth) {
		truth = new Int16Array(256 * 200).fill(-1);
		for (let y = 0; y < 200; y++) for (let p = PL[y]; p <= PR[y]; p++) {
			const px = (2 * p + 0.5 - 128) / R, py = (y + 0.5 - 100) / R, rr = px * px + py * py;
			if (rr > 1) continue;
			const z = Math.sqrt(1 - rr);
			// обратный поворот: экранные оси e_x, e_y, e_z
			const ex = [-sl, cl, 0], ey = [-sc * cl, -sc * sl, cc], ez = [cc * cl, cc * sl, sc];
			const q = [0, 1, 2].map(k => px * ex[k] + py * ey[k] + z * ez[k]);
			const t = truthAt(q);
			truth[y * 256 + 2 * p] = truth[y * 256 + 2 * p + 1] = t;
			total++;
			if (img[y * 256 + 2 * p] !== t) diff++;
		}
		// «грубые» расхождения: вокруг пары (5x5 пар) эталон однороден — не шум на границе
		for (let y = 2; y < 198; y++) for (let p = 2; p < 126; p++) {
			const i = y * 256 + 2 * p, t = truth[i];
			if (t < 0 || img[i] === t) continue;
			let uni = true;
			for (let dy = -2; dy <= 2 && uni; dy++) for (let dp = -2; dp <= 2; dp++) if (truth[i + dy * 256 + dp * 2] !== t) { uni = false; break; }
			if (uni) gross++;
		}
	}
	return { img, truth, diff, gross, total, nE, nCells, nV, runs, badIns, gridTex };
}

function png(img, zoom, out) {
	const W2 = 512, H2 = 400, raw = Buffer.alloc((W2 * 3 + 1) * H2);
	const set = 2 - (zoom >> 1);
	for (let y = 0; y < H2; y++) for (let x = 0; x < W2; x++) {
		const t = img[(y >> 1) * 256 + (x >> 1)], X = x >> 1, Y = y >> 1;
		const c = t < 0 ? -1 : (t === 255 || t === 13) ? OCEAN : t === 254 ? 1 : TEX[(set * 13 + t) * 1024 + (Y & 31) * 32 + (X & 31)];
		const p = c < 0 ? [0, 0, 0] : c === 1 ? [255, 0, 255] : pal[c], o = y * (W2 * 3 + 1) + 1 + x * 3;
		raw[o] = p[0]; raw[o + 1] = p[1]; raw[o + 2] = p[2];
	}
	const CRC = (() => { const t = new Uint32Array(256); for (let n = 0; n < 256; n++) { let c = n; for (let k = 0; k < 8; k++) c = c & 1 ? 0xEDB88320 ^ (c >>> 1) : c >>> 1; t[n] = c >>> 0; } return t; })();
	const crc32 = b => { let c = 0xFFFFFFFF; for (let i = 0; i < b.length; i++) c = CRC[(c ^ b[i]) & 255] ^ (c >>> 8); return (c ^ 0xFFFFFFFF) >>> 0; };
	const chunk = (t, d) => { const l = Buffer.alloc(4); l.writeUInt32BE(d.length); const td = Buffer.concat([Buffer.from(t), d]); const c = Buffer.alloc(4); c.writeUInt32BE(crc32(td)); return Buffer.concat([l, td, c]); };
	const ih = Buffer.alloc(13); ih.writeUInt32BE(W2, 0); ih.writeUInt32BE(H2, 4); ih[8] = 8; ih[9] = 2;
	fs.writeFileSync(out, Buffer.concat([Buffer.from([137, 80, 78, 71, 13, 10, 26, 10]), chunk('IHDR', ih), chunk('IDAT', zlib.deflateSync(raw)), chunk('IEND', Buffer.alloc(0))]));
}

module.exports = { render, TEX, OCEAN, pal, bandStat };
if (require.main !== module) {} else if (process.env.SWEEP) {
	let worst = [];
	for (let zoom = 0; zoom < 6; zoom++) {
		let sd = 0, st = 0, bad = 0, sg = 0;
		for (let lon = 0; lon < 360; lon += 37) for (let lat = -80; lat <= 80; lat += 32) {
			const r = render(lon, lat, zoom, true);
			sd += r.diff; st += r.total; bad += r.badIns; sg += r.gross;
			worst.push({ lon, lat, zoom, diff: r.gross, pct: r.gross / Math.max(1, r.total) });
		}
		console.log(`zoom ${zoom}: mismatch ${(100 * sd / st).toFixed(3)}% (${sd} of ${st} pairs), gross ${sg}, bad inserts ${bad}`);
	}
	worst.sort((a, b) => b.pct - a.pct);
	console.log('worst:', worst.slice(0, 6).map(w => `${w.lon},${w.lat},z${w.zoom}: ${w.diff} (${(100 * w.pct).toFixed(2)}%)`).join('; '));
} else {
	const a = process.argv.slice(2).map(Number);
	const lon = a[0] ?? -5, lat = a[1] ?? 6.4, zoom = a[2] ?? 0;
	const r = render(lon, lat, zoom, true);
	png(r.img, zoom, `tmp/m4c_${game}_z${zoom}.png`);
	png(r.truth, zoom, `tmp/m4c_${game}_z${zoom}_truth.png`);
	const d = new Int16Array(r.img.length).fill(-1);
	for (let i = 0; i < d.length; i++) if (r.truth[i] >= 0) d[i] = r.img[i] === r.truth[i] ? r.truth[i] : 254;
	png(d, zoom, `tmp/m4c_${game}_z${zoom}_diff.png`);
	console.log(`cells ${r.nCells}, verts ${r.nV}, edges ${r.nE}, runs ${r.runs}, bad inserts ${r.badIns}, repairs ${nRep}, grid ${r.gridTex}; mismatch ${r.diff} (gross ${r.gross}) of ${r.total} pairs (${(100 * r.diff / r.total).toFixed(3)}%)`);
}
