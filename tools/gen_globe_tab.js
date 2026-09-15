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

// Тень (src/ui/globe_sh.c, банк 25): по зумам 0..5 и строкам 0..199 — пары диска pl, pr (как
// globe.c rows_init; строки нет — 255, 0) и z крайних пар (доли R x 255; пиксель вне диска —
// 0) для точного уровня тени на концах строки
const ZRAD = [90, 120, 180, 280, 450, 720];
const shPL = [], shPR = [], shZF = [], shZL = [], shRH = [];
for (const R of ZRAD) {
	for (let y = 0; y < 200; y++) {
		const v = 2 * y + 1 - 200, d = 4 * R * R - v * v;
		let pl = 255, pr = 0;
		if (d > 0) {
			let s = 0; while ((s + 1) * (s + 1) < d) s++;
			const a = Math.max(0, (255 - s + 1) >> 1), b = Math.min(255, (255 + s) >> 1);
			if (a <= b) { pl = a >> 1; pr = b >> 1; }
		}
		// первая пара — правый пиксель (левый у края диска бывает вне диска), последняя — левый
		const z8 = X => { const Y = y + 0.5 - 100, q = 1 - (X * X + Y * Y) / (R * R); return q > 0 ? Math.min(255, Math.round(255 * Math.sqrt(q))) : 0; };
		shPL.push(pl); shPR.push(pr); shZF.push(pl <= pr ? z8(2 * pl + 1.5 - 128) : 0); shZL.push(pl <= pr ? z8(2 * pr + 0.5 - 128) : 0);
		shRH.push(z8(0));                        // полухорда строки ρ / R x 255 (экстремум тени вдоль строки)
	}
}
// пороги уровней в t (ночь уровня k — t > TB_k, как (Sint16) в shade_gradient)
const TBK = [-34, -12, -8, -5, -2, 2, 5, 8, 14, 39];
const shLutFull = [], shLutPair = [];
for (let i = 0; i < 256; i++) {
	const t = i - 128;
	shLutFull.push(TBK.filter(b => t >= b).length);
	shLutPair.push(TBK.filter((b, k) => !(k & 1) && t >= b).length);
}
let seed = 0x5A17;
const shShift = [];
for (let y = 0; y < 200; y++) { seed = (seed * 1103515245 + 12345) & 0x7fffffff; shShift.push((seed >> 16) & 0xFE); }
fs.writeFileSync(path.join(__dirname, '..', 'src', 'ui', 'globe_sh_tab.h'), `// Сгенерировано tools/gen_globe_tab.js — не править вручную.
// Тень глобуса (src/ui/globe_sh.c, банк 25): [зум * 200 + строка] — пары диска pl, pr (как
// rows_init; строки нет — 255, 0), z крайних пар в долях R x 255 (первая — правый пиксель пары,
// последняя — левый).
#ifndef GLOBE_SH_TAB_H
#define GLOBE_SH_TAB_H

#include <stdint.h>

// [(зум * 200 + строка) * 5]: pl, pr, zf, zl, rh — rh = ρ / R x 255, полухорда строки (уровень в
// экстремуме e·s вдоль строки, зумы 0–1)
const uint8_t sh_row[6000] = {
${rows(shPL.map((v, i) => [v, shPR[i], shZF[i], shZL[i], shRH[i]]).flat(), 25)}
};
// Уровень тени по t = i − 128 (целое): все 10 порогов / только 1, 3, 5, 7, 9 (полосы по два)
const uint8_t sh_lut_full[256] = {
${rows(shLutFull, 16)}
};
const uint8_t sh_lut_pair[256] = {
${rows(shLutPair, 16)}
};
// Сдвиг строки-образца шума по строке экрана (чётный: DMA пишет словами)
const uint8_t sh_shift[200] = {
${rows(shShift, 25)}
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
