#!/usr/bin/env node
// Разбор готовых потоков музыки (tmp/sd/OXZ/<игра>/MUSICAY.PAK, MUSICFM.PAK): что реально
// уходит в чип — сколько каналов звучит, какие частоты и громкости. Нужен, чтобы понимать
// жалобы на звук («громко и хрипит», «тихо и пусто») по данным, а не на слух.
//   node tools/proto/chipscan.js [TFTD|UFO]
'use strict';
const fs = require('fs');
const path = require('path');

const game = (process.argv[2] || 'TFTD').toUpperCase();
const dir = path.join(__dirname, '..', '..', 'tmp', 'sd', 'OXZ', game);
const AY_CLOCK = 1774400, FM_CLOCK = 3548800, FRAME_HZ = 3500000 / 71680;

function pak(file) {
	const b = fs.readFileSync(file);
	const n = b.readUInt16LE(6), out = [];
	for (let i = 0; i < n; i++) {
		const e = 16 + i * 16;
		out.push({
			id: b.readUInt16LE(e),
			off: b.readUInt16LE(e + 4) * 512,
			size: b.readUInt32LE(e + 6) & 0xFFFFFF,
			frames: b.readUInt16LE(e + 10),
			maxw: b.readUInt16LE(e + 12),
		});
	}
	return { buf: b, ents: out };
}

// Поток: u32 loop, u32 frames, u16 maxw, u16 resync; записи #00-#7F — n0 пар банка 0,
// байт n1 и n1 пар банка 1; #81-#FE — пауза; #FF — конец.
function* frames(buf, base, size) {
	let p = base + 12;
	const end = base + size;
	for (;;) {
		if (p >= end) return;
		const b = buf[p];
		if (b === 0xFF) return;
		if (b >= 0x81) { for (let k = 0; k < b - 0x80; k++) yield null; p++; continue; }
		const w = [];
		const n0 = buf[p++];
		for (let i = 0; i < n0; i++) { w.push([0, buf[p], buf[p + 1]]); p += 2; }
		const n1 = buf[p++];
		for (let i = 0; i < n1; i++) { w.push([1, buf[p], buf[p + 1]]); p += 2; }
		yield w;
	}
}

function scanAy(buf, e) {
	const reg = new Array(16).fill(0);
	let frames_ = 0, voiced = 0, notes = 0, low = 0, vhist = new Array(16).fill(0);
	let fmin = 1e9, fmax = 0;
	for (const w of frames(buf, e.off, e.size)) {
		frames_++;
		if (w) for (const [, r, v] of w) if (r < 16) reg[r] = v;
		for (let c = 0; c < 3; c++) {
			const vol = reg[8 + c] & 0x0F;
			vhist[vol]++;
			if (!vol || (reg[7] >> c & 1)) continue;
			voiced++;
			const per = (reg[c * 2] | (reg[c * 2 + 1] & 0x0F) << 8) || 1;
			const f = AY_CLOCK / (16 * per);
			notes++;
			if (f < 55) low++;
			if (f < fmin) fmin = f;
			if (f > fmax) fmax = f;
		}
	}
	return { frames: frames_, chans: voiced / frames_, vhist, low: low / Math.max(1, notes), fmin, fmax };
}

function scanFm(buf, e) {
	const reg = [new Array(256).fill(0), new Array(256).fill(0)];
	const on = [[0, 0, 0], [0, 0, 0]];
	let frames_ = 0, voiced = 0, keyons = 0, tlSum = 0, tlN = 0, fmin = 1e9, fmax = 0;
	for (const w of frames(buf, e.off, e.size)) {
		frames_++;
		if (w) for (const [bank, r, v] of w) {
			if (r === 0x28) { const ch = v & 3; if (ch < 3) { if (v & 0xF0) keyons++; on[bank][ch] = (v & 0xF0) ? 1 : 0; } }
			else reg[bank][r] = v;
		}
		for (let bank = 0; bank < 2; bank++)
			for (let ch = 0; ch < 3; ch++) {
				if (!on[bank][ch]) continue;
				voiced++;
				const tl = reg[bank][0x48 + ch] & 0x7F;   // TL несущей (S2)
				tlSum += tl; tlN++;
				const hi = reg[bank][0xA4 + ch], lo = reg[bank][0xA0 + ch];
				const fn = ((hi & 7) << 8) | lo, block = hi >> 3;
				const f = fn * FM_CLOCK * Math.pow(2, block - 1) / (72 * (1 << 20));
				if (f > 1) { if (f < fmin) fmin = f; if (f > fmax) fmax = f; }
			}
	}
	return { frames: frames_, chans: voiced / frames_, keyons: keyons / (frames_ / FRAME_HZ), tl: tlSum / Math.max(1, tlN), fmin, fmax };
}

const ay = pak(path.join(dir, 'MUSICAY.PAK'));
const fm = pak(path.join(dir, 'MUSICFM.PAK'));
console.log(`${game}: ${ay.ents.length} треков`);
console.log('id    сек   AY: каналов  низких  Гц           FM: каналов  key-on/с  TL     Гц');
let ayC = 0, fmC = 0, n = 0;
for (let i = 0; i < ay.ents.length; i++) {
	const a = scanAy(ay.buf, ay.ents[i]), f = scanFm(fm.buf, fm.ents[i]);
	ayC += a.chans; fmC += f.chans; n++;
	console.log(
		`${ay.ents[i].id.toString(16)} ${(a.frames / FRAME_HZ).toFixed(0).padStart(5)}` +
		`   ${a.chans.toFixed(2)}  ${(a.low * 100).toFixed(0).padStart(3)}%  ${a.fmin.toFixed(0)}-${a.fmax.toFixed(0)}`.padEnd(30) +
		`   ${f.chans.toFixed(2)}     ${f.keyons.toFixed(1).padStart(5)}  ${f.tl.toFixed(0).padStart(3)}  ${f.fmin.toFixed(0)}-${f.fmax.toFixed(0)}`);
}
console.log(`среднее: AY каналов ${(ayC / n).toFixed(2)} из 3, FM каналов ${(fmC / n).toFixed(2)} из 6`);
// Гистограмма громкости AY по всем трекам: видно, жмётся ли всё к 15
const vh = new Array(16).fill(0);
for (const e of ay.ents) { const a = scanAy(ay.buf, e); a.vhist.forEach((v, i) => vh[i] += v); }
const tot = vh.reduce((a, b) => a + b, 0);
console.log('громкость AY:', vh.map((v, i) => `${i}:${(v * 100 / tot).toFixed(0)}%`).join(' '));

// Разбор одного трека FM: какие тембры и ноты уходят в чип (поиск «хрипящей» ноты).
//   node tools/proto/chipscan.js TFTD 32d
if (process.argv[3]) {
	const id = parseInt(process.argv[3], 16);
	const e = fm.ents.find(x => x.id === id);
	const reg = [new Array(256).fill(0), new Array(256).fill(0)];
	const timbres = new Map(), blocks = new Array(8).fill(0);
	for (const w of frames(fm.buf, e.off, e.size)) {
		if (!w) continue;
		for (const [bank, r, v] of w) {
			if (r === 0x28) {
				const ch = v & 3;
				if (ch > 2 || !(v & 0xF0)) continue;
				const fb = reg[bank][0xB0 + ch] >> 3 & 7, alg = reg[bank][0xB0 + ch] & 7;
				const k = [fb, alg,
					reg[bank][0x30 + ch] & 15, reg[bank][0x38 + ch] & 15,      // MULT мод/нес
					reg[bank][0x40 + ch], reg[bank][0x48 + ch],                // TL мод/нес
					reg[bank][0x50 + ch], reg[bank][0x58 + ch],                // KS/AR
					reg[bank][0x80 + ch], reg[bank][0x88 + ch]].join(' ');     // SL/RR
				timbres.set(k, (timbres.get(k) || 0) + 1);
				blocks[reg[bank][0xA4 + ch] >> 3]++;
			} else reg[bank][r] = v;
		}
	}
	console.log(`\nтрек ${id.toString(16)}: тембры (fb alg multM multC tlM tlC ksarM ksarC slrrM slrrC -> нот)`);
	[...timbres].sort((a, b) => b[1] - a[1]).forEach(([k, n]) => console.log(`  ${k}  -> ${n}`));
	console.log('блоки (октавы):', blocks.map((n, i) => `${i}:${n}`).join(' '));
}

// Ретриги FM и прыжки периода AY: короткий интервал между key-on на одном канале звучит
// как рычание, октавный скачок периода — как «выбивается из сетки».
if (process.argv[3]) {
	const id = parseInt(process.argv[3], 16);
	const ef = fm.ents.find(x => x.id === id), ea = ay.ents.find(x => x.id === id);
	const lastOn = [[-9, -9, -9], [-9, -9, -9]], gaps = new Map();
	let t = 0;
	for (const w of frames(fm.buf, ef.off, ef.size)) {
		t++;
		if (!w) continue;
		for (const [bank, r, v] of w)
			if (r === 0x28 && (v & 0xF0) && (v & 3) < 3) {
				const g = t - lastOn[bank][v & 3];
				lastOn[bank][v & 3] = t;
				const k = g > 8 ? '9+' : String(g);
				gaps.set(k, (gaps.get(k) || 0) + 1);
			}
	}
	console.log('FM: интервалы между key-on на канале (кадров):',
		[...gaps].sort((a, b) => (a[0] === '9+' ? 99 : +a[0]) - (b[0] === '9+' ? 99 : +b[0])).map(([k, n]) => `${k}:${n}`).join(' '));

	const reg = new Array(16).fill(0), prev = [0, 0, 0];
	let jumps = 0, oct = 0, holds = 0;
	for (const w of frames(ay.buf, ea.off, ea.size)) {
		if (w) for (const [, r, v] of w) if (r < 16) reg[r] = v;
		for (let c = 0; c < 3; c++) {
			if (!(reg[8 + c] & 15) || (reg[7] >> c & 1)) continue;
			const per = (reg[c * 2] | (reg[c * 2 + 1] & 0x0F) << 8) || 1;
			if (prev[c]) {
				const ratio = per / prev[c];
				if (Math.abs(ratio - 1) > 0.02) jumps++;
				if (Math.abs(ratio - 2) < 0.06 || Math.abs(ratio - 0.5) < 0.03) oct++;
				else holds++;
			}
			prev[c] = per;
		}
	}
	console.log(`AY: смен периода ${jumps}, из них октавных скачков ${oct} (остальных удержаний ${holds})`);
}
