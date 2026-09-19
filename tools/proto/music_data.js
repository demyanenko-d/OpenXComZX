// Разбор данных музыки для project_docs/22_music_data.md:
// 1) группы треков (music.rul + правило подстроки Mod::getRandomMusic);
// 2) точка цикла: какие регистры OPL3 расходятся между концом потока и точкой повтора;
// 3) нормализация громкости: распределение уровней несущей (рег 0x43+op) и риск
//    переполнения оригинальной формулы ~((vol*volume)>>8)&0x3F при normalization > 1;
// 4) объём/битрейт и буфер стриминга (худшие окна в байтах);
// 5) опыты сжатия потока.
//
// Запуск: node tools/proto/music_data.js [tmp/sd] [REF/OpenXcom/bin/standard] > tmp/proto/music/data.txt
'use strict';
const fs = require('fs');
const path = require('path');
const zlib = require('zlib');

const ROOT = process.argv[2] || 'tmp/sd';
const RUL = process.argv[3] || 'REF/OpenXcom/bin/standard';
const FRAME_HZ = 3500000 / 71680;
const GAMES = { UFO: 'xcom1', TFTD: 'xcom2' };

// ---------------------------------------------------------------- music.rul
function parseMusicRul(file) {
  const out = [];
  for (const raw of fs.readFileSync(file, 'utf8').split(/\r?\n/)) {
    const line = raw.replace(/#.*$/, '').trimEnd();
    let m;
    if ((m = line.match(/^\s*-\s*type:\s*(\S+)/))) out.push({ type: m[1], catPos: null, name: null, norm: 0.76 });
    else if (out.length && (m = line.match(/^\s*catPos:\s*(-?\d+)/))) out[out.length - 1].catPos = +m[1];
    else if (out.length && (m = line.match(/^\s*name:\s*(\S+)/))) out[out.length - 1].name = m[1];
    else if (out.length && (m = line.match(/^\s*normalization:\s*([\d.]+)/))) out[out.length - 1].norm = +m[1];
  }
  return out;
}

// Все имена, с которыми оригинал зовёт playMusic: зашитые в код + из правил.
const HARDCODED = {
  xcom1: ['GMGEO', 'GMGEO1', 'GMINTER', 'GMLOSE', 'GMTACTIC', 'GMDEFEND', 'GMMARS', 'GMINTRO1', 'GMINTRO2', 'GMINTRO3'],
  xcom2: ['GMGEO', 'GMGEO1', 'GMINTER', 'GMLOSE', 'GMTACTIC', 'GMDEFEND'],
};
function rulRefs(dir) {
  const refs = new Map();      // имя -> [откуда]
  for (const f of fs.readdirSync(dir).filter(f => f.endsWith('.rul') && f !== 'music.rul')) {
    const txt = fs.readFileSync(path.join(dir, f), 'utf8').split(/\r?\n/);
    let inList = null;
    for (const raw of txt) {
      const line = raw.replace(/#.*$/, '');
      let m;
      if ((m = line.match(/^(\s*)(music|musicId|goodDebriefingMusic|badDebriefingMusic):\s*(\S+)?\s*$/))) {
        if (m[3]) { add(m[3], f); inList = null; }
        else inList = m[1].length;          // список на следующих строках
        continue;
      }
      if (inList !== null && (m = line.match(/^(\s*)-\s*(GM\S+)/)) && m[1].length > inList) { add(m[2], f); continue; }
      if (line.trim() && !/^\s*-?\s*$/.test(line)) inList = null;
    }
  }
  function add(n, f) { if (!refs.has(n)) refs.set(n, new Set()); refs.get(n).add(f); }
  return refs;
}

// ---------------------------------------------------------------- MUSIC.PAK
function readPak(file) {
  const b = fs.readFileSync(file);
  if (b.toString('ascii', 0, 4) !== 'OXZP') throw new Error('не OXZP: ' + file);
  const n = b.readUInt16LE(6);
  const ents = [];
  for (let i = 0; i < n; i++) {
    const o = 16 + i * 16;
    ents.push({
      id: b.readUInt16LE(o), type: b[o + 2], off: b.readUInt16LE(o + 4) * 512,
      size: b.readUInt32LE(o + 6), a: b.readUInt16LE(o + 10), bp: b.readUInt16LE(o + 12), c: b.readUInt16LE(o + 14),
    });
  }
  return { buf: b, ents };
}

// Поток -> кадры [{w:[[bank,reg,val]...], bytes, off}] + служебное.
function decode(buf, base, size) {
  const loop = buf.readUInt32LE(base), framesHdr = buf.readUInt32LE(base + 4), maxw = buf.readUInt16LE(base + 8);
  const end = base + size;
  let p = base + 12;
  const frames = [];
  let idleRuns = 0, idleFrames = 0, recs = 0, loopFrame = -1;
  while (p < end) {
    const b0 = buf[p];
    if (b0 === 0xFF) break;
    if (loop !== 0xFFFFFFFF && loopFrame < 0 && p - base - 12 >= loop) loopFrame = frames.length;
    if (b0 >= 0x81) {
      const k = b0 - 0x80; p++;
      idleRuns++; idleFrames += k;
      for (let i = 0; i < k; i++) frames.push({ w: [], bytes: i === 0 ? 1 : 0, off: p - base - 12 });
      continue;
    }
    const start = p, w = [];
    const n0 = buf[p++]; for (let i = 0; i < n0; i++) { w.push([0, buf[p], buf[p + 1]]); p += 2; }
    const n1 = buf[p++]; for (let i = 0; i < n1; i++) { w.push([1, buf[p], buf[p + 1]]); p += 2; }
    frames.push({ w, bytes: p - start, off: start - base - 12, n0, n1 });
    recs++;
  }
  return { loop, framesHdr, maxw, frames, idleRuns, idleFrames, recs, loopFrame, size };
}

// ---------------------------------------------------------------- отчёт
const typeList = fs.readFileSync('OxzConv/Data/music_types.txt', 'utf8').split(/\r?\n/)
  .map(s => s.replace(/#.*$/, '').trim()).filter(Boolean);
const typeName = new Map(typeList.map((t, i) => [0x300 + i, t]));

const CARRIER0 = new Set([0x43, 0x44, 0x45, 0x4B, 0x4C, 0x4D, 0x53, 0x54, 0x55]);
const CARRIER1 = new Set([0x43, 0x44, 0x45]);
const isCarrierTL = (bank, reg) => bank === 0 ? CARRIER0.has(reg) : CARRIER1.has(reg);

console.log('================ 1. Группы треков (правило Mod::getRandomMusic — подстрока)\n');
const rules = {};
for (const [game, dir] of Object.entries(GAMES)) {
  const list = parseMusicRul(path.join(RUL, dir, 'music.rul'));
  rules[game] = list;
  const loaded = list.filter(t => t.catPos !== null);
  console.log(`--- ${game} (${dir}/music.rul): типов ${list.length}, с catPos ${loaded.length}`);
  const refs = rulRefs(path.join(RUL, dir));
  for (const h of HARDCODED[dir]) if (!refs.has(h)) refs.set(h, new Set(['<код движка>']));
  const names = [...refs.keys()].sort();
  for (const n of names) {
    const grp = loaded.filter(t => t.type.includes(n)).map(t => t.type);
    const ghost = list.filter(t => t.catPos === null && t.type.includes(n)).map(t => t.type);
    console.log(`  ${n.padEnd(10)} -> ${String(grp.length).padStart(2)} трек(ов): ${grp.join(', ') || '(НЕТ)'}` +
      (ghost.length ? `   [без catPos: ${ghost.length}]` : '') + `   {${[...refs.get(n)].join(' ')}}`);
  }
  console.log('  типы с catPos и normalization:');
  for (const t of loaded) console.log(`    ${t.type.padEnd(10)} catPos=${String(t.catPos).padStart(2)} norm=${t.norm}${t.name ? ' name=' + t.name : ''}`);
  console.log();
}

console.log('\n================ 2-5. Потоки MUSIC.PAK\n');
const all = {};
for (const game of Object.keys(GAMES)) {
  const f = path.join(ROOT, 'OXZ', game, 'MUSIC.PAK');
  if (!fs.existsSync(f)) { console.log(`нет ${f}`); continue; }
  const { buf, ents } = readPak(f);
  const mus = ents.filter(e => e.type === 7);
  const tracks = [];
  for (const e of mus) {
    const d = decode(buf, e.off, e.size);
    d.id = e.id; d.name = typeName.get(e.id) || '?'; d.hdrFrames = e.a; d.hdrMax = e.bp; d.hdrLoop = e.c;
    d.data = buf.slice(e.off, e.off + e.size);
    tracks.push(d);
  }
  all[game] = tracks;
}

// --- 2. точка цикла: расхождение регистров
console.log('--- 2. Точка цикла: кадр повтора и рассинхрон регистров (что надо дописать при прыжке)\n');
console.log('Последний кадр потока — это уже первый кадр повтора (RenderTrack прерывается ПОСЛЕ Tick,');
console.log('в котором сработал повтор), поэтому он в расчёте не учитывается: столбец «в_посл_кадре».\n');
console.log('игра трек        кадров  loop_off  loopКадр  в_посл_кадре  регистров_разных  из_них_важных(A0/B0/C0/4x)  байт_ресинка');
const resyncStats = [];
for (const game of Object.keys(all)) {
  for (const t of all[game]) {
    const st = [new Map(), new Map()];
    let atLoop = null;
    const lastW = t.frames[t.frames.length - 1].w.length;
    for (let i = 0; i < t.frames.length - 1; i++) {
      if (i === t.loopFrame) atLoop = [new Map(st[0]), new Map(st[1])];
      for (const [b, r, v] of t.frames[i].w) st[b].set(r, v);
    }
    if (!atLoop) atLoop = [new Map(), new Map()];
    let diff = 0, hot = 0;
    const list = [];
    for (const b of [0, 1]) {
      const keys = new Set([...st[b].keys(), ...atLoop[b].keys()]);
      for (const r of keys) {
        const a = atLoop[b].has(r) ? atLoop[b].get(r) : null, z = st[b].has(r) ? st[b].get(r) : null;
        if (a !== z) { diff++; list.push([b, r, a, z]); if ((r & 0xF0) >= 0xA0 && (r & 0xF0) <= 0xC0 || (r & 0xE0) === 0x40) hot++; }
      }
    }
    resyncStats.push({ game, name: t.name, diff, hot, bytes: diff * 2 + 2 });
    console.log(`${game.padEnd(5)}${t.name.padEnd(10)}${String(t.frames.length).padStart(8)}${String(t.loop).padStart(10)}` +
      `${String(t.loopFrame).padStart(10)}${String(lastW).padStart(14)}${String(diff).padStart(18)}${String(hot).padStart(28)}${String(diff * 2 + 2).padStart(14)}`);
  }
}

// --- 3. нормализация
console.log('\n--- 3. Нормализация: уровни несущей в потоке (vol=127) и что даст normalization\n');
console.log('Записи в 0x43+op (аттенюация a = ~level & 0x3F, level = (127*volume)>>8, 0<=level<=63).');
console.log('При normalization k уровень станет ~level*k; level > 63 в оригинале ПЕРЕПОЛНЯЕТСЯ (~level&0x3F).\n');
console.log('игра трек        записей_TL  a=0..9(level>=54)  a=0..15  средн_level  норм   level*норм>63');
for (const game of Object.keys(all)) {
  const norms = new Map(rules[game].map(t => [t.type, t.norm]));
  for (const t of all[game]) {
    let n = 0, sum = 0, le9 = 0, le15 = 0, over = 0;
    const k = norms.has(t.name) ? norms.get(t.name) : 0.76;
    for (const fr of t.frames) for (const [b, r, v] of fr.w) {
      if (!isCarrierTL(b, r)) continue;
      const a = v & 0x3F, level = 63 - a;
      n++; sum += level;
      if (a <= 9) le9++;
      if (a <= 15) le15++;
      if (Math.floor(level * k) > 63) over++;
    }
    if (!n) continue;
    console.log(`${game.padEnd(5)}${t.name.padEnd(10)}${String(n).padStart(11)}${String(le9).padStart(19)}${String(le15).padStart(9)}` +
      `${(sum / n).toFixed(1).padStart(13)}${String(k).padStart(7)}${String(over).padStart(15)}`);
  }
}

// --- 4. объём и буфер
console.log('\n--- 4. Объём потоков и буфер стриминга\n');
console.log('(инициализация — кадр 0, играется один раз; далее «без init» = со второй секунды)\n');
console.log('игра трек        кадров     сек   байт  Б/кадр  init_Б  макс_запись_Б  макс_Б/с  макс_Б/64кадра  секторов/мин');
for (const game of Object.keys(all)) {
  let tot = 0;
  for (const t of all[game]) {
    const all_by = t.frames.map(f => f.bytes);
    const initB = all_by.slice(0, 3).reduce((a, b) => a + b, 0);
    const by = all_by.slice(3);
    const win = (w) => { let s = 0, best = 0; for (let i = 0; i < by.length; i++) { s += by[i]; if (i >= w) s -= by[i - w]; if (s > best) best = s; } return best; };
    const maxRec = Math.max(...by);
    console.log(`${game.padEnd(5)}${t.name.padEnd(10)}${String(t.frames.length).padStart(7)}${(t.frames.length / FRAME_HZ).toFixed(0).padStart(8)}` +
      `${String(t.size).padStart(8)}${(t.size / t.frames.length).toFixed(2).padStart(8)}${String(initB).padStart(8)}${String(maxRec).padStart(15)}` +
      `${String(win(49)).padStart(10)}${String(win(64)).padStart(16)}${(t.size / t.frames.length * FRAME_HZ * 60 / 512).toFixed(1).padStart(14)}`);
    tot += t.size;
  }
  console.log(`  ${game}: суммарно потоков ${tot} Б = ${(tot / 1024).toFixed(0)} КБ\n`);
}

// --- 5. сжатие
console.log('--- 5. Опыты сжатия потока\n');
console.log('игра трек        байт   deflate  пар_всего  уник_пар  уник_рег  кадров>0  без_банка1  экономия_n1  дельта_val  повтор_кадров');
let sum = { size: 0, defl: 0, n1: 0, pairs: 0 };
for (const game of Object.keys(all)) {
  for (const t of all[game]) {
    const defl = zlib.deflateRawSync(t.data.slice(12), { level: 9 }).length;
    const pairs = new Set(), regs = new Set();
    let np = 0, nz = 0, noB1 = 0;
    const fhash = new Map(); let rep = 0;
    for (const fr of t.frames) {
      if (!fr.w.length) continue;
      nz++;
      if (!fr.w.some(x => x[0] === 1)) noB1++;
      for (const [b, r, v] of fr.w) { np++; pairs.add((b << 16) | (r << 8) | v); regs.add((b << 8) | r); }
      const h = fr.w.map(x => x.join(',')).join(';');
      if (fhash.has(h)) rep++; else fhash.set(h, 1);
    }
    // дельта значений по регистру: сколько записей отличаются от предыдущего значения регистра на -8..7
    const last = new Map(); let small = 0;
    for (const fr of t.frames) for (const [b, r, v] of fr.w) {
      const k = (b << 8) | r;
      if (last.has(k)) { const d = v - last.get(k); if (d >= -8 && d <= 7) small++; }
      last.set(k, v);
    }
    console.log(`${game.padEnd(5)}${t.name.padEnd(10)}${String(t.size).padStart(7)}${String(defl).padStart(10)}${String(np).padStart(11)}` +
      `${String(pairs.size).padStart(10)}${String(regs.size).padStart(10)}${String(nz).padStart(10)}${String(noB1).padStart(12)}` +
      `${String(noB1).padStart(13)}${String(small).padStart(12)}${String(rep).padStart(15)}`);
    sum.size += t.size; sum.defl += defl; sum.n1 += noB1; sum.pairs += np;
  }
}
console.log(`\nИТОГО потоки ${sum.size} Б; deflate ${sum.defl} Б (${(100 * sum.defl / sum.size).toFixed(1)}%); ` +
  `пар ${sum.pairs}; кадров без банка 1 ${sum.n1} (экономия байта n1 = ${(100 * sum.n1 / sum.size).toFixed(1)}% потока)`);
