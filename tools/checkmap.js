#!/usr/bin/env node
// Проверка раскладки по .map линкера SDCC: sdld молча накладывает секции,
// поэтому каждая сборка проверяет границы (03 §1, §7 п.7).
//
// node checkmap.js <file.map> <правила...>
//   правило: ОБЛАСТЬ[,ОБЛАСТЬ...]=НАЧАЛО-КОНЕЦ  (конец не включительно)
//   пример: _CODE,_HOME,_INITIALIZER,_GSINIT,_GSFINAL=0x4000-0xA000
'use strict';
const fs = require('fs');

const [mapFile, ...rules] = process.argv.slice(2);
if (!mapFile) { console.error('usage: checkmap.js file.map RULE...'); process.exit(1); }

// Строки вида: "_CODE    00004000    000012AB =  4779. bytes (REL,CON)"
const areas = {};
for (const line of fs.readFileSync(mapFile, 'latin1').split(/\r?\n/)) {
	const m = /^(_\w+)\s+([0-9A-F]{8})\s+([0-9A-F]{8})\s*=\s*(\d+)\.\s+bytes/.exec(line);
	if (m) areas[m[1]] = { start: parseInt(m[2], 16), size: parseInt(m[3], 16) };
}

let ok = true;
const hex = v => '#' + v.toString(16).toUpperCase().padStart(4, '0');
for (const rule of rules) {
	const [names, range] = rule.split('=');
	const [lo, hi] = range.split('-').map(Number);
	for (const name of names.split(',')) {
		const a = areas[name];
		if (!a || a.size === 0) continue;
		const end = a.start + a.size;
		const bad = a.start < lo || end > hi;
		console.log(`  ${name.padEnd(14)} ${hex(a.start)}-${hex(end - 1)} ${String(a.size).padStart(6)} bytes  [${hex(lo)}-${hex(hi - 1)}]${bad ? '  <-- OUT OF RANGE' : ''}`);
		if (bad) ok = false;
	}
}
// Пересечения всех непустых областей между собой
const list = Object.entries(areas).filter(([, a]) => a.size > 0 && a.start !== undefined)
	.filter(([n]) => n !== '_HEADER' && !n.startsWith('_HEADER'));
for (let i = 0; i < list.length; i++)
	for (let j = i + 1; j < list.length; j++) {
		const [n1, a] = list[i], [n2, b] = list[j];
		if (a.start < b.start + b.size && b.start < a.start + a.size) {
			console.log(`  OVERLAP: ${n1} and ${n2}`);
			ok = false;
		}
	}
if (!ok) { console.error('checkmap: layout check FAILED'); process.exit(1); }
console.log('checkmap: OK');
