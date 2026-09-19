// Проверка точки цикла: порт плеера OxzConv/Core/Music.cs (он же adlplayer.cpp) в JS,
// прогон трека на ДВА прохода и сравнение второго прохода с первым.
// Отвечает на вопросы 22_music_data.md §2: совпадает ли тело цикла с первым проходом,
// сколько записей надо дописать при прыжке (ресинк), и как это меняет размер потока.
//
// Запуск: node tools/proto/music_loop.js > tmp/proto/music/loop.txt
'use strict';
const fs = require('fs');
const path = require('path');

const TickHz = 44100 / 629, FrameHz = 3500000 / 71680;
const GAMES = [
  ['UFO', 'Steam/XCom UFO Defense/XCOM/SOUND/ADLIB.CAT', 'REF/OpenXcom/bin/standard/xcom1/music.rul'],
  ['TFTD', 'Steam/X-COM Terror from the Deep/TFD/SOUND/ADLIB.CAT', 'REF/OpenXcom/bin/standard/xcom2/music.rul'],
];

const Freq1 = [0x0B5, 0x0C0, 0x0CC, 0x0D8, 0x0E5, 0x0F2, 0x101, 0x110, 0x120, 0x131, 0x143, 0x157];
const Freq2 = [0x16B, 0x181, 0x198, 0x1B0, 0x1CA, 0x1E5, 0x202, 0x220, 0x241, 0x263, 0x287, 0x2AE];
const FreqTable = Freq1.concat(...Array(8).fill(Freq2));
const OctaveTable = Array(24).fill(0).concat(...[1, 2, 3, 4, 5, 6, 7].map(o => Array(12).fill(o)));
const DetuneTable = [].concat(...Array(9).fill([3, 3, 3, 3, 4, 4, 4, 4, 4, 5, 5, 5]));
const InstrOrder = [0, 1, 2, 3, 4, 5, 6, 7, 8, 10, 11, 12, 13, 14, 15, 9];
const Ops1 = [0, 1, 2, 8, 9, 10, 16, 17, 18, 24, 25, 26];

function catEntries(buf) {
  const n = buf.readInt32LE(0) / 8, o = [];
  for (let i = 0; i < n; i++) o.push([buf.readInt32LE(i * 8), buf.readInt32LE(i * 8 + 4)]);
  return o;
}
function catTrack(buf, e) {
  let size = e[1];
  if (buf[e[0]] <= 56) size += buf[e[0]] + 1;
  size = Math.min(size, buf.length - e[0]);
  return buf.slice(e[0], e[0] + size);
}

class Player {
  constructor(d, reg, vol) {
    this.d = d; this.regFn = reg;
    this.ch = Array.from({ length: 12 }, () => ({ Note: 0, Instr: 0, Sample: 0xFF, Freq: 0, HiFreq: 0, Volume: 0, Duration: 0 }));
    this.ins = Array.from({ length: 16 }, () => ({ Sample: 0, Prev: 0, Volume: 0, Pitch: 0, Delay: 0, Addr: -1, Start: -1, Ret: -1 }));
    this.chorus = new Array(16).fill(0);
    this.subtracks = [];
    this.vol = vol === undefined ? 127 : vol;
    this.tempo = 120; this.tempoRun = 60; this.tempoInc = 70; this.samples = 0;
    this.Playing = false; this.Looped = false;
  }
  D(p) { return p >= 0 && p < this.d.length ? this.d[p] : 0; }
  U16(p) { return this.D(p) | this.D(p + 1) << 8; }
  reg(r, v) { this.regFn(r, v); }
  numSeq(I) { let v = 0, c; do { c = this.D(I.Addr++); v = (v << 7) + (c & 0x7F); } while (c & 0x80); return v; }
  setAmp(c, value) { this.reg(0x43 + Ops1[c], ~(value >> 1) & 0x3F); }
  clearChannels() { for (const c of this.ch) { c.Sample = 0xFF; c.Note = 0; } }
  resetChannels() { this.clearChannels(); for (let i = 0; i < 12; i++) { this.reg(0xB0 + i, 0); this.setAmp(i, 0); } }
  at(t, i, def) { return i >= 0 && i < t.length ? t[i] : def; }
  pitchedFreq(note, instr) {
    const p = this.ins[instr].Pitch, bas = this.at(FreqTable, note, 0);
    if (p === 0) return bas;
    if (p > 0) return bas + this.at(DetuneTable, note, 3) * p;
    return bas + (note > 0 ? this.at(DetuneTable, note - 1, 3) : 7) * p;
  }
  setPitch(instr, pitch) {
    this.ins[instr].Pitch = pitch;
    for (let i = 0; i < 12; i++) {
      const c = this.ch[i];
      if (c.Note !== 0 && c.Instr === instr) {
        const f = this.pitchedFreq(c.Note, instr);
        c.Freq = f & 0xFF; this.reg(0xA0 + i, f & 0xFF);
        const hf = ((f >> 8) & 3 | this.at(OctaveTable, c.Note, 0) << 2) & 0xFF;
        c.HiFreq = hf; this.reg(0xB0 + i, hf | 0x20);
      }
    }
  }
  unusedChannel(sample) {
    let maxchan = 0, maxdur = 0;
    for (const c of this.ch) c.Duration++;
    for (let i = 0; i < 12; i++) {
      if (this.ch[i].Duration > maxdur) { maxdur = this.ch[i].Duration; maxchan = i; }
      if (this.ch[i].Note === 0) { maxchan = i; break; }
    }
    let same = false;
    if (this.ch[maxchan].Sample === sample) same = true; else this.ch[maxchan].Sample = sample;
    this.ch[maxchan].Duration = 0;
    return [maxchan, same];
  }
  playNote(note, volume, instr) {
    const sample = this.ins[instr].Sample, s = this.samples + sample * 24;
    note = (note - 1) & 0xFF;
    if (volume === 0) {
      for (let i = 0; i < 12; i++)
        if (this.ch[i].Note === note && this.ch[i].Instr === instr) { this.ch[i].Note = 0; this.reg(0xB0 + i, this.ch[i].HiFreq); }
      return;
    }
    if (volume > 127) volume = 127;
    const [c, same] = this.unusedChannel(sample), cc = this.ch[c], op1 = Ops1[c];
    cc.Volume = volume; cc.Note = note; cc.Instr = instr;
    if (!same) {
      this.reg(0x20 + op1, this.D(s)); this.reg(0x23 + op1, this.D(s + 1));
      const ampl = this.D(s + 2);
      this.reg(0x40 + op1, (~ampl & 0x3F) | (ampl & 0xC0));
    }
    this.reg(0xB0 + c, cc.HiFreq);
    this.reg(0x43 + op1, ~((this.vol * volume) >> 8) & 0x3F);
    if (!same) {
      this.reg(0x60 + op1, this.D(s + 4)); this.reg(0x63 + op1, this.D(s + 5));
      this.reg(0x80 + op1, this.D(s + 6)); this.reg(0x83 + op1, this.D(s + 7));
      this.reg(0xE0 + op1, this.D(s + 8)); this.reg(0xE3 + op1, this.D(s + 9));
      this.reg(0xC0 + c, this.D(s + 10) ^ 0x01);
    }
    const f = this.pitchedFreq(note, instr);
    cc.Freq = f & 0xFF; this.reg(0xA0 + c, f & 0xFF);
    const hf = (f >> 8 | this.at(OctaveTable, note, 0) << 2) & 0xFF;
    cc.HiFreq = hf; this.reg(0xB0 + c, hf | 0x20);
  }
  freeChannel() { return this.ch.some(c => c.Note === 0); }
  noteOff(note, i) { this.playNote(note, 0, i); if (this.chorus[i] !== 0 && this.chorus[i] < 16) this.playNote(note, 0, this.chorus[i]); }
  decode(i, st) {
    const I = this.ins[i];
    let delay = 0;
    do {
      let op = this.D(I.Addr++);
      if (op === 0xFE) { const a = this.D(I.Addr++); I.Ret = I.Addr; I.Addr = a < this.subtracks.length ? this.subtracks[a] : this.d.length; }
      else if (op === 0xFD) { if (I.Ret >= 0) { I.Addr = I.Ret; I.Ret = -1; } }
      else if (op === 0xFF) { this.Playing = false; delay = 0; break; }
      else if (op >= 0x80) { I.Prev = op; op = this.D(I.Addr++); }
      if (op < 0x80) {
        const a1 = op;
        switch (I.Prev & 0xF0) {
          case 0x80: I.Addr++; this.noteOff(a1, i); break;
          case 0x90: {
            const a2 = this.D(I.Addr++);
            if (a2 === 0) this.noteOff(a1, i);
            else {
              const v = (a2 * I.Volume) >> 7, ci = this.chorus[i];
              if (ci !== 0 && ci < 16 && this.freeChannel()) {
                this.ins[ci].Sample = I.Sample; this.ins[ci].Pitch = I.Pitch - 1;
                this.playNote(a1, v, ci);
              }
              this.playNote(a1, v, i);
            }
            break;
          }
          case 0xB0: {
            const a2 = this.D(I.Addr++);
            if (a1 === 0 && a2 !== 0) this.tempo = Math.trunc(a2 * 0.8);
            else if (a1 === 7) I.Volume = a2;
            else if (a1 === 0x7E) this.chorus[i] = (a2 - 1) & 0xFF;
            else if (a1 === 0x7F) this.chorus[i] = 0;
            break;
          }
          case 0xC0: if (a1 === 0x7E) st.loop = true; else I.Sample = a1; break;
          case 0xE0:
            I.Pitch = a1 - 16; this.setPitch(i, a1 - 16);
            if (this.chorus[i] !== 0 && this.chorus[i] < 16) this.setPitch(this.chorus[i], a1 - 17);
            break;
        }
      }
      if (!this.Playing) break;
      let vv = 0, c;
      do { c = this.D(I.Addr++); vv = (vv << 7) + (c & 0x7F); } while (c & 0x80);
      delay = vv;
    } while (delay === 0);
    return delay;
  }
  initData() {
    const len = this.d.length; let p = 0;
    for (const I of this.ins) I.Start = -1;
    const fmt = this.d[0] > 56 ? 0 : 1;
    if (fmt === 1) p += this.d[0] + 1;
    this.tempo = this.D(p++);
    this.samples = p + 1;
    p += this.D(p) * 24 + 1;
    const nsub = this.D(p++);
    for (let i = 0; i < nsub; i++) { const add = this.U16(p); this.subtracks.push(p + 4); p += add; }
    const nins = this.D(p++);
    for (let i = 0; i < nins; i++) {
      const add = this.U16(p);
      if (fmt === 1) { let j = this.D(p + 4); if (j > 15) j = 15; this.ins[j].Start = p + 5; }
      else if (i < 16) this.ins[i].Start = p + 4;
      p += add;
      if (p >= len) break;
    }
  }
  initMusic() {
    for (let i = 0; i < 16; i++) {
      const I = this.ins[i];
      I.Pitch = 0; this.chorus[i] = 0;
      if (I.Start >= 0) { I.Addr = I.Start; I.Delay = this.numSeq(I); }
      else { I.Addr = -1; I.Delay = 0; }
    }
  }
  adlibInit() {
    for (let i = 1; i < 0xF5; i++) this.reg(i, 0);
    this.reg(0x04, 0x60); this.reg(0x04, 0x80); this.reg(0x01, 0x20);
    this.reg(0xA8, 0x01); this.reg(0x08, 0x40); this.reg(0xBD, 0xC0);
  }
  setup() {
    this.Playing = false;
    this.initData(); this.initMusic(); this.adlibInit(); this.resetChannels();
    this.tempo = Math.trunc(this.tempo * 0.4);
    this.tempoRun = this.tempo;
    this.Playing = true;
    for (let i = 0; i < 12; i++) this.setAmp(i, (this.ch[i].Volume * this.vol) >> 7);
  }
  tick() {
    if (!this.Playing) return;
    this.tempoRun -= this.tempo;
    if (this.tempoRun > 0) return;
    this.tempoRun += this.tempoInc;
    const st = { loop: false };
    do {
      st.loop = false;
      for (let k = 0; k < 16; k++) {
        const i = InstrOrder[k], I = this.ins[i];
        if (I.Addr < 0) continue;
        if (I.Delay === 0) { I.Delay = this.decode(i, st); if (!this.Playing) break; }
        I.Delay--;
      }
      if (!st.loop && this.Playing) break;
      this.Looped = true;
      this.initMusic(); this.clearChannels();
    } while (st.loop);
  }
}

function mapReg(r) {
  if (r === 0xBD || r < 0x20) return [0, r];
  const hi = r & 0xF0;
  if (hi === 0xA0 || hi === 0xB0 || hi === 0xC0) {
    const c = r & 0x0F;
    if (c < 9) return [0, r];
    if (c < 12) return [1, hi | (c - 9)];
    return null;
  }
  if (r >= 0x20 && r < 0xA0 || r >= 0xE0 && r < 0x100) {
    const bas = r & 0xE0, off = r & 0x1F;
    if (off <= 0x15) return [0, r];
    if (off >= 0x18 && off <= 0x1D) return [1, bas | (off - 0x18)];
    return null;
  }
  return null;
}

// Сырые (без отсева повторов) записи по кадрам; останавливаемся после N-го повтора.
function renderRaw(data, passes = 3, maxSeconds = 1200, vol = 127) {
  const frames = [];                     // [ [ [bank,reg,val], ... ], ... ]
  const loopFrames = [];                 // кадр, на котором произошёл повтор
  let cur = [];
  const p = new Player(data, (rg, v) => {
    const m = mapReg(rg & 0xFF);
    if (m) cur.push([m[0], m[1], v & 0xFF]);
  }, vol);
  frames.push(cur);
  cur.push([1, 0x05, 0x01], [1, 0x04, 0x00]);
  p.setup();
  const maxTicks = Math.floor(maxSeconds * TickHz);
  for (let k = 1; k <= maxTicks && p.Playing; k++) {
    const f = Math.floor(k * FrameHz / TickHz);
    while (frames.length <= f) frames.push([]);
    cur = frames[f];
    p.Looped = false;
    p.tick();
    if (p.Looped) { loopFrames.push(f); if (loopFrames.length >= passes - 1) break; }
  }
  return { frames, loopFrames, playing: p.Playing };
}

// Дельта-кодирование куска кадров при заданном начальном состоянии регистров.
function deltaEncode(frames, from, to, state) {
  const st = [new Map(state[0]), new Map(state[1])];
  const out = [];
  for (let i = from; i < to; i++) {
    const w = [];
    for (const [b, r, v0] of frames[i]) {
      let v = v0;
      if (b === 0 && (r & 0xF0) === 0xC0 && (r & 0x0F) < 9 || b === 1 && (r & 0xF0) === 0xC0) v |= 0x30;
      if (st[b].get(r) === v) continue;
      st[b].set(r, v);
      w.push([b, r, v]);
    }
    out.push(w);
  }
  return { frames: out, state: st };
}
const streamBytes = fr => fr.reduce((a, w) => a + (w.length ? 2 + w.length * 2 : 0), 0) +
  fr.filter((w, i) => !w.length && (i === 0 || fr[i - 1].length)).length;   // грубо: 1 байт на серию пауз

console.log('Плеер портирован из OxzConv/Core/Music.cs; прогон 2 проходов каждого трека.\n');
console.log('игра трек       кадр1  кадрL1  кадрL2  длина1  длина2  расхождение_проход2                ресинк_рег  дельта1_Б  дельта2_Б');
const rows = [];
for (const [game, cat, rul] of GAMES) {
  const buf = fs.readFileSync(cat);
  const ents = catEntries(buf);
  const rules = [];
  for (const raw of fs.readFileSync(rul, 'utf8').split(/\r?\n/)) {
    const line = raw.replace(/#.*$/, '');
    let m;
    if ((m = line.match(/^\s*-\s*type:\s*(\S+)/))) rules.push({ type: m[1], catPos: null });
    else if (rules.length && (m = line.match(/^\s*catPos:\s*(-?\d+)/))) rules[rules.length - 1].catPos = +m[1];
  }
  const seen = new Set();
  for (const t of rules) {
    if (t.catPos === null || t.catPos >= ents.length || seen.has(t.catPos)) continue;
    seen.add(t.catPos);
    const data = catTrack(buf, ents[t.catPos]);
    const r = renderRaw(data, 3);
    const L1 = r.loopFrames[0], L2 = r.loopFrames[1];
    if (L1 === undefined) { console.log(`${game} ${t.type}: цикла нет (конец трека)`); continue; }
    const len1 = L1 - 1, len2 = L2 === undefined ? -1 : L2 - L1;
    // сравнение сырых записей: кадры 1..L1-1 против L1..L2-1
    let same = 'n/a', firstDiff = -1, diffFrames = 0, cmp = 0;
    if (L2 !== undefined) {
      const n = Math.min(len1, len2);
      for (let i = 0; i < n; i++) {
        const a = r.frames[1 + i], b = r.frames[L1 + i];
        cmp++;
        if (a.length !== b.length || a.some((x, j) => x[0] !== b[j][0] || x[1] !== b[j][1] || x[2] !== b[j][2])) { diffFrames++; if (firstDiff < 0) firstDiff = i; }
      }
      same = diffFrames === 0 ? (len1 === len2 ? 'да' : 'да(±1 кадр)') : `${diffFrames}/${cmp}`;
    } else {
      same = r.playing ? 'обрыв по лимиту' : `КОНЕЦ на кадре ${r.frames.length - 1} (проход2 = ${r.frames.length - 1 - L1})`;
    }
    // дельта-кодирование первого прохода и второго (с продолжением состояния)
    const e0 = deltaEncode(r.frames, 0, 1, [new Map(), new Map()]);
    const d1 = deltaEncode(r.frames, 1, L1, e0.state);
    const d2 = L2 !== undefined ? deltaEncode(r.frames, L1, L2, d1.state) : null;
    // ресинк: регистры, отличающиеся между состоянием после кадра 0 и состоянием в точке повтора
    let resync = 0;
    for (const b of [0, 1]) {
      const keys = new Set([...e0.state[b].keys(), ...d1.state[b].keys()]);
      for (const k of keys) if (e0.state[b].get(k) !== d1.state[b].get(k)) resync++;
    }
    const b1 = streamBytes(d1.frames), b2 = d2 ? streamBytes(d2.frames) : -1;
    console.log(`${game.padEnd(5)}${t.type.padEnd(10)}${String(r.frames[0].length).padStart(7)}${String(L1).padStart(8)}${String(L2).padStart(8)}` +
      `${String(len1).padStart(8)}${String(len2).padStart(8)}${String(same).padStart(34)}${String(resync).padStart(12)}${String(b1).padStart(11)}${String(b2).padStart(11)}` +
      (firstDiff >= 0 ? `  первое расхождение в кадре +${firstDiff}` : ''));
    rows.push({ game, type: t.type, resync, b1, b2, same });
  }
}
const tot = rows.reduce((a, r) => ({ b1: a.b1 + r.b1, b2: a.b2 + (r.b2 > 0 ? r.b2 : 0), resync: a.resync + r.resync }), { b1: 0, b2: 0, resync: 0 });
console.log(`\nИТОГО: первый проход ${tot.b1} Б, второй ${tot.b2} Б (${(100 * tot.b2 / tot.b1).toFixed(1)}% от первого), ` +
  `ресинк суммарно ${tot.resync} регистров = ${tot.resync * 2} Б`);
console.log('Совпало «да» у ' + rows.filter(r => r.same === 'да').length + ' из ' + rows.length + ' треков.');
