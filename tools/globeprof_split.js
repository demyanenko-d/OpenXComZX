// Раскладка времени рендера глобуса: этапы и функции x (расчёт, порты DMA, ожидание DMA,
// пересылки, ожидание кадра). Читает tmp/prof_z0..5.txt (tools\globeprof.ps1 -Hour 18, сценарий
// с «profile off 200 ops»; класс «dma wait» — опрос DMAStatus, эмулятор 07 §6).
//   node tools/globeprof_split.js [зумы, напр. 3,4,5] [функций в таблице, 25] > tmp/profsplit.md
// Кадры = такты профиля / 71680. Расчёт — все команды, кроме портов, блочных пересылок и опроса
// DMA; пересылки — ldir/lddr/ldi… и функции копирования памяти (far_*, memcpy, memset) целиком;
// ожидание кадра — blit (копия на экран стартует после смены кадра) и ui$wait_frames (простой).
'use strict';
const fs = require('fs');
const zooms = (process.argv[2] || '0,1,2,3,4,5').split(',').map(Number);
const TOP = +(process.argv[3] || 25);
const FR = 71680, f1 = t => (t / FR < 0.05 ? '·' : (t / FR).toFixed(2));
const COPY_FN = /^(_far_read|_far_copy|_far_fill|_far_byte|_memcpy|_memset|far_dma)$/;
const VSYNC_FN = /^(blit|ui\$wait_frames)$/;
const STAGES = [
	['Таблицы вида и проекция', /^(_gl_project|vidx|_gl_ktab|kt_put16|kt_lo16|_gl_ztab|tables|___mul|__mul|__div|mul8e|mulu16|_globe_sin|sin16)/],
	['Ячейки (отсев, чтение)', /^(_gl_cull|cu_loop|cu_take|cu_next|rc_loop|rc_done|r_edges|_far_|far_dma|_res_find|cm_fill|bands|_gl_bands|fill)$/],
	['Рёбра (подготовка, отсечение)', /^(_gl_edges|edge1|edge1s|e_rows|e_fast|hclip|limb_l|xclip|lerpz|lerpx|div32|eslope|bord_ev|e_store)$/],
	['Активные рёбра (AEL)', /^(_gl_rows|rw_row|ael_|ai_|fx_|f2_|cap_)/],
	['Вывод отрезков (без тени)', /^(rw_emit|rw_band|rw_pfill|rw_flush|em_run_p|rp_row|rp_next|_gl_rows_pre|_gl_rows_cap)$/],
	['Вывод строк с тенью', /^(rw_emit_s|em_run_s)$/],
	['Тень: уровни блоков', /^(_sh_rows|blk_eval|_sh_ramp|add32|_globe_shadow|_globe_sunlon|globe_sh\$|r_shadow)/],
	['Тень: отрезки и подкладка DMA', /^(build|iv_put|iv_dma|set_num)$/],
	['Копия на экран', /^(blit|dma_wait|dma_go|dma_blk)$/],
	['Предрасчитанные виды', /^(_gview_|gview_)/],
	['Кадр глобуса (прочее)', /^(render|r_rows|r_bg|r_sunonly|_globe_draw|scr_geo\$draw_globe|_globe_snap|_gl_cull)$/],
	['Отладочная печать (только замер)', /^(_dbg_|_text_draw|_glyph_run|_font_|_text_)/],
	['Интерфейс, игра, простой', /./],
];
const stageOf = n => STAGES.findIndex(([, re]) => re.test(n));
const RENDER_LAST = STAGES.length - 3;          // этапы 0..RENDER_LAST — рендер

// функция -> [расчёт, порты, ожидание DMA, пересылки, ожидание кадра]
function load(z) {
	const f = `tmp/prof_z${z}.txt`;
	if (!fs.existsSync(f)) return null;
	const rows = new Map();
	let total = 0, info = '';
	for (const l of fs.readFileSync(f, 'utf8').split(/\r?\n/)) {
		let r = l.match(/OXZ: profile (\d+) T/); if (r) total = +r[1];
		r = l.match(/^globe: (.*)/); if (r) info = r[1];
		r = l.match(/OXZ: opsf (\S+) ([\d ]+)$/);
		if (!r) continue;
		const v = r[2].trim().split(' ').map(Number);
		if (v.length < 21) throw new Error(`${f}: нет класса «dma wait» — профиль снят старым эмулятором`);
		const all = v.reduce((a, b) => a + b, 0), io = v[17], blk = v[18], wait = v[20];
		let s = [all - io - blk - wait, io, wait, blk, 0];
		if (COPY_FN.test(r[1])) s = [0, io, wait, all - io - wait, 0];
		if (VSYNC_FN.test(r[1])) s = [0, io, wait, blk, all - io - blk - wait];
		rows.set(r[1], s);
	}
	return { rows, total, info };
}

const COLS = ['расчёт', 'порты DMA', 'ожидание DMA', 'пересылки', 'ожидание кадра'];
const out = ['# Раскладка времени рендера глобуса', '',
	'Кадры 50 Гц (такты профиля / 71680). Окно профиля — от нажатия стрелки до метки перерисовки,',
	'поэтому в него попадают интерфейс и простой; «рендер» — строки этапов без них.', ''];
const sumRow = s => s.reduce((a, b) => a + b, 0);
const P = zooms.map(z => [z, load(z)]).filter(([, p]) => p);

// 1. этапы по зумам: всего
out.push('## Этапы по зумам, кадров', '', '| этап | ' + P.map(([z]) => `z${z}`).join(' | ') + ' |', '|---|' + '---:|'.repeat(P.length));
const stageSum = P.map(([, p]) => { const a = STAGES.map(() => [0, 0, 0, 0, 0]); for (const [n, s] of p.rows) { const g = stageOf(n); s.forEach((t, k) => a[g][k] += t); } return a; });
STAGES.forEach(([name], g) => out.push(`| ${name} | ${stageSum.map(a => f1(sumRow(a[g]))).join(' | ')} |`));
const renderTot = stageSum.map(a => a.slice(0, RENDER_LAST + 1).reduce((acc, s) => acc.map((v, k) => v + s[k]), [0, 0, 0, 0, 0]));
out.push(`| **рендер** | ${renderTot.map(s => `**${f1(sumRow(s))}**`).join(' | ')} |`);
out.push(`| окно профиля | ${P.map(([, p]) => f1(p.total)).join(' | ')} |`, '');
for (const [z, p] of P) out.push(`- z${z}: ${p.info}`);

// 2. рендер по видам работы
out.push('', '## Рендер по видам работы, кадров', '', '| вид | ' + P.map(([z]) => `z${z}`).join(' | ') + ' |', '|---|' + '---:|'.repeat(P.length));
COLS.forEach((c, k) => out.push(`| ${c} | ${renderTot.map(s => `${f1(s[k])} (${Math.round(100 * s[k] / sumRow(s))}%)`).join(' | ')} |`));

// 3. по зуму: этапы x виды и функции
P.forEach(([z, p], i) => {
	out.push('', `## Зум ${z}: этапы`, '', '| этап | ' + COLS.join(' | ') + ' | всего |', '|---|' + '---:|'.repeat(COLS.length + 1));
	STAGES.forEach(([name], g) => { const s = stageSum[i][g]; if (sumRow(s) / FR >= 0.05) out.push(`| ${name} | ${s.map(f1).join(' | ')} | **${f1(sumRow(s))}** |`); });
	out.push('', `### Зум ${z}: функции рендера (${TOP} самых дорогих)`, '', '| функция | этап | ' + COLS.join(' | ') + ' | всего |', '|---|---|' + '---:|'.repeat(COLS.length + 1));
	[...p.rows].filter(([n]) => stageOf(n) <= RENDER_LAST).sort((a, b) => sumRow(b[1]) - sumRow(a[1])).slice(0, TOP)
		.forEach(([n, s]) => out.push(`| ${n} | ${STAGES[stageOf(n)][0]} | ${s.map(f1).join(' | ')} | **${f1(sumRow(s))}** |`));
});
console.log(out.join('\n'));
