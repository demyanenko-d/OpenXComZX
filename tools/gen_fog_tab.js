// Таблица линий тумана войны (src/battlescape/vision.c, 16 §5.2): все линии сектора обзора
// TileEngine::calculateFOV (клетка сектора x этаж) в порядке обхода дерева путей, у каждой — длина
// общего начала с предыдущей. Прототип и проверка на точность — tools/proto/fog_fast.js.
//
//   node tools/gen_fog_tab.js <выход.bin>
//
// Формат (две страницы подряд с FOGTAB_PAGE): слово n0, слово n1 — число записей для
// направления 0 (прямо) и 1 (наискось), затем записи по 5 байт: n0 штук, потом n1:
//   [0] x (0..20, вдоль сектора) | (dz + 3) << 5   (dz — разность этажей -3..3)
//   [1] y со знаком (поперёк сектора, как в cone() прототипа)
//   [2] L — сколько первых клеток пути общие с предыдущей записью (0 у первой)
//   [3..4] J — номер следующей записи (в своей половине) с L меньше, чем у этой; n — нет такой.
//   Если общее начало с последней посчитанной линией уже упёрлось, все записи до J — то же
//   поддерево, их можно не перебирать (vision.c fov_unit)
// Координаты — локальные для направлений 0 и 1; прочие направления — знаками и обменом осей
// (как в calculateFOV: signX/signY, swap при 0 и 4); линия calculateLine симметрична — проверено
// прототипом (вариант canon: расхождение с точным расчётом 0).
'use strict';
const fs = require('fs');

const MAXV = 20, DZ = 3;
const sgX = [1, 1, 1, 1, -1, -1, -1, -1], sgY = [-1, -1, -1, 1, 1, 1, -1, -1];

// TileEngine::calculateLine — клетки пути от (0,0,0) до цели
function linePath(x1, y1, z1) {
	let x0 = 0, y0 = 0, z0 = 0, t;
	const out = [];
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

function table(dir) {
	const swap = dir === 0 || dir === 4;
	const lines = [];
	for (let x = 0; x <= MAXV; x++) {
		const y1 = dir & 1 ? 0 : -x, y2 = dir & 1 ? MAXV : x;
		for (let y = y1; y <= y2; y++) {
			if (x * x + y * y > MAXV * MAXV) continue;
			const wx = sgX[dir] * (swap ? y : x), wy = sgY[dir] * (swap ? x : y);
			for (let dz = -DZ; dz <= DZ; dz++) lines.push({ x, y, dz, path: linePath(wx, wy, dz) });
		}
	}
	lines.sort((a, b) => {
		const pa = a.path, pb = b.path;
		for (let i = 0; i < Math.min(pa.length, pb.length); i++) {
			const d = (pa[i][0] - pb[i][0]) || (pa[i][1] - pb[i][1]) || (pa[i][2] - pb[i][2]);
			if (d) return d;
		}
		return pa.length - pb.length;
	});
	const L = [];
	for (let i = 0; i < lines.length; i++) {
		let l = 0;
		if (i) {
			const pa = lines[i - 1].path, pb = lines[i].path;
			while (l < pa.length && l < pb.length && pa[l][0] === pb[l][0] && pa[l][1] === pb[l][1] && pa[l][2] === pb[l][2]) l++;
		}
		L.push(l);
	}
	// J[i] — ближайшая следующая запись с L < L[i] (стек убывающих L)
	const J = new Array(lines.length).fill(lines.length), st = [];
	for (let i = 0; i < lines.length; i++) {
		while (st.length && L[st[st.length - 1]] > L[i]) J[st.pop()] = i;
		st.push(i);
	}
	const out = [];
	for (let i = 0; i < lines.length; i++) {
		const e = lines[i];
		if (e.x > 31 || e.y < -128 || e.y > 127 || L[i] > 255) throw new Error('entry out of range');
		out.push(e.x | ((e.dz + DZ) << 5), e.y & 255, L[i], J[i] & 255, J[i] >> 8);
	}
	return out;
}

const t0 = table(0), t1 = table(1);
const n0 = t0.length / 5, n1 = t1.length / 5;
const buf = Buffer.alloc(4 + t0.length + t1.length);
buf.writeUInt16LE(n0, 0);
buf.writeUInt16LE(n1, 2);
Buffer.from(t0).copy(buf, 4);
Buffer.from(t1).copy(buf, 4 + t0.length);
if (buf.length > 32768) throw new Error('fog table does not fit two pages: ' + buf.length);
fs.writeFileSync(process.argv[2], buf);
console.log(`gen_fog_tab: ${n0} + ${n1} lines, ${buf.length} bytes -> ${process.argv[2]}`);
