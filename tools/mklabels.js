#!/usr/bin/env node
// Метки для отладчика эмулятора из .noi линкера SDCC (+ статические функции из .cdb).
// Формат эмулятора (unreal/Unreal/ui/debugger/dbglabls.cpp): «PP:AAAA имя» —
// физическая страница и адрес; по ним же сценарии адресуют переменные (poke/expect),
// профилировщик относит такты к ближайшей метке снизу.
//
// node mklabels.js <in.noi> <out.labels> [--cdb oxz.cdb] --win 0:0x04 --win 1:0x05 [--bank 1:0x30]...
// Адреса банков (N*#10000 + #8000..#BFFF) переводятся в страницы по --bank.
// Статические функции (.cdb, sdcc --debug) называются «модуль$функция».
'use strict';
const fs = require('fs');

const [inFile, outFile, ...rest] = process.argv.slice(2);
if (!inFile || !outFile) { console.error('usage: mklabels.js in.noi out.labels [--cdb f] --win W:P --bank N:P ...'); process.exit(1); }
const win = {}, bank = {};
let cdb = null;
for (let i = 0; i < rest.length; i += 2) {
	if (rest[i] === '--cdb') { cdb = rest[i + 1]; continue; }
	const [k, v] = rest[i + 1].split(':').map(Number);
	if (rest[i] === '--win') win[k] = v;
	else if (rest[i] === '--bank') bank[k] = v;
	else { console.error('unknown option ' + rest[i]); process.exit(1); }
}

const out = [];
function add(name, full) {
	const seg = full >>> 16, addr = full & 0xFFFF;
	const page = seg === 0 ? win[addr >> 14] : bank[seg];
	if (page === undefined) return;
	out.push(`${page.toString(16).toUpperCase().padStart(2, '0')}:${addr.toString(16).toUpperCase().padStart(4, '0')} ${name}`);
}

for (const line of fs.readFileSync(inFile, 'latin1').split(/\r?\n/)) {
	// DEF _main 0x4012   /   DEF _bank_a_calc 0x18000
	const m = /^DEF\s+(\S+)\s+0x([0-9A-Fa-f]+)/.exec(line);
	if (!m) continue;
	const name = m[1], full = parseInt(m[2], 16);
	if (name.startsWith('s_') || name.startsWith('l_') || name.startsWith('.')) continue;  // границы областей
	if (name.includes('$')) continue;      // отладочные записи --debug (строки C/asm)
	add(name, full);
}

if (cdb && fs.existsSync(cdb)) {
	// F:Fmodule$func$0_0$0(...)  — статическая функция; L:Fmodule$func$0_0$0:ADDR — её адрес
	const text = fs.readFileSync(cdb, 'latin1').split(/\r?\n/);
	const funcs = new Set();
	for (const l of text) { const m = /^F:F([^$]+)\$([^$]+)\$/.exec(l); if (m) funcs.add(m[1] + '$' + m[2]); }
	for (const l of text) {
		const m = /^L:F([^$]+)\$([^$]+)\$[^:]*:([0-9A-Fa-f]+)$/.exec(l);
		if (m && funcs.has(m[1] + '$' + m[2])) add(m[1] + '$' + m[2], parseInt(m[3], 16));
	}
}
out.sort();
fs.writeFileSync(outFile, out.join('\r\n') + '\r\n', 'latin1');
console.log(`mklabels: ${outFile}: ${out.length} labels`);
