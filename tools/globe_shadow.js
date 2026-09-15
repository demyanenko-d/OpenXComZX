// Модель тени глобуса (день/ночь) и сверка с OpenXcom (Globe::drawShadow, Globe.cpp:176–245,
// 936–1008; шум выключен). Уровень суши k = ⌊тень/3⌋ ∈ 0..10: по строке — отрезки уровней
// (граница уровня — эллипс x = xc(y) ± w(y), xc линейна по y, w² — квадратичный многочлен),
// сравнение по левому пикселю пары. node tools\globe_shadow.js [lon lat zoom daylight]
// (daylight — доля суток GameTime::getDaylight: ((час + 18) % 24 + мин/60) / 24).
'use strict';
const ZR = [90, 120, 180, 280, 450, 720];

// shade_gradient (Globe.cpp:118–159)
const grad = new Int16Array(240);
for (let i = 0; i < 240; i++) {
	let j = i - 120;
	if (j < -66) j = -16; else if (j < -48) j = -15; else if (j < -33) j = -14; else if (j < -22) j = -13;
	else if (j < -15) j = -12; else if (j < -11) j = -11; else if (j < -9) j = -10;
	if (j > 120) j = 19; else if (j > 98) j = 18; else if (j > 86) j = 17; else if (j > 74) j = 16;
	else if (j > 54) j = 15; else if (j > 38) j = 14; else if (j > 26) j = 13; else if (j > 18) j = 12;
	else if (j > 13) j = 11; else if (j > 10) j = 10; else if (j > 8) j = 9;
	grad[i] = j + 16;
}
function shadeOf(d) {                                // d = e·s -> тень 0..31 (без шума)
	const t = -250 * d;
	let v;
	if (t < -110) v = -31; else if (t > 120) v = 50; else v = grad[Math.trunc(t) + 120];
	return Math.max(0, Math.min(31, v));
}
// пороги уровней: наименьшее целое T с ⌊тень/3⌋ >= k; (Sint16) усекает к нулю, поэтому для
// T <= 0 уровень начинается уже с t > T - 1: граница по t — TB_k, d <= D_k = -TB_k / 250
const TK = [], TB = [];
for (let k = 1; k <= 10; k++) {
	let t = -121;
	while (t <= 121 && Math.floor(shadeOf(-t / 250) / 3) < k) t++;
	TK.push(t);
	TB.push(t <= 0 ? t - 1 : t);
}

function sunDir(lonD, latD, daylight) {              // Globe::getSunDirection без сезонов
	const rot = daylight * 2 * Math.PI, lon = lonD * Math.PI / 180, lat = latD * Math.PI / 180;
	const s = [Math.cos(rot + lon), Math.sin(rot + lon) * -Math.sin(lat), Math.sin(rot + lon) * Math.cos(lat)];
	const n = Math.hypot(...s);
	return s.map(v => v / n);
}

function run(lonD, latD, zoom, daylight) {
	const R = ZR[zoom], s = sunDir(lonD, latD, daylight);
	// эталон: уровень по пикселю (центр пикселя, circle_norm)
	let diff = 0, tot = 0, bands = 0, maxSeg = 0;
	const lv = new Int8Array(256 * 200).fill(-1), lvO = new Int8Array(256 * 200).fill(-1);
	for (let y = 0; y < 200; y++) for (let x = 0; x < 256; x++) {
		const ex = (x + 0.5 - 128) / R, ey = (y + 0.5 - 100) / R, q = ex * ex + ey * ey;
		if (q >= 1) continue;
		lvO[y * 256 + x] = Math.floor(shadeOf(ex * s[0] + ey * s[1] + Math.sqrt(1 - q) * s[2]) / 3);
	}
	// модель: уровни по строке из корней границ (плавающая точка — проверка схемы)
	const A2 = s[0] * s[0] + s[2] * s[2];
	for (let y = 0; y < 200; y++) {
		const ey = (y + 0.5 - 100) / R, rho2 = 1 - ey * ey;
		if (rho2 <= 0) continue;
		const rho = Math.sqrt(rho2);
		// для каждого k: переключения «ночь уровня k» вдоль хорды [-rho, rho]
		const segs = [];                             // [x0, x1, k]: на отрезке уровень >= k
		for (let k = 1; k <= 10; k++) {
			const D = -TB[k - 1] / 250, c = D - ey * s[1];
			const nightAt = ex => ex * s[0] + Math.sqrt(Math.max(0, rho2 - ex * ex)) * s[2] <= c;
			const roots = [];
			const rad = A2 * rho2 - c * c;
			if (rad >= 0 && A2 > 1e-12) {
				const w = Math.abs(s[2]) * Math.sqrt(rad) / A2, xc = s[0] * c / A2;
				for (const ex of [xc - w, xc + w]) {
					if (ex < -rho || ex > rho) continue;
					const ez = Math.sqrt(Math.max(0, rho2 - ex * ex));
					if (Math.abs(ex * s[0] + ez * s[2] - c) < 1e-9) roots.push(ex);   // корень на видимой стороне
				}
			}
			roots.sort((a, b) => a - b);
			let st = -s[0] * rho <= c, x0 = -rho;         // у левого края (ez = 0): -rho·sx <= c
			for (const r of roots) { if (st) segs.push([x0, r, k]); st = !st; x0 = r; }
			if (st) segs.push([x0, rho, k]);
		}
		maxSeg = Math.max(maxSeg, segs.length);
		if (segs.length) bands++;
		for (let x = 0; x < 256; x += 2) {
			const i = y * 256 + x;
			if (lvO[i] < 0) continue;
			const ex = (x + 0.5 - 128) / R;
			let k = 0;
			for (const [a, b, kk] of segs) if (ex >= a && ex <= b && kk > k) k = kk;
			lv[i] = k;
			tot++;
			if (k !== lvO[i]) diff++;
		}
	}
	return { diff, tot, bands, maxSeg, s };
}

// ---- картинка: дневной кадр — эталон globe_model (текстура на пиксель); тень OpenXcom
// (шум rand() % 4 в клетке 60x60) против схемы движка: уровень пары k (отрезки по строке),
// строки-образцы уровня с шумом (256 пикселей, сдвиг по строке): суша — BLT2 по полубайтам
// +L_k(x) = ⌊(v_k − n)/3⌋, океан — копия O_k(x) = OCEAN + (v_k − n), v_k = 3k + 1 (k = 10 — 31)
function picture(lonD, latD, zoom, daylight, out) {
	const M = require('./globe_model.js'), fs = require('fs'), zlib = require('zlib');
	const R = ZR[zoom], s = sunDir(lonD, latD, daylight), set = 2 - (zoom >> 1);
	const r = M.render(lonD, latD, zoom, true);
	const day = (x, y) => { const t = r.truth[y * 256 + x]; return t < 0 ? -1 : (t === 13 || t === 255) ? M.OCEAN : M.TEX[(set * 13 + t) * 1024 + (y & 31) * 32 + (x & 31)]; };
	let seed = 12345; const rnd = () => (seed = (seed * 1103515245 + 12345) & 0x7fffffff) >>> 16;
	const noise60 = Array.from({ length: 3600 }, () => rnd() % 4);
	const pat = Array.from({ length: 256 }, () => rnd() % 4), shift = Array.from({ length: 200 }, () => rnd() & 0xFE);
	const pat2 = Array.from({ length: 256 }, () => rnd() & 1);
	const isOcean = c => c >= M.OCEAN && c < M.OCEAN + 32;
	const land = (c, sh) => { if (!sh) return c; const d = c & 0xF0, e = c + Math.floor(sh / 3); return e > d + 15 ? d + 15 : e; };
	const truth = new Int16Array(256 * 200).fill(-1), ours = new Int16Array(256 * 200).fill(-1);
	let sum = 0, n = 0, big = 0;
	for (let y = 0; y < 200; y++) for (let x = 0; x < 256; x++) {
		const c = day(x, y);
		if (c < 0) continue;
		const ex = (x + 0.5 - 128) / R, ey = (y + 0.5 - 100) / R, q = ex * ex + ey * ey;
		if (q >= 1) continue;
		const dd = ex * s[0] + ey * s[1] + Math.sqrt(1 - q) * s[2];
		// эталон
		const sh = Math.max(0, Math.min(31, shadeOfRaw(dd) - noise60[(y % 60) * 60 + (x % 60)]));
		truth[y * 256 + x] = isOcean(c) ? M.OCEAN + sh : land(c, sh);
		// схема: уровень по левому пикселю пары
		const xp = x & ~1, exp = (xp + 0.5 - 128) / R, qp = exp * exp + ey * ey;
		let k = qp < 1 ? Math.floor(shadeOf(exp * s[0] + ey * s[1] + Math.sqrt(1 - qp) * s[2]) / 3) : Math.floor(shadeOf(dd) / 3);
		// BLOCK=WxH: уровень блока — по его центру (за краем диска — точка края), ступенчато;
		// OCEAN32 — ровная заливка (океан) берёт тень блока целиком (0..31), а не v_k уровня
		let s32 = -1;
		if (process.env.BLOCK) {
			const [bw, bh] = process.env.BLOCK.split('x').map(Number);
			const cx = Math.floor(x / bw) * bw + bw / 2, cy = Math.floor(y / bh) * bh + bh / 2;
			const bx = (cx - 128) / R, by = (cy - 100) / R, bq = Math.min(1, bx * bx + by * by);
			const sb = shadeOf(bx * s[0] + by * s[1] + Math.sqrt(1 - bq) * s[2]);
			k = Math.floor(sb / 3);
			if (process.env.OCEAN32) s32 = sb;
		}
		// MERGE: полоса b = число границ 1,3,5,7,9 (уровни 2b−1, 2b); узор — растр 50/50 из двух
		if (process.env.MERGE) { const b = [1, 3, 5, 7, 9].filter(q => k >= q).length; k = b === 0 ? 0 : 2 * b - ((pat2[(x + shift[y] * 3) & 255]) ? 1 : 0); }
		const vk = k === 10 ? 31 : 3 * k + 1, nn = process.env.NONOISE ? 0 : pat[(x + shift[y]) & 255], sv = Math.max(0, vk - nn);
		ours[y * 256 + x] = isOcean(c) ? M.OCEAN + (s32 >= 0 ? Math.max(0, s32 - nn) : sv) : (() => { const d = c & 0xF0, e = c + Math.floor(sv / 3); return e > d + 15 ? d + 15 : e; })();
		// разница в шагах тени (для суши — в шагах полубайта, океан — в шагах 0..31)
		const dv = Math.abs(truth[y * 256 + x] - ours[y * 256 + x]);
		sum += dv; n++; if (dv > 3) big++;
	}
	if (out) {
		const W2 = 1024, H2 = 400, raw = Buffer.alloc((W2 * 3 + 1) * H2);
		for (let y = 0; y < H2; y++) for (let x = 0; x < W2; x++) {
			const img = x < 512 ? truth : ours, X = (x & 511) >> 1, Y = y >> 1, c = img[Y * 256 + X];
			const p = c < 0 ? [0, 0, 0] : M.pal[c], o = y * (W2 * 3 + 1) + 1 + x * 3;
			raw[o] = p[0]; raw[o + 1] = p[1]; raw[o + 2] = p[2];
		}
		const CRC = (() => { const t = new Uint32Array(256); for (let n = 0; n < 256; n++) { let c = n; for (let k = 0; k < 8; k++) c = c & 1 ? 0xEDB88320 ^ (c >>> 1) : c >>> 1; t[n] = c >>> 0; } return t; })();
		const crc32 = b => { let c = 0xFFFFFFFF; for (let i = 0; i < b.length; i++) c = CRC[(c ^ b[i]) & 255] ^ (c >>> 8); return (c ^ 0xFFFFFFFF) >>> 0; };
		const chunk = (t, d) => { const l = Buffer.alloc(4); l.writeUInt32BE(d.length); const td = Buffer.concat([Buffer.from(t), d]); const c = Buffer.alloc(4); c.writeUInt32BE(crc32(td)); return Buffer.concat([l, td, c]); };
		const ih = Buffer.alloc(13); ih.writeUInt32BE(W2, 0); ih.writeUInt32BE(H2, 4); ih[8] = 8; ih[9] = 2;
		fs.writeFileSync(out, Buffer.concat([Buffer.from([137, 80, 78, 71, 13, 10, 26, 10]), chunk('IHDR', ih), chunk('IDAT', zlib.deflateSync(raw)), chunk('IEND', Buffer.alloc(0))]));
	}
	return { mean: sum / n, big, n };
}
// ---- уровни по строкам из малых кругов: круг уровня k — {P : P·s = D_k} на сфере, в мировых
// координатах неподвижен при данном времени суток (солнце на экваторе, долгота λs = 90° − 360°·
// daylight); N вершин на круг проецируются как вершины карты, отрезки режутся горизонтом (z = 0),
// пересечения со строками (y = 4r + 2, Q2) — как у рёбер. Уровень пары: у левого края диска —
// точно (d края строки), дальше — переключения на пересечениях. Сверка с точным уровнем пары.
function chains(lonD, latD, zoom, daylight, N) {
	const R = ZR[zoom], lon0 = lonD * Math.PI / 180, lat0 = latD * Math.PI / 180;
	const cl = Math.cos(lon0), sl = Math.sin(lon0), cc = Math.cos(lat0), sc = Math.sin(lat0);
	const ls = Math.PI / 2 - daylight * 2 * Math.PI, sw = [Math.cos(ls), Math.sin(ls), 0];
	const U = [0, 0, 1], V = [Math.sin(ls), -Math.cos(ls), 0];
	const proj = ([X, Y, Z]) => { const W = X * cl + Y * sl; return { x: 512 + 4 * R * (Y * cl - X * sl), y: 400 + 4 * R * (cc * Z - sc * W), z: cc * W + sc * Z }; };
	const s = sunDir(lonD, latD, daylight);
	// crossings[k][r] — {x (Q2), n}: пересечение полилинии уровня k со строкой r; n — правее
	// пересечения ночь (по направлению обхода: ночь всегда с одной стороны от хода — U x V
	// с солнцем, проекция передней полусферы ориентацию не меняет)
	const cross = Array.from({ length: 10 }, () => Array.from({ length: 200 }, () => []));
	// ночь справа от хода (экран: y вниз) — для нисходящего отрезка ночь левее (x меньше)
	const cr3 = (a, b) => [a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]];
	const orient = (() => { const w = cr3(U, V); return w[0] * sw[0] + w[1] * sw[1] + w[2] * sw[2]; })();
	for (let k = 1; k <= 10; k++) {
		const D = -TB[k - 1] / 250, rk = Math.sqrt(Math.max(0, 1 - D * D));
		const P = [];
		for (let j = 0; j < N; j++) { const th = 2 * Math.PI * j / N, c = Math.cos(th), si = Math.sin(th); P.push(proj([0, 1, 2].map(i => D * sw[i] + rk * (c * U[i] + si * V[i])))); }
		// сторона ночи: на сфере (снаружи) ночь справа от хода, если (U x V)·s > 0; на экране
		// (y вниз) «справа» — (−Ty, Tx), зеркально правому на сфере (Ty, −Tx)
		const nightRight = orient < 0;
		for (let j = 0; j < N; j++) {
			let A = P[j], B = P[(j + 1) % N];
			if (A.z < 0 && B.z < 0) continue;
			if (A.z < 0 || B.z < 0) { const [F, K] = A.z >= 0 ? [A, B] : [B, A], t = F.z / (F.z - K.z), Q = { x: F.x + (K.x - F.x) * t, y: F.y + (K.y - F.y) * t, z: 0 }; if (A.z < 0) A = Q; else B = Q; }
			if (A.y === B.y) continue;
			const down = B.y > A.y, n = down ? !nightRight : nightRight;   // вниз: ночь справа от хода = левее
			if (!down) { const t = A; A = B; B = t; }
			const r0 = Math.max(0, Math.floor((A.y + 1) / 4)), r1 = Math.min(199, Math.floor((B.y + 1) / 4) - 1);
			for (let r = r0; r <= r1; r++) { const yq = 4 * r + 2; cross[k - 1][r].push({ x: A.x + (B.x - A.x) * (yq - A.y) / (B.y - A.y), n }); }
		}
	}
	let diff = 0, tot = 0, far = 0, nCross = 0;
	for (let y = 0; y < 200; y++) {
		const Y = y + 0.5 - 100, rho2 = R * R - Y * Y;
		if (rho2 <= 0) continue;
		const rho = Math.sqrt(rho2);
		for (let k = 0; k < 10; k++) nCross += cross[k][y].length;
		// точный уровень в первой паре строки (край диска или окна) — один на строку
		let p0 = 0; while (p0 < 128 && (2 * p0 + 0.5 - 128) ** 2 + Y * Y >= R * R) p0++;
		const X0 = 2 * p0 + 0.5 - 128, e0x = X0 / R, e0y = Y / R, lev0 = p0 < 128 ? Math.floor(shadeOf(e0x * s[0] + e0y * s[1] + Math.sqrt(Math.max(0, 1 - e0x * e0x - e0y * e0y)) * s[2]) / 3) : 0;
		let p1 = 127; while (p1 > 0 && (2 * p1 + 0.5 - 128) ** 2 + Y * Y >= R * R) p1--;
		const X1 = 2 * p1 + 0.5 - 128, e1x = X1 / R, lev1 = Math.floor(shadeOf(e1x * s[0] + e0y * s[1] + Math.sqrt(Math.max(0, 1 - e1x * e1x - e0y * e0y)) * s[2]) / 3);
		for (let p = 0; p < 128; p++) {
			const X = 2 * p + 0.5 - 128;
			if (X * X + Y * Y >= R * R) continue;
			const ex = X / R, ey = Y / R, kx = Math.floor(shadeOf(ex * s[0] + ey * s[1] + Math.sqrt(1 - ex * ex - ey * ey) * s[2]) / 3);
			// схема: для каждого уровня — ночь у левого края диска, переключения левее пары
			const xq = 4 * (X + 128) - 2 + 2;       // Q2 точки пары (x = 2p + 0.5 -> Q2 = 4x)
			let lev = 0;
			for (let k = 0; k < 10; k++) {
				const D = -TB[k] / 250;
				// до первого пересечения правее первой пары — точный уровень первой пары; дальше —
				// сторона ночи последнего пересечения левее пары
				// правее последнего пересечения левее последней пары — точный уровень последней пары
				let night = k < lev0, bx = 4 * (X0 + 128), last = -1e9;
				const xs = 4 * (2 * p + 0.5), xe = 4 * (X1 + 128);
				for (const c of cross[k][y]) if (c.x > 4 * (X0 + 128) && c.x <= xe && c.x > last) last = c.x;
				for (const c of cross[k][y]) if (c.x <= xs && c.x > bx) { bx = c.x; night = c.n; }
				if (bx >= last && last > -1e9) night = k < lev1;
				if (night) lev = k + 1;
			}
			tot++;
			if (lev !== kx) { diff++; if (Math.abs(lev - kx) > 1) far++; }
		}
	}
	return { diff, tot, far, nCross };
}

// ---- уровни по строкам таблицей пролётов (схема движка). Эллипс уровня k на экране —
// сдвиг R·D_k·(sx, sy) и масштаб a_k = √(1 − D_k²) ≈ 1 эллипса E0 (проекция большого круга
// ⟂ s). Таблица E0 на кадр: для смещения v (пиксели, целые) от центра —
// X = (−sx·sy·v ∓ |sz|·√(A2·R² − v²)) / A2, A2 = 1 − sy². Строка y, уровень k:
// v = (Y − cy_k) / a_k (накопитель 8.8 по строкам), x = cx_k + a_k·T[v]. Видимость: дуга
// уровня между точками касания края (z = 0) — по сторонам L/R диапазоны v. Порядок
// пересечений — вложенность (солнце к зрителю, sz > 0 — ночь по краям строки: L — k по
// убыванию, уровень −1; R — по возрастанию, +1; sz < 0 — наоборот). Концы строки — точный
// уровень крайних пар (таблицы t = TX + TY + TZ в движке). MERGE — только границы 1,3,5,7,9
// (полосы по два уровня, растр); HOLD — считать чётные строки, нечётные повторяют.
function spans(lonD, latD, zoom, daylight, opt = {}) {
	const R = ZR[zoom], s = sunDir(lonD, latD, daylight), [sx, sy, sz] = s;
	const A2 = Math.max(1e-9, sx * sx + sz * sz), asz = Math.abs(sz);
	const ks = opt.merge ? [1, 3, 5, 7, 9] : [1, 2, 3, 4, 5, 6, 7, 8, 9, 10];
	const H = Math.sqrt(A2) * R;                         // полувысота E0 (пиксели)
	const tab0 = v => {                                  // E0: X левого / правого (пиксели) или null
		const q = A2 * R * R - v * v;
		if (q < 0) return null;
		const xc = -sx * sy * v / A2, w = asz * Math.sqrt(q) / A2;
		return [xc - w, xc + w];
	};
	// GRID — таблица в узлах через GRID строк, между ними — линейно (узел за краем — край)
	const G = opt.grid || 1;
	const tab = G === 1 ? tab0 : v => {
		const a = Math.floor(v / G) * G, f = (v - a) / G, t0 = tab0(a), t1 = tab0(a + G);
		if (t0 && t1) return [t0[0] + (t1[0] - t0[0]) * f, t0[1] + (t1[1] - t0[1]) * f];
		return tab0(v) && (t0 || t1) ? (t0 || t1) : null;
	};
	// точка E0 (единичный круг ⟂ s) по смещению η (доли R) и стороне — z для видимости
	const lev = ks.map(k => {
		const D = -TB[k - 1] / 250, a = Math.sqrt(1 - D * D);
		// видимость стороны: z точки уровня = D·sz + a·qz ≥ 0; qz на E0 при q.y = η:
		// L: −sy·η·sz/A2 + sx·sgn(sz)·W/A2, R: −sy·η·sz/A2 − sx·sgn(sz)·W/A2, W = √(A2 − η²)
		const sg = sz >= 0 ? 1 : -1;
		const vis = (eta, side) => { const W = Math.sqrt(Math.max(0, A2 - eta * eta)); return D * sz + a * (-sy * eta * sz + (side ? -1 : 1) * sx * sg * W) / A2 >= 0; };
		return { k, D, a, cx: R * D * sx, cy: R * D * sy, vis };
	});
	const Lsign = sz >= 0 ? -1 : 1;                      // смена уровня на пересечении L (R — обратная)
	const exactLev = (p, y) => {
		const X = 2 * p + 0.5 - 128, Y = y + 0.5 - 100, q = (X * X + Y * Y) / (R * R);
		if (q >= 1) return -1;
		const l = Math.floor(shadeOf((X * sx + Y * sy) / R + Math.sqrt(1 - q) * sz) / 3);
		return opt.merge ? [1, 3, 5, 7, 9].filter(k => l >= k).length : l;
	};
	const out = new Int8Array(128 * 200).fill(-1);      // уровень (или полоса) пары
	let nInt = 0;
	for (let y = 0; y < 200; y++) {
		const Y = y + 0.5 - 100;
		let p0 = -1, p1 = -1;
		for (let p = 0; p < 128; p++) if (exactLev(p, y) >= 0) { if (p0 < 0) p0 = p; p1 = p; }
		if (p0 < 0) continue;
		const yc = opt.hold ? (y & ~1) : y, Yc = yc + 0.5 - 100;   // строка расчёта
		const lf = exactLev(p0, y), ll = exactLev(p1, y);
		const cL = [], cR = [];
		for (const L of lev) {
			const v = (Yc - L.cy) / L.a;
			let vi = Math.round(v), t = tab(vi);
			if (opt.interp) {                            // линейно между целыми v (дробь 1/SUB)
				const sub = opt.interp, vq = Math.floor(v * sub) / sub, v0 = Math.floor(vq), f = vq - v0, t0 = tab(v0), t1 = tab(v0 + 1);
				if (t0 && t1) { t = [t0[0] + (t1[0] - t0[0]) * f, t0[1] + (t1[1] - t0[1]) * f]; vi = vq; }
				else if (t0 || t1) { t = t0 || t1; vi = t0 ? v0 : v0 + 1; }
			}
			if (!t) continue;
			const eta = vi / R;
			for (let side = 0; side < 2; side++) {
				if (!L.vis(eta, side)) continue;
				const xq = 512 + 4 * (L.cx + L.a * t[side]);
				const pc = Math.ceil((xq - 2) / 8);
				(side ? cR : cL).push({ k: L.k, pc });
			}
		}
		// порядок по вложенности, монотонно
		const ordL = sz >= 0 ? (a, b) => b.k - a.k : (a, b) => a.k - b.k, ordR = sz >= 0 ? (a, b) => a.k - b.k : (a, b) => b.k - a.k;
		cL.sort(ordL); cR.sort(ordR);
		const cr = [...cL.map(c => ({ pc: c.pc, d: Lsign })), ...cR.map(c => ({ pc: c.pc, d: -Lsign }))];
		let m = -1e9; for (const c of cr) { if (c.pc < m) c.pc = m; m = c.pc; }
		const act = cr.filter(c => c.pc > p0 && c.pc <= p1);
		let cur = lf, ci = 0, prev = -1;
		const top = opt.merge ? 5 : 10;
		for (let p = p0; p <= p1; p++) {
			while (ci < act.length && act[ci].pc <= p) { cur = Math.max(0, Math.min(top, cur + act[ci].d)); ci++; }
			const l = ci === act.length && act.length ? ll : cur;
			out[y * 128 + p] = l;
			if (l !== prev) { nInt++; prev = l; }
		}
	}
	// сверка с точным уровнем пары
	let diff = 0, tot = 0, far = 0;
	for (let y = 0; y < 200; y++) for (let p = 0; p < 128; p++) {
		const e = exactLev(p, y);
		if (e < 0) continue;
		tot++;
		const d = Math.abs(out[y * 128 + p] - e);
		if (d) { diff++; if (d > 1) far++; }
	}
	return { diff, tot, far, nInt, out };
}

// тень без ограничения сверху (для вычитания шума — как temp.x до Clamp в getShadowValue)
function shadeOfRaw(d) {
	const t = -250 * d;
	if (t < -110) return -31;
	if (t > 120) return 50;
	return grad[Math.trunc(t) + 120];
}

if (require.main === module) {
	const a = process.argv.slice(2).map(Number);
	if (process.env.SPANS) {
		for (const hold of [false, true]) {
			const line = [];
			for (let z = 0; z < 6; z++) {
				let d = 0, t = 0, f = 0, ni = 0, v = 0;
				for (let lon = 0; lon < 360; lon += 45) for (const lat of [-50, 0, 35]) for (const dl of [0, 0.15, 0.3, 0.45, 0.7, 0.9]) {
					const r = spans(lon, lat, z, dl, { merge: z >= 3, hold }); d += r.diff; t += r.tot; f += r.far; ni += r.nInt; v++;
				}
				line.push(`z${z} ${(100 * d / t).toFixed(2)}% far ${f} int/view ${(ni / v).toFixed(0)}`);
			}
			console.log(`hold ${hold}: ` + line.join('; '));
		}
	} else if (process.env.CHAINS) {
		for (const N of [16, 24, 32, 48, 64]) {
			const line = [];
			for (let z = 0; z < 6; z++) {
				let d = 0, t = 0, f = 0, nc = 0, v = 0;
				for (let lon = 0; lon < 360; lon += 45) for (const lat of [-50, 0, 35]) for (const dl of [0, 0.15, 0.3, 0.45, 0.7, 0.9]) {
					const r = chains(lon, lat, z, dl, N); d += r.diff; t += r.tot; f += r.far; nc += r.nCross; v++;
				}
				line.push(`z${z} ${(100 * d / t).toFixed(2)}% far ${f} cr/view ${(nc / v).toFixed(0)}`);
			}
			console.log(`N ${N}: ` + line.join('; '));
		}
	} else if (process.env.PIC) {
		const r = picture(a[0] ?? 0, a[1] ?? 0, a[2] ?? 0, a[3] ?? 0.3, `tmp/shadow_z${a[2] ?? 0}.png`);
		console.log(`mean |truth - ours| ${r.mean.toFixed(3)} palette steps, >3: ${r.big} of ${r.n} pixels -> tmp/shadow_z${a[2] ?? 0}.png (left OpenXcom, right scheme)`);
	} else if (a.length) {
		const r = run(a[0] ?? 0, a[1] ?? 0, a[2] ?? 0, a[3] ?? 0.3);
		console.log(`sun ${r.s.map(v => v.toFixed(3))}; rows with shadow ${r.bands}, max segments ${r.maxSeg}; level mismatch ${r.diff} of ${r.tot} pairs`);
	} else {
		console.log('thresholds t_k:', TK.join(' '));
		let d = 0, t = 0;
		for (const z of [0, 2, 5]) for (let dl = 0; dl < 1; dl += 0.13) for (const lat of [-60, 0, 40]) {
			const r = run(20, lat, z, dl); d += r.diff; t += r.tot;
		}
		console.log(`sweep: level mismatch ${d} of ${t} pairs (${(100 * d / t).toFixed(3)}%)`);
	}
}
module.exports = { run, shadeOf, TK, sunDir };
