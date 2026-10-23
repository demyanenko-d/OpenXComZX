// Трасса аварии эмулятора (tmp/oxz_trace.txt, «стр:адрес» для банков Win2, «адрес» для ядра)
// -> имена функций по меткам и карте компоновщика, подряд идущие адреса одной функции
// схлопываются: «имя+смещение_первого .. +последнего (n)».
//   node tools\traceann.js [tmp\oxz_trace.txt] [последних N строк вывода]
'use strict';
const fs = require('fs');
const path = require('path');
const root = path.join(__dirname, '..');
const tr = process.argv[2] || path.join(root, 'tmp', 'oxz_trace.txt');
const tail = +(process.argv[3] || 0);

// Метки: глобальные (oxz.labels) + статические функции из карты (Fмодуль$имя), если есть
const syms = [];
for (const l of fs.readFileSync(path.join(root, 'tmp', 'build', 'oxz.labels'), 'utf8').split(/\r?\n/)) {
	const m = /^([0-9A-F]{2}):([0-9A-F]{4}) (\S+)/i.exec(l);
	if (m) syms.push({ pg: parseInt(m[1], 16), a: parseInt(m[2], 16), n: m[3] });
}
function find(pg, a) {
	let best = null;
	for (const s of syms) if (s.pg === pg && s.a <= a && (!best || s.a > best.a)) best = s;
	return best;
}

const out = [];
let cur = null;
for (const line of fs.readFileSync(tr, 'utf8').split(/\r?\n/)) {
	const t = line.trim();
	if (!t) continue;
	let pg, a;
	const m = /^([0-9A-F]{2}):([0-9A-F]{4})$/i.exec(t);
	if (m) { pg = parseInt(m[1], 16); a = parseInt(m[2], 16); }
	else { a = parseInt(t, 16); pg = a < 0x4000 ? 0x04 : 0x05; }
	const s = find(pg, a);
	const name = s ? s.n : `?${pg.toString(16)}`;
	const off = s ? a - s.a : a;
	if (cur && cur.name === name && cur.pg === pg) { cur.last = off; cur.n++; }
	else { if (cur) out.push(cur); cur = { name, pg, first: off, last: off, n: 1 }; }
}
if (cur) out.push(cur);
const lines = out.map((c) => `${c.pg.toString(16).padStart(2, '0')} ${c.name}+${c.first.toString(16)}${c.n > 1 ? ' .. +' + c.last.toString(16) : ''} (${c.n})`);
console.log((tail ? lines.slice(-tail) : lines).join('\n'));
