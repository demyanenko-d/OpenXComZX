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

if (require.main === module) {
	const a = process.argv.slice(2).map(Number);
	if (a.length) {
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
