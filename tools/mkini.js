#!/usr/bin/env node
// Генератор ini эмулятора: копия шаблона с заменой ключей.
// node mkini.js <template.ini> <out.ini> SECTION.key=value ...
// Ключ заменяется в своей секции; если его нет — добавляется в конец секции.
'use strict';
const fs = require('fs');

const [tpl, out, ...sets] = process.argv.slice(2);
if (!tpl || !out) { console.error('usage: mkini.js template.ini out.ini SECTION.key=value ...'); process.exit(1); }

const lines = fs.readFileSync(tpl, 'latin1').split(/\r?\n/);
for (const s of sets) {
	const eq = s.indexOf('='), dot = s.indexOf('.');
	const section = s.slice(0, dot).toUpperCase(), key = s.slice(dot + 1, eq), value = s.slice(eq + 1);
	let cur = null, done = false, lastInSection = -1;
	for (let i = 0; i < lines.length; i++) {
		const sec = /^\s*\[([^\]]+)\]/.exec(lines[i]);
		if (sec) { cur = sec[1].toUpperCase(); continue; }
		if (cur !== section) continue;
		lastInSection = i;
		const kv = /^\s*([^=;\s]+)\s*=/.exec(lines[i]);
		if (kv && kv[1].toLowerCase() === key.toLowerCase()) { lines[i] = `${key}=${value}`; done = true; break; }
	}
	if (!done) {
		if (lastInSection < 0) lines.push(`[${section}]`, `${key}=${value}`);
		else lines.splice(lastInSection + 1, 0, `${key}=${value}`);
	}
}
fs.writeFileSync(out, lines.join('\r\n'), 'latin1');
