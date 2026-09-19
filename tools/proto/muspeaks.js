#!/usr/bin/env node
// Где в потоке музыки стоят «тяжёлые» кадры — те, в которых чип получает сразу много
// записей. Именно на них приходятся щелчки, привязанные к месту в треке (22 §11.5):
// смена части музыки, когда несколько каналов одновременно получают и новую ноту, и
// новый тембр. Сравнив времена таких кадров с временами скачков огибающей из
// `wavscan.js` (OXZ_CLICKS=1), видно, одно ли это событие.
//
//   node tools/proto/muspeaks.js <файл .PAK> <id трека в hex> [порог записей]
//   node tools/proto/muspeaks.js tmp/sd/OXZ/TFTD/MUSICFM.PAK 32d 20
'use strict';
const fs = require('fs');

const FRAME_HZ = 3500000 / 71680;

function pak(file) {
	const b = fs.readFileSync(file);
	const n = b.readUInt16LE(6), out = [];
	for (let i = 0; i < n; i++) {
		const e = 16 + i * 16;
		out.push({ id: b.readUInt16LE(e), off: b.readUInt16LE(e + 4) * 512, size: b.readUInt32LE(e + 6) & 0xFFFFFF });
	}
	return { buf: b, ents: out };
}

// Число пар «регистр, значение» в каждом кадре потока (формат — 22 §4.3).
function* counts(buf, base, size) {
	let p = base + 12;
	const end = base + size;
	for (;;) {
		if (p >= end) return;
		const b = buf[p];
		if (b === 0xFF) return;
		if (b >= 0x81) { for (let k = 0; k < b - 0x80; k++) yield 0; p++; continue; }
		const n0 = buf[p++]; p += n0 * 2;
		const n1 = buf[p++]; p += n1 * 2;
		yield n0 + n1;
	}
}

const file = process.argv[2], id = parseInt(process.argv[3], 16), th = +(process.argv[4] || 20);
if (!file || isNaN(id)) {
	console.error('usage: node tools/proto/muspeaks.js <PAK> <id hex> [порог]');
	process.exit(2);
}
const P = pak(file);
const e = P.ents.find(x => x.id === id);
if (!e) { console.error('нет такого трека'); process.exit(2); }
const loop = P.buf.readUInt32LE(e.off);
let i = 0, total = 0, max = 0;
const big = [];
for (const n of counts(P.buf, e.off, e.size)) {
	total += n;
	if (n > max) max = n;
	if (n >= th) big.push(`${(i / FRAME_HZ).toFixed(1)}с:${n}`);
	i++;
}
console.log(`${file} трек ${id.toString(16)}: ${i} кадров (${(i / FRAME_HZ).toFixed(1)} с), ${e.size} Б, ` +
	`loop_offset ${loop === 0xFFFFFFFF ? 'нет' : loop}, записей ${total} (${(total / i).toFixed(2)} на кадр), максимум ${max}`);
console.log(`кадры с ${th}+ записями: ${big.length ? big.join(' ') : 'нет'}`);
