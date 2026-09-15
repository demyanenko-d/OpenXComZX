// Таблицы глобуса: src/ui/globe_tab.h (банк 24, рендер) — обратные величины 65536/dy
// (dy 2..511) для шага рёбер; src/ui/globe_ui_tab.h (банк 2, точки и клики, углы вида) —
// синус четверти круга Q14 (1025 значений, шаг 16 единиц 16-битного угла = 0.088°) и
// арктангенс atan(i/256) в единицах угла (65536 = 360°, 257 значений); src/ui/globe_sq.s —
// четверти квадратов для умножения 8x8 (область _GTAB банка 24).
// Запуск: node tools\gen_globe_tab.js
'use strict';
const fs = require('fs');
const path = require('path');

function rows(arr, per) {
	const out = [];
	for (let i = 0; i < arr.length; i += per) out.push('\t' + arr.slice(i, i + per).join(', ') + ',');
	return out.join('\n');
}

const sin = [];
for (let i = 0; i <= 1024; i++) sin.push(Math.round(Math.sin(i / 1024 * Math.PI / 2) * 16384));
const atan = [];
for (let i = 0; i <= 256; i++) atan.push(Math.round(Math.atan(i / 256) / (2 * Math.PI) * 65536));
const recip = [0, 0];
for (let d = 2; d < 512; d++) recip.push(Math.round(65536 / d));
// выборка клетки сетки 5° (globe.c cell_of): z = sqrt(1 - i/256) Q14, i = 0..256; границы широт
// sin(5k - 90°) Q14, k = 0..36; cos / sin 5k° Q14, k = 0..18
const sqz = [], sin5 = [], cos5k = [], sin5k = [];
for (let i = 0; i <= 256; i++) sqz.push(Math.round(Math.sqrt(1 - i / 256) * 16384));
for (let k = 0; k <= 36; k++) sin5.push(Math.round(Math.sin((5 * k - 90) * Math.PI / 180) * 16384));
for (let k = 0; k <= 18; k++) { cos5k.push(Math.round(Math.cos(5 * k * Math.PI / 180) * 16384)); sin5k.push(Math.round(Math.sin(5 * k * Math.PI / 180) * 16384)); }

fs.writeFileSync(path.join(__dirname, '..', 'src', 'ui', 'globe_tab.h'), `// Сгенерировано tools/gen_globe_tab.js — не править вручную.
// Таблица рендера глобуса (src/ui/globe.c, банк 24).
#ifndef GLOBE_TAB_H
#define GLOBE_TAB_H

#include <stdint.h>

// 65536 / dy (округлено), dy = 2..511; 0 и 1 — 0 (особый случай). Не static: читает
// src/ui/globe_s.s (dy >= 512 — делится на 2)
const uint16_t recip_tab[512] = {
${rows(recip, 16)}
};

// Клетка сетки 5° по пикселю (globe.c cell_of): sqrt(1 - i/256) Q14, i = 0..256
static const int16_t sqz_q14[257] = {
${rows(sqz, 16)}
};
// sin(5k - 90°) Q14, k = 0..36 — границы широт клеток
static const int16_t sin5_q14[37] = {
${rows(sin5, 16)}
};
// cos 5k°, sin 5k° Q14, k = 0..18 — границы долгот в четверти
static const int16_t cos5k_q14[19] = {
${rows(cos5k, 16)}
};
static const int16_t sin5k_q14[19] = {
${rows(sin5k, 16)}
};

#endif
`);

fs.writeFileSync(path.join(__dirname, '..', 'src', 'ui', 'globe_ui_tab.h'), `// Сгенерировано tools/gen_globe_tab.js — не править вручную.
// Таблицы точек глобуса (src/ui/globe_ui.c, банк 2).
#ifndef GLOBE_UI_TAB_H
#define GLOBE_UI_TAB_H

#include <stdint.h>

// sin(i / 1024 * 90°) * 16384, i = 0..1024
static const int16_t sin_q14[1025] = {
${rows(sin, 16)}
};

// atan(i / 256) в единицах угла (65536 = 360°), i = 0..256
static const uint16_t atan_tab[257] = {
${rows(atan, 16)}
};

#endif
`);

// Четверти квадратов floor(n^2 / 4), n = 0..511 — для умножения 8x8 (a*b = q(a+b) - q(|a-b|)):
// младшие байты — страницы 0 и 1, старшие — 2 и 3 области _GTAB (адрес кратен 256, банк 24).
const lo = [], hi = [];
for (let n = 0; n < 512; n++) { const q = Math.floor(n * n / 4); lo.push(q & 255); hi.push(q >> 8); }
const asm = [
	';; Сгенерировано tools/gen_globe_tab.js — не править вручную.',
	';; Четверти квадратов floor(n^2 / 4), n = 0..511: младшие байты (512), затем старшие (512).',
	';; Область _GTAB — с границы 256 байт в конце банка 24 (tools/build.ps1).',
	'\t.module globe_sq',
	'\t.globl\t_gl_sq',
	'\t.area\t_GTAB',
	'_gl_sq::',
];
for (const tab of [lo, hi])
	for (let i = 0; i < 512; i += 16) asm.push('\t.db\t' + tab.slice(i, i + 16).join(', '));
fs.writeFileSync(path.join(__dirname, '..', 'src', 'ui', 'globe_sq.s'), asm.join('\n') + '\n');
console.log('globe_tab.h: recip 512, cell tables; globe_ui_tab.h: sin 1025, atan 257; globe_sq.s: squares 512');
