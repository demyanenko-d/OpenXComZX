#!/usr/bin/env node
// Упаковщик SPG 1.0 (TS-Config) из Intel HEX линкера SDCC.
//
// Формат — project_docs/03_build_architecture.md §6 и
// unreal/Unreal/media/snapshot.h (hdrSPG1_0). Блоки грузятся без упаковки.
//
// node mkspg.js <in.ihx> <out.spg> --pc 0x7FF0 --sp 0x3E00
//     --win 0:0x04 --win 1:0x05 [--bank 1:0x30]... [--win3 0x0F] [--clk 0]
//     [--data <page>:<file>]...
//
// --win W:P  — адреса окна W (#0000/#4000/#8000/#C000) кладутся в страницу P.
// --bank N:P — банк кода N (виртуальные адреса N*#10000+#8000..#BFFF,
//              -Wl-bBANKn линкера) кладётся в страницу P.
// --data P:F — файл F целиком в страницы P, P+1, ... (по 16 КБ).
'use strict';
const fs = require('fs');

function die(msg) { console.error('mkspg: ' + msg); process.exit(1); }
const num = s => { const v = Number(s); if (!Number.isInteger(v)) die('bad number ' + s); return v; };

const args = process.argv.slice(2);
if (args.length < 2) die('usage: mkspg.js in.ihx out.spg --pc A --sp A --win W:P ...');
const [inFile, outFile] = args;
const opt = { pc: null, sp: null, win: {}, bank: {}, win3: 0x0F, clk: 0, data: [] };
for (let i = 2; i < args.length; i++) {
	const a = args[i], v = args[++i];
	if (v === undefined) die('missing value for ' + a);
	if (a === '--pc') opt.pc = num(v);
	else if (a === '--sp') opt.sp = num(v);
	else if (a === '--win3') opt.win3 = num(v);
	else if (a === '--clk') opt.clk = num(v);
	else if (a === '--win') { const [w, p] = v.split(':'); opt.win[num(w)] = num(p); }
	else if (a === '--bank') { const [n, p] = v.split(':'); opt.bank[num(n)] = num(p); }
	else if (a === '--data') { const k = v.indexOf(':'); opt.data.push({ page: num(v.slice(0, k)), file: v.slice(k + 1) }); }
	else die('unknown option ' + a);
}
if (opt.pc === null || opt.sp === null) die('--pc and --sp are required');

// --- страницы 16 КБ: { buf, usedSectors[32] }
const pages = new Map();
function pageBuf(p) {
	if (!pages.has(p)) pages.set(p, { buf: Buffer.alloc(0x4000), sec: new Uint8Array(32) });
	return pages.get(p);
}

// Физическая страница для 24-битного адреса линкера.
function pageOf(addr) {
	const seg = addr >>> 16, a = addr & 0xFFFF;
	if (seg === 0) {
		const p = opt.win[a >> 14];
		if (p === undefined) die(`byte at #${a.toString(16)} (window ${a >> 14}) but no --win ${a >> 14}:page`);
		return p;
	}
	if (a < 0x8000 || a > 0xBFFF) die(`banked byte at #${addr.toString(16)} outside #8000-#BFFF`);
	const p = opt.bank[seg];
	if (p === undefined) die(`byte in bank ${seg} (#${addr.toString(16)}) but no --bank ${seg}:page`);
	return p;
}

// --- Intel HEX (записи 00 данные, 01 конец, 04 старшие 16 бит адреса)
let upper = 0;
for (const line of fs.readFileSync(inFile, 'latin1').split(/\r?\n/)) {
	if (!line.startsWith(':')) continue;
	const b = Buffer.from(line.slice(1), 'hex');
	const len = b[0], addr = (b[1] << 8) | b[2], type = b[3];
	if (type === 1) break;
	if (type === 4) { upper = (b[4] << 8) | b[5]; continue; }
	if (type !== 0) die('unsupported ihx record type ' + type);
	for (let i = 0; i < len; i++) {
		const full = upper * 0x10000 + addr + i;
		const pg = pageBuf(pageOf(full));
		const off = full & 0x3FFF;
		pg.buf[off] = b[4 + i];
		pg.sec[off >> 9] = 1;
	}
}
for (const d of opt.data) {
	const data = fs.readFileSync(d.file);
	for (let off = 0; off < data.length; off += 0x4000) {
		const pg = pageBuf(d.page + off / 0x4000);
		const chunk = data.subarray(off, off + 0x4000);
		chunk.copy(pg.buf, 0);
		for (let s = 0; s < Math.ceil(chunk.length / 512); s++) pg.sec[s] = 1;
	}
}

// --- блоки: непрерывные серии занятых секторов в странице
const blocks = [];
for (const [page, pg] of [...pages].sort((a, b) => a[0] - b[0])) {
	if (page >= 0xF0) console.warn(`mkspg: warning: page #${page.toString(16)} >= #F0 is used by loaders`);
	let s = 0;
	while (s < 32) {
		if (!pg.sec[s]) { s++; continue; }
		let e = s;
		while (e < 32 && pg.sec[e]) e++;
		blocks.push({ page, sec: s, count: e - s, data: pg.buf.subarray(s * 512, e * 512) });
		s = e;
	}
}
if (blocks.length === 0) die('nothing to load');
if (blocks.length > 256) die('too many blocks');

// --- заголовок 1024 байта
const hdr = Buffer.alloc(1024);
const now = new Date();
hdr.write('OpenXComZX'.padEnd(32), 0, 'latin1');
hdr.write('SpectrumProg', 32, 'latin1');
hdr[44] = 0x10;
hdr[45] = now.getDate(); hdr[46] = now.getMonth() + 1; hdr[47] = now.getFullYear() % 100;
hdr.writeUInt16LE(opt.pc, 48);
hdr.writeUInt16LE(opt.sp, 50);
hdr[52] = opt.win3;
hdr[53] = opt.clk & 7;
hdr.writeUInt16LE(0, 54);            // pgmgr_addr (только 0.x)
hdr.writeUInt16LE(0xFF00, 56);       // rsd_addr: 16 байт передачи управления WC — в win3 (страница --win3)
hdr.writeUInt16LE(blocks.length, 58);
hdr[60] = now.getSeconds(); hdr[61] = now.getMinutes(); hdr[62] = now.getHours();
hdr.write('mkspg.js'.padEnd(32), 80, 'latin1');
blocks.forEach((b, i) => {
	const last = i === blocks.length - 1;
	hdr[256 + i * 3] = (b.sec & 0x1F) | (last ? 0x80 : 0);
	hdr[256 + i * 3 + 1] = (b.count - 1) & 0x1F;   // упаковка 0
	hdr[256 + i * 3 + 2] = b.page;
});

fs.writeFileSync(outFile, Buffer.concat([hdr, ...blocks.map(b => b.data)]));
console.log(`mkspg: ${outFile}: ${blocks.length} blocks, PC #${opt.pc.toString(16)}, SP #${opt.sp.toString(16)}`);
for (const b of blocks)
	console.log(`  page #${b.page.toString(16).padStart(2, '0')} +#${(b.sec * 512).toString(16).padStart(4, '0')} ${b.count * 512} bytes`);
