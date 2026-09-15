// Проверка ассемблера SDCC 4.5 (#15242) на ошибку кодогенерации:
// сравнение на равенство байта в A с операндом в памяти компилируется в
// «sub a, (hl)» / «sub a, N (iy)» (A портится), а затем A используется так,
// будто там осталась переменная. Пример из input.c:
//     if (k == prev_keys) return 0;  prev_keys = k;   ->   sub a,(hl) / ld (_prev_keys),a
// Скрипт ищет «sub a, <память>» + «jr/jp Z|NZ» и смотрит первую инструкцию,
// касающуюся A, на обоих путях. Если она читает A — место подозрительное.
// Обход: скопировать глобальную в локальную перед сравнением (uint8_t p = g;).
//   node tools/checkasm.js tmp/build/*.asm
'use strict';
const fs = require('fs');

const SUB_MEM = /^\s+sub\s+a,\s*(\(hl\)|\(i[xy]\)|-?\d+\s*\(i[xy]\))/;
const JCOND = /^\s+j[rp]\s+(N?Z),\s*(\S+)/;
const KILLS = /^\s+(ld\s+a,|xor\s+a,\s*a\b|pop\s+af\b|ld\s+a\s*,)/;
const LABEL = /^(\S+):/;

function code(lines, i) {
	// следующая строка кода (без комментариев и пустых)
	for (; i < lines.length; i++) {
		const l = lines[i];
		if (/^\s*(;|$)/.test(l)) continue;
		return i;
	}
	return -1;
}

function touchesA(l) {
	const ins = l.replace(/;.*$/, '').trim();
	if (!ins) return null;
	if (KILLS.test(l)) return 'kill';
	if (/^call\b/.test(ins)) return 'stop';        // аргументы вызова неизвестны
	if (/^cp\s+a,\s*a$/.test(ins)) return null;   // сброс переноса (перед sbc hl): от A не зависит
	if (/^(ret|jp\s+\(hl\)|push\s+af|out\s|cpl|neg|rlca|rrca|rla|rra|daa)\b/.test(ins)) return 'read';
	if (/^(add|adc|sub|sbc|and|or|xor|cp)\s+a\b/.test(ins)) return 'read';
	if (/^(inc|dec|srl|sla|sra|rl|rr|rlc|rrc|bit|set|res)\s+(\d+,\s*)?a$/.test(ins)) return 'read';
	if (/^ld\s+[^,]+,\s*a$/.test(ins)) return 'read';
	if (/^(jr|jp)\s+[^,]+$/.test(ins)) return 'jump';
	return null;
}

function firstUse(lines, i, labels, depth) {
	for (let n = 0; i >= 0 && i < lines.length && n < 40; i++, n++) {
		const l = lines[i];
		if (/^\s*(;|$)/.test(l) || LABEL.test(l)) continue;
		const t = touchesA(l);
		if (t === 'jump') {
			const m = l.trim().match(/^(?:jr|jp)\s+(\S+)$/);
			if (m && labels.has(m[1]) && depth < 2) return firstUse(lines, labels.get(m[1]) + 1, labels, depth + 1);
			return null;
		}
		if (t === 'stop') return null;
		if (t) return { kind: t, line: i };
	}
	return null;
}

let bad = 0;
for (const f of process.argv.slice(2)) {
	const lines = fs.readFileSync(f, 'utf8').split(/\r?\n/);
	// локальные метки NNNNN$ повторяются в каждой функции: карта — на функцию
	let labels = new Map();
	const funcLabels = (from) => {
		const m = new Map();
		for (let i = from; i < lines.length; i++) {
			if (i > from && /^_\w+::?\s*$/.test(lines[i])) break;
			const l = lines[i].match(LABEL);
			if (l) m.set(l[1], i);
		}
		return m;
	};
	let src = '', retVoid = false;
	for (let i = 0; i < lines.length; i++) {
		// перед меткой функции SDCC пишет комментарий с её объявлением: у void-функции
		// «ret» A не читает (возврата нет)
		if (/^_\w+::?\s*$/.test(lines[i])) { labels = funcLabels(i); retVoid = /(^|[\s:])void\s+\w+\s*\(/.test(src); }
		if (/^;.*\.c:\d+:/.test(lines[i])) src = lines[i].slice(1).trim();
		if (!SUB_MEM.test(lines[i])) continue;
		const j = code(lines, i + 1);
		if (j < 0) continue;
		const m = lines[j].match(JCOND);
		if (!m) continue;
		for (const start of [j + 1, labels.has(m[2]) ? labels.get(m[2]) + 1 : -1]) {
			const u = firstUse(lines, start, labels, 0);
			if (u && u.kind === 'read' && !(retVoid && /^\s+ret\b/.test(lines[u.line]))) {
				console.log(`${f}:${i + 1}: SDCC sub-compare, A read at line ${u.line + 1} (${lines[u.line].trim()})\n    ${src}`);
				bad++;
				break;
			}
		}
	}
}
if (bad) { console.log(`checkasm: ${bad} suspicious place(s)`); process.exit(1); }
console.log('checkasm: OK');
