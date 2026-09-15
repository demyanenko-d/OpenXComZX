// Сверка снимка эмулятора (окно глобуса 256x200) с эталоном «художник по многоугольникам»
// модели tools/globe_model.js. node tools\globe_emucmp.js снимок.png lon16 lat16 zoom [out.png]
// (lon16/lat16 — ctx.globe_lon/lat, 16-битные углы, как в peek). Цвет эмулятора для индекса
// палитры — по большинству там, где модель совпала с эталоном.
'use strict';
const fs = require('fs'), zlib = require('zlib');
const M = require('./globe_model.js');

function readPng(file) {
	const b = fs.readFileSync(file);
	let o = 8, w, h, ct, idat = [];
	while (o < b.length) {
		const len = b.readUInt32BE(o), type = b.toString('ascii', o + 4, o + 8), d = b.subarray(o + 8, o + 8 + len);
		if (type === 'IHDR') { w = d.readUInt32BE(0); h = d.readUInt32BE(4); ct = d[9]; if (d[8] !== 8) throw 'bit depth'; }
		else if (type === 'IDAT') idat.push(d);
		o += 12 + len;
	}
	const bpp = ct === 6 ? 4 : ct === 2 ? 3 : (() => { throw 'color type ' + ct; })();
	const raw = zlib.inflateSync(Buffer.concat(idat)), st = w * bpp, px = Buffer.alloc(w * h * 3);
	let prev = Buffer.alloc(st);
	for (let y = 0; y < h; y++) {
		const f = raw[y * (st + 1)], line = Buffer.from(raw.subarray(y * (st + 1) + 1, (y + 1) * (st + 1)));
		for (let i = 0; i < st; i++) {
			const a = i >= bpp ? line[i - bpp] : 0, bb = prev[i], c = i >= bpp ? prev[i - bpp] : 0;
			let v = line[i];
			if (f === 1) v += a; else if (f === 2) v += bb; else if (f === 3) v += (a + bb) >> 1;
			else if (f === 4) { const p = a + bb - c, pa = Math.abs(p - a), pb = Math.abs(p - bb), pc = Math.abs(p - c); v += pa <= pb && pa <= pc ? a : pb <= pc ? bb : c; }
			line[i] = v & 255;
		}
		for (let x = 0; x < w; x++) for (let k = 0; k < 3; k++) px[(y * w + x) * 3 + k] = line[x * bpp + k];
		prev = line;
	}
	return { w, h, px };
}

const [file, lon16, lat16, zs, out] = process.argv.slice(2);
const zoom = +zs, lonD = (+lon16) * 360 / 65536, latD = ((+lat16 << 16) >> 16) * 360 / 65536;
const img = readPng(file);
const r = M.render(lonD, latD, zoom, true);
const set = 2 - (zoom >> 1);
const idxOf = (t, x, y) => (t === 255 || t === 13) ? M.OCEAN : M.TEX[(set * 13 + t) * 1024 + (y & 31) * 32 + (x & 31)];
// цвет эмулятора по индексу палитры
const votes = new Map();
const emu = (x, y) => { const i = (y * img.w + x) * 3; return (img.px[i] << 16) | (img.px[i + 1] << 8) | img.px[i + 2]; };
for (let y = 0; y < 200; y++) for (let x = 0; x < 256; x++) {
	const t = r.truth[y * 256 + x];
	if (t < 0 || r.img[y * 256 + x] !== t) continue;
	const k = idxOf(t, x, y), c = emu(x, y);
	if (!votes.has(k)) votes.set(k, new Map());
	const m = votes.get(k); m.set(c, (m.get(c) || 0) + 1);
}
const color = new Map();
for (const [k, m] of votes) color.set(k, [...m.entries()].sort((a, b) => b[1] - a[1])[0][0]);
// расхождения по парам (левый пиксель), грубые — эталон однороден в 5x5 пар
let diff = 0, gross = 0, total = 0, mdiff = 0;
const bad = new Uint8Array(256 * 200);
for (let y = 0; y < 200; y++) for (let x = 0; x < 256; x += 2) {
	const t = r.truth[y * 256 + x];
	if (t < 0) continue;
	total++;
	const want = color.get(idxOf(t, x, y));
	if (want === undefined) continue;
	if (emu(x, y) !== want) {
		diff++; bad[y * 256 + x] = 1;
		let uni = y >= 2 && y < 198 && x >= 4 && x < 252;
		for (let dy = -2; dy <= 2 && uni; dy++) for (let dx = -4; dx <= 4; dx += 2) if (r.truth[(y + dy) * 256 + x + dx] !== t) { uni = false; break; }
		if (uni) { gross++; bad[y * 256 + x] = 2; }
	}
	// эмулятор против модели (та же логика — должны почти совпадать)
	const mt = r.img[y * 256 + x];
	if (mt >= 0) { const mw = color.get(idxOf(mt, x, y)); if (mw !== undefined && emu(x, y) !== mw) mdiff++; }
}
console.log(`view ${lonD.toFixed(2)},${latD.toFixed(2)} z${zoom}: emu vs truth ${diff} of ${total} pairs (${(100 * diff / total).toFixed(2)}%), gross ${gross}; emu vs model ${mdiff}`);
if (out) {
	const W2 = 512, H2 = 400, raw = Buffer.alloc((W2 * 3 + 1) * H2);
	for (let y = 0; y < H2; y++) for (let x = 0; x < W2; x++) {
		const X = x >> 1, Y = y >> 1, i = (Y * img.w + X) * 3, o = y * (W2 * 3 + 1) + 1 + x * 3;
		const bb = bad[Y * 256 + (X & ~1)];
		if (bb) { raw[o] = 255; raw[o + 1] = bb === 2 ? 0 : 128; raw[o + 2] = 255; }
		else { raw[o] = img.px[i]; raw[o + 1] = img.px[i + 1]; raw[o + 2] = img.px[i + 2]; }
	}
	const CRC = (() => { const t = new Uint32Array(256); for (let n = 0; n < 256; n++) { let c = n; for (let k = 0; k < 8; k++) c = c & 1 ? 0xEDB88320 ^ (c >>> 1) : c >>> 1; t[n] = c >>> 0; } return t; })();
	const crc32 = b => { let c = 0xFFFFFFFF; for (let i = 0; i < b.length; i++) c = CRC[(c ^ b[i]) & 255] ^ (c >>> 8); return (c ^ 0xFFFFFFFF) >>> 0; };
	const chunk = (t, d) => { const l = Buffer.alloc(4); l.writeUInt32BE(d.length); const td = Buffer.concat([Buffer.from(t), d]); const c = Buffer.alloc(4); c.writeUInt32BE(crc32(td)); return Buffer.concat([l, td, c]); };
	const ih = Buffer.alloc(13); ih.writeUInt32BE(W2, 0); ih.writeUInt32BE(H2, 4); ih[8] = 8; ih[9] = 2;
	fs.writeFileSync(out, Buffer.concat([Buffer.from([137, 80, 78, 71, 13, 10, 26, 10]), chunk('IHDR', ih), chunk('IDAT', zlib.deflateSync(raw)), chunk('IEND', Buffer.alloc(0))]));
}
