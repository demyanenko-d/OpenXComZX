// Статистика покадрового потока MUSIC.PAK (бюджет плеера, project_docs/20_music_player.md).
// Запуск: node tools/proto/music_budget.js [tmp/sd] > tmp/proto/music/budget.txt
//
// Формат PAK — 09 §5; запись MUSIC: u32 loop_off, u32 frames, u16 max_writes, u16 0,
// далее поток: #00..#7F — n0 пар (рег,знач) банка 0, байт n1 и n1 пар банка 1, конец кадра;
// #81..#FE — пауза (b-#80) кадров; #FF — конец.
'use strict';
const fs = require('fs');
const path = require('path');

const root = process.argv[2] || 'tmp/sd';
const FRAME_HZ = 3500000 / 71680;

function readPak(file) {
  const b = fs.readFileSync(file);
  if (b.toString('ascii', 0, 4) !== 'OXZP') throw new Error('не OXZP: ' + file);
  const n = b.readUInt16LE(6);
  const out = [];
  for (let i = 0; i < n; i++) {
    const o = 16 + i * 16;
    out.push({
      id: b.readUInt16LE(o), type: b[o + 2], flags: b[o + 3],
      off: b.readUInt16LE(o + 4) * 512, size: b.readUInt32LE(o + 6),
      a: b.readUInt16LE(o + 10), c: b.readUInt16LE(o + 14),
    });
  }
  return { buf: b, ents: out };
}

// Разбор потока одного трека -> массив «записей на кадр» (длина = число кадров).
function decode(buf, base, size) {
  const loop = buf.readUInt32LE(base);
  const frames = buf.readUInt32LE(base + 4);
  const maxw = buf.readUInt16LE(base + 8);
  let p = base + 12;
  const end = base + size;
  const per = [];          // записей (пар рег/знач) в кадре
  const bytes = [];        // байт потока, прочитанных в кадре
  let chunks = 0, maxChunkFrames = 0;
  while (p < end) {
    const b0 = buf[p];
    if (b0 === 0xFF) break;
    if (b0 >= 0x81) { const k = b0 - 0x80; p++; for (let i = 0; i < k; i++) { per.push(0); bytes.push(i === 0 ? 1 : 0); } continue; }
    if (b0 === 0x80) throw new Error('#80 в потоке');
    const start = p;
    const n0 = b0; p += 1 + n0 * 2;
    const n1 = buf[p]; p += 1 + n1 * 2;
    per.push(n0 + n1);
    bytes.push(p - start);
    chunks++;
    if (n0 === 127 || n1 === 255) maxChunkFrames++;
  }
  return { loop, framesHdr: frames, maxw, per, bytes, chunks, split: maxChunkFrames, bytesTotal: size };
}

function pct(arr, q) { const s = arr.slice().sort((a, b) => a - b); return s[Math.min(s.length - 1, Math.floor(q * s.length))]; }

const games = fs.readdirSync(path.join(root, 'OXZ'));
let grand = { tracks: 0, frames: 0, writes: 0, bytes: 0, max: 0 };
const allPer = [];
for (const g of games) {
  const f = path.join(root, 'OXZ', g, 'MUSIC.PAK');
  if (!fs.existsSync(f)) continue;
  const { buf, ents } = readPak(f);
  const mus = ents.filter(e => e.type === 7);
  console.log(`\n=== ${g}: ${f} (${fs.statSync(f).size} байт), треков ${mus.length}`);
  console.log('  id     кадров   сек   байт   Б/кадр  max  p99  p999  сред  сред>0  сплит  loop');
  let gf = 0, gw = 0, gb = 0, gmax = 0;
  for (const e of mus) {
    const d = decode(buf, e.off, e.size);
    const sum = d.per.reduce((a, b) => a + b, 0);
    const nz = d.per.filter(x => x > 0);
    const mx = Math.max(...d.per);
    gf += d.per.length; gw += sum; gb += e.size; gmax = Math.max(gmax, mx);
    allPer.push(...d.per);
    console.log(
      `  ${e.id.toString(16).padStart(4, '0')} ${String(d.per.length).padStart(8)} ${(d.per.length / FRAME_HZ).toFixed(1).padStart(6)} ` +
      `${String(e.size).padStart(7)} ${(e.size / d.per.length).toFixed(2).padStart(7)} ` +
      `${String(mx).padStart(4)} ${String(pct(d.per, 0.99)).padStart(4)} ${String(pct(d.per, 0.999)).padStart(5)} ` +
      `${(sum / d.per.length).toFixed(2).padStart(6)} ${(nz.length ? sum / nz.length : 0).toFixed(1).padStart(7)} ` +
      `${String(d.split).padStart(6)} ${d.loop === 0xFFFFFFFF ? '   нет' : String(d.loop).padStart(6)}`);
  }
  console.log(`  итого: кадров ${gf} (${(gf / FRAME_HZ / 60).toFixed(1)} мин), записей ${gw}, ` +
    `среднее ${(gw / gf).toFixed(2)}/кадр, максимум ${gmax}, поток ${(gb / gf).toFixed(2)} Б/кадр ` +
    `(${(gb / gf * FRAME_HZ / 1024).toFixed(2)} КБ/с)`);
  grand.tracks += mus.length; grand.frames += gf; grand.writes += gw; grand.bytes += gb; grand.max = Math.max(grand.max, gmax);
}

// Гистограмма записей на кадр по обеим играм
const hist = new Map();
for (const v of allPer) hist.set(v, (hist.get(v) || 0) + 1);
const keys = [...hist.keys()].sort((a, b) => a - b);
console.log('\n=== Распределение записей на кадр (обе игры)');
let acc = 0;
const buckets = [0, 1, 2, 3, 4, 6, 8, 12, 16, 24, 32, 48, 64, 96, 128, 256, 1024];
for (let i = 0; i < buckets.length; i++) {
  const lo = buckets[i], hi = i + 1 < buckets.length ? buckets[i + 1] : Infinity;
  let c = 0; for (const k of keys) if (k >= lo && k < hi) c += hist.get(k);
  if (!c) continue;
  acc += c;
  console.log(`  ${String(lo).padStart(4)}..${(hi === Infinity ? '' : hi - 1).toString().padStart(4)}: ${String(c).padStart(8)} кадров  ${(c / allPer.length * 100).toFixed(3)}%  накоп ${(acc / allPer.length * 100).toFixed(3)}%`);
}
console.log(`\n=== Всего: треков ${grand.tracks}, кадров ${grand.frames} (${(grand.frames / FRAME_HZ / 60).toFixed(1)} мин), ` +
  `записей ${grand.writes}, среднее ${(grand.writes / grand.frames).toFixed(3)}/кадр, максимум ${grand.max}`);
console.log(`Поток: ${grand.bytes} байт, ${(grand.bytes / grand.frames).toFixed(2)} Б/кадр = ${(grand.bytes / grand.frames * FRAME_HZ / 1024).toFixed(2)} КБ/с`);

// Худшее окно: сколько записей приходится на N подряд идущих кадров
console.log('\n=== Худшее скользящее окно (записей суммарно / среднее на кадр)');
for (const w of [1, 2, 4, 8, 16, 32, 64, 128]) {
  let best = 0, s = 0;
  for (let i = 0; i < allPer.length; i++) { s += allPer[i]; if (i >= w) s -= allPer[i - w]; if (i >= w - 1 && s > best) best = s; }
  console.log(`  ${String(w).padStart(4)} кадров: ${String(best).padStart(6)} записей, ${(best / w).toFixed(2)} на кадр`);
}
