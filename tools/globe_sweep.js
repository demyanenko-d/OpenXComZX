// Плотный обход глобуса в эмуляторе: сценарий шагов клавишами (снимок и углы вида после каждого),
// прогон и сверка каждого снимка с моделью (tools/globe_emucmp.js). Худшие кадры — с картой
// расхождений в tmp/shots/globe_sweep/.
//   node tools\globe_sweep.js [шагов на зум] [зумы, напр. 0,1,3,5] [клавиша: RIGHT|DOWN|MIX]
'use strict';
const fs = require('fs'), cp = require('child_process'), path = require('path');
const steps = +(process.argv[2] || 24), zooms = (process.argv[3] || '0,1,2,3,4,5').split(',').map(Number);
const how = process.argv[4] || 'MIX';
const dir = 'tmp/shots/globe_sweep';
fs.mkdirSync(dir, { recursive: true });
for (const f of fs.readdirSync(dir)) fs.unlinkSync(path.join(dir, f));
const head = fs.readFileSync('tests/globe_cmp.oxs', 'utf8').split('\n');
const L = head.slice(0, head.indexOf('waitmark 32 3000') + 1).filter(s => !s.startsWith('#'));
let zoom = 0, n = 0;
const shots = [];
// VIEWS="lon,lat,zoom;..." (градусы) — заданные виды: pokew центра за шаг RIGHT (15°) до цели
if (process.env.VIEWS) {
	const vs = process.env.VIEWS.split(';').map(s => s.split(',').map(Number)).sort((a, b) => a[2] - b[2]);
	for (const [lo, la, z] of vs) {
		while (zoom < z) { L.push('key SS+K', 'waitmark 32 3000'); zoom++; }
		const l16 = (Math.round(lo * 65536 / 360) - 0x0AAB) & 0xFFFF, a16 = Math.round(la * 65536 / 360) & 0xFFFF;
		L.push(`pokew _ctx+6 ${l16}`, `pokew _ctx+8 ${a16}`, 'key RIGHT', 'waitmark 32 3000', `shot ${dir}/s${n}.png`, 'peek _ctx+6 4');
		shots.push({ n, zoom: z }); n++;
	}
	zooms.length = 0;
}
for (const z of zooms) {
	while (zoom < z) { L.push('key SS+K', 'waitmark 32 3000'); zoom++; }
	for (let i = 0; i < steps; i++) {
		const k = how === 'MIX' ? (i % 4 === 3 ? 'DOWN' : 'RIGHT') : how;
		L.push(`key ${k}`, 'waitmark 32 3000', `shot ${dir}/s${n}.png`, 'peek _ctx+6 4');
		shots.push({ n, zoom: z });
		n++;
	}
}
L.push('exit 0');
fs.writeFileSync('tmp/globe_sweep.oxs', L.join('\n') + '\n');
const out = cp.execFileSync('powershell', ['-File', 'tools\\run.ps1', '-Script', 'tmp\\globe_sweep.oxs', '-Headless'], { encoding: 'utf8', maxBuffer: 64 << 20 });
fs.writeFileSync('tmp/globe_sweep.log', out);
const peeks = [...out.matchAll(/_ctx\+6:\s*([0-9A-Fa-f]{2})\s+([0-9A-Fa-f]{2})\s+([0-9A-Fa-f]{2})\s+([0-9A-Fa-f]{2})/g)];
const warn = out.split('\n').filter(s => /pool full|too big|no pages|bad/.test(s));
if (warn.length) console.log('warnings:', [...new Set(warn)].join(' | '));
if (peeks.length !== shots.length) { console.log(`peeks ${peeks.length} != shots ${shots.length}; see tmp/globe_sweep.log`); }
const res = [];
for (let i = 0; i < Math.min(peeks.length, shots.length); i++) {
	const p = peeks[i], lon = parseInt(p[1], 16) | parseInt(p[2], 16) << 8, lat = parseInt(p[3], 16) | parseInt(p[4], 16) << 8;
	const s = shots[i], f = `${dir}/s${s.n}.png`;
	const r = cp.execFileSync('node', ['tools/globe_emucmp.js', f, String(lon), String(lat), String(s.zoom), `${dir}/d${s.n}.png`], { encoding: 'utf8' }).trim();
	const m = r.match(/gross (\d+); emu vs model (\d+)/);
	res.push({ i, zoom: s.zoom, lon, lat, gross: +m[1], mdiff: +m[2], line: r });
}
for (const z of [...new Set(shots.map(s => s.zoom))]) {
	const rz = res.filter(r => r.zoom === z);
	console.log(`zoom ${z}: max gross ${Math.max(...rz.map(r => r.gross))}, max emu-model ${Math.max(...rz.map(r => r.mdiff))}`);
}
if (process.env.VIEWS) for (const r of res) console.log(`s${r.i}: ${r.line}`);
else {
	res.sort((a, b) => b.mdiff - a.mdiff);
	for (const r of res.slice(0, 8)) console.log(`s${r.i}: ${r.line}`);
}
