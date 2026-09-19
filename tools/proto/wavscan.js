#!/usr/bin/env node
// Разбор записи звука эмулятора (tools/run.ps1 -Wav …): энергия, клиппинг, разрывы волны,
// усреднённый спектр и гармоники. Нужен, чтобы судить о звуке по числам: слышно «хрипит»,
// а причина может быть и в данных (огибающая, унисон), и в микшере эмулятора (переполнение).
//   node tools/proto/wavscan.js tmp/rec/ym.wav [tmp/rec/opl.wav ...]
'use strict';
const fs = require('fs');

function readWav(file) {
	const b = fs.readFileSync(file);
	const fq = b.readUInt32LE(24), ch = b.readUInt16LE(22);
	let p = 12, data = null;
	while (p + 8 <= b.length) {
		const id = b.toString('ascii', p, p + 4), n = b.readUInt32LE(p + 4);
		if (id === 'data') { data = b.slice(p + 8, p + 8 + n); break; }
		p += 8 + n;
	}
	const n = Math.floor(data.length / 2 / ch), s = new Float32Array(n);
	for (let i = 0; i < n; i++) {          // в моно
		let v = 0;
		for (let c = 0; c < ch; c++) v += data.readInt16LE((i * ch + c) * 2);
		s[i] = v / ch;
	}
	return { fq, s };
}

// FFT радикс-2 на месте (re, im)
function fft(re, im) {
	const n = re.length;
	for (let i = 1, j = 0; i < n; i++) {
		let bit = n >> 1;
		for (; j & bit; bit >>= 1) j ^= bit;
		j ^= bit;
		if (i < j) { [re[i], re[j]] = [re[j], re[i]]; [im[i], im[j]] = [im[j], im[i]]; }
	}
	for (let len = 2; len <= n; len <<= 1) {
		const ang = -2 * Math.PI / len;
		for (let i = 0; i < n; i += len)
			for (let k = 0; k < len / 2; k++) {
				const wr = Math.cos(ang * k), wi = Math.sin(ang * k);
				const ur = re[i + k], ui = im[i + k];
				const vr = re[i + k + len / 2] * wr - im[i + k + len / 2] * wi;
				const vi = re[i + k + len / 2] * wi + im[i + k + len / 2] * wr;
				re[i + k] = ur + vr; im[i + k] = ui + vi;
				re[i + k + len / 2] = ur - vr; im[i + k + len / 2] = ui - vi;
			}
	}
}

const N = 8192;

function spectrum(s, from, to) {
	const acc = new Float64Array(N / 2);
	let frames = 0;
	for (let p = from; p + N <= to; p += N / 2) {
		const re = new Float64Array(N), im = new Float64Array(N);
		for (let i = 0; i < N; i++) re[i] = s[p + i] * (0.5 - 0.5 * Math.cos(2 * Math.PI * i / (N - 1)));
		fft(re, im);
		for (let i = 0; i < N / 2; i++) acc[i] += Math.hypot(re[i], im[i]);
		frames++;
	}
	for (let i = 0; i < N / 2; i++) acc[i] /= Math.max(1, frames);
	return acc;
}

function scan(file) {
	const { fq, s } = readWav(file);
	let peak = 0, sum = 0, clip = 0, jumps = 0, maxJump = 0;
	for (let i = 0; i < s.length; i++) {
		const a = Math.abs(s[i]);
		if (a > peak) peak = a;
		sum += s[i] * s[i];
		if (a >= 32000) clip++;
		if (i) {
			const d = Math.abs(s[i] - s[i - 1]);
			if (d > maxJump) maxJump = d;
			if (d > 16000) jumps++;             // разрыв волны: половина шкалы за сэмпл
		}
	}
	const rms = Math.sqrt(sum / s.length);
	const db = v => (20 * Math.log10(Math.max(v, 1e-9) / 32768)).toFixed(1);
	console.log(`\n${file}: ${(s.length / fq).toFixed(1)} с, ${fq} Гц`);
	console.log(`  пик ${peak.toFixed(0)} (${db(peak)} dBFS), RMS ${rms.toFixed(0)} (${db(rms)} dBFS)`);
	console.log(`  у предела ${(clip * 100 / s.length).toFixed(3)} %, разрывов волны ${jumps} (наибольший скачок ${maxJump.toFixed(0)})`);

	// огибающая по 100 мс: видно атаки и затухания
	const step = Math.floor(fq / 10), env = [];
	for (let p = 0; p + step <= s.length; p += step) {
		let e = 0;
		for (let i = 0; i < step; i++) e += s[p + i] * s[p + i];
		env.push(Math.sqrt(e / step));
	}
	const bar = v => '#'.repeat(Math.max(0, Math.round(20 + 20 * Math.log10(Math.max(v, 1) / 32768) / 3)));
	console.log('  огибающая (100 мс, шаг 3 дБ):');
	for (let i = 0; i < env.length; i += 5)
		console.log(`    ${(i / 10).toFixed(1).padStart(5)} с ${bar(env[i])}`);

	// спектр: где энергия и какие гармоники
	const sp = spectrum(s, Math.floor(fq * 2), Math.min(s.length, Math.floor(fq * 10)));
	const peaks = [];
	for (let i = 2; i < sp.length - 1; i++)
		if (sp[i] > sp[i - 1] && sp[i] >= sp[i + 1]) peaks.push([i * fq / N, sp[i]]);
	peaks.sort((a, b) => b[1] - a[1]);
	const top = peaks.slice(0, 12);
	const mx = top.length ? top[0][1] : 1;
	console.log('  спектральные пики (Гц / дБ от наибольшего):');
	console.log('    ' + top.map(([f, v]) => `${f.toFixed(0)}:${(20 * Math.log10(v / mx)).toFixed(0)}`).join('  '));
	// энергия по октавным полосам — «тембр» записи целиком
	const bands = [0, 100, 200, 400, 800, 1600, 3200, 6400, 12800, 22050];
	const be = [];
	for (let k = 0; k + 1 < bands.length; k++) {
		let e = 0;
		for (let i = Math.ceil(bands[k] * N / fq); i < Math.min(sp.length, bands[k + 1] * N / fq); i++) e += sp[i] * sp[i];
		be.push(e);
	}
	const tot = be.reduce((a, b) => a + b, 0) || 1;
	console.log('  энергия по полосам: ' + be.map((e, k) => `${bands[k]}-${bands[k + 1]}:${(e * 100 / tot).toFixed(0)}%`).join(' '));
}

for (const f of process.argv.slice(2)) scan(f);

// Темп по кускам записи: автокорреляция огибающей. Если период бита растёт от начала к
// концу — музыка замедляется (жалоба «темп падает и падает»).
function tempo(file) {
	const { fq, s } = readWav(file);
	const hop = Math.floor(fq / 100);                  // огибающая с шагом 10 мс
	const env = [];
	for (let p = 0; p + hop <= s.length; p += hop) {
		let e = 0;
		for (let i = 0; i < hop; i++) e += s[p + i] * s[p + i];
		env.push(Math.sqrt(e / hop));
	}
	const part = Math.floor(env.length / 4);
	const out = [];
	for (let k = 0; k < 4; k++) {
		const a = env.slice(k * part, (k + 1) * part);
		const mean = a.reduce((x, y) => x + y, 0) / a.length;
		const d = a.map(v => v - mean);
		let best = 0, bestLag = 0;
		for (let lag = 20; lag < Math.min(200, a.length / 2); lag++) {   // 0.2 .. 2 с
			let c = 0;
			for (let i = 0; i + lag < d.length; i++) c += d[i] * d[i + lag];
			if (c > best) { best = c; bestLag = lag; }
		}
		out.push(`${(k * part / 100).toFixed(0)}-${((k + 1) * part / 100).toFixed(0)} с: ${(bestLag * 10)} мс`);
	}
	console.log(`  период повторов по четвертям: ${out.join(' | ')}`);
}

if (process.env.OXZ_TEMPO) for (const f of process.argv.slice(2)) tempo(f);
