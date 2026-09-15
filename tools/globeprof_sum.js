// Сводка профилей рендера глобуса по группам и функциям (кадры = T / 71680): читает
// tmp/prof_z0..5.txt — вывод эмулятора, их пишет tools/globeprof.ps1 [-Hour 18].
// node tools/globeprof_sum.js > tmp/profsum.md
// node tools/globeprof_sum.js ops — по операциям (сценарии с «profile off 200 ops», эмулятор
// 07 §6): подпрограммы умножения/деления (вызовы, тактов 14 МГц на вызов), такты рендера по
// классам инструкций Z80 и по этапам.
const fs = require('fs');
const G = [
	['Математика SDCC (mul/div библиотеки)', /^_{2,3}(mul|div|mod|get_rem)/],
	['Математика асм (деления, умножения, интерполяция)', /^(div32|mul8e|mulu16|lerpz|lerpx|eslope|add32)$/],
	['Тригонометрия', /(_globe_sin|sin16)$/],
	['Проекция вершин', /^(_gl_project|vidx|globe\$project)$/],
	['Таблицы вида', /^(_gl_ktab|kt_put16|kt_lo16|_gl_ztab|globe\$tables)$/],
	['Рёбра (подготовка, отсечение)', /^(_gl_edges|edge1|edge1s|e_rows|e_fast|hclip|limb_l|xclip|bord_ev|e_store)$/],
	['Активные рёбра (вставка, починка, шаг)', /^(_gl_rows|rw_row|ael_|ai_|fx_|f2_)/],
	['Вывод отрезков -> DMA', /^(rw_emit|rw_band|rw_pfill|rw_flush|em_run|gb_end|globe\$dma_wait)$/],
	['Тень: вывод строк с тенью', /^(rw_emit_s|em_run_s)$/],
	['Тень: блоки и подкладка', /^(_sh_rows|blk_eval|build|iv_put|iv_dma|set_num|_sh_ramp|globe_sh\$|_globe_shadow|_globe_sunlon)/],
	['Ячейки, C рендера, копии', /^(globe\$|_gl_bands|_far_|_memset|_memcpy)/],
	['Отладочная печать', /^(_dbg_|_text_draw|_glyph_run|_font_|_text_)/],
	['Интерфейс, игра, ожидание кадра', /./],
];
const fr = t => t / 71680;
const data = [], tot = [], calls = [], ops = [], opsf = [];
for (let z = 0; z < 6; z++) {
	const m = new Map(), c = new Map(), o = [], of = new Map();
	for (const l of fs.readFileSync(`tmp/prof_z${z}.txt`, 'utf8').split(/\r?\n/)) {
		let r = l.match(/OXZ: profile (\d+) T/); if (r) tot[z] = +r[1];
		r = l.match(/OXZ: prof\s+[\d.]+%\s+(\d+)\s+(\S+)(?:\s+calls (\d+) iters (\d+))?/);
		if (r) { m.set(r[2], +r[1]); c.set(r[2], [+(r[3] || 0), +(r[4] || 0)]); }
		r = l.match(/OXZ: ops (.+?)\s+(\d+)\s+[\d.]+%\s+(\d+)$/); if (r) o.push([r[1], +r[2], +r[3]]);
		r = l.match(/OXZ: opsf (\S+) ([\d ]+)$/); if (r) of.set(r[1], r[2].split(' ').map(Number));
	}
	data.push(m); calls.push(c); ops.push(o); opsf.push(of);
}
const names = [...new Set(data.flatMap(m => [...m.keys()]))];
const grp = n => G.findIndex(([, re]) => re.test(n));
const f1 = v => v < 0.05 ? '·' : v.toFixed(1);
if (process.argv[2] === 'ops') { opsReport(); process.exit(0); }

function opsReport() {
	const out = [], Z = [0, 1, 2, 3, 4, 5];
	// 1. подпрограммы арифметики: вызовов и тактов 14 МГц на вызов (профиль — в тактах 3.5 МГц)
	const MATH = [['___mulsint2slong', 'умножение 16x16 -> 32 со знаком (SDCC, C)'], ['___muluint2ulong', 'умножение 16x16 -> 32 без знака (SDCC, C)'],
		['__mullong', 'умножение 32x32 (SDCC, C)'], ['__mulint', 'умножение 16x16 -> 16 (SDCC, C)'], ['__divulong', 'деление 32/32 без знака (SDCC, C)'],
		['__divslong', 'деление 32/32 со знаком (SDCC, C)'], ['__divuint', 'деление 16/16 (SDCC, C)'], ['__divsint', 'деление 16/16 со знаком (SDCC, C)'],
		['mul8e', 'умножение 8x8 таблицей квадратов (асм)'], ['mulu16', 'умножение 16x16 -> 32 (асм, 4 x mul8e)'], ['div32', 'деление 32/16 (асм, наклоны рёбер)'],
		['lerpz', 'интерполяция z (асм, край диска)'], ['lerpx', 'интерполяция x (асм, отсечение)'], ['add32', 'сложение 32 бит (асм, лестницы тени)'],
		['_globe_sin', 'синус таблицей']];
	out.push('### Подпрограммы арифметики: вызовов / тактов 14 МГц на вызов / кадров', '',
		'| подпрограмма | что | ' + Z.map(z => `зум ${z}`).join(' | ') + ' |', '|---|---|' + '---|'.repeat(6));
	for (const [n, what] of MATH) {
		const cells = Z.map(z => { const [c] = calls[z].get(n) || [0, 0], t = data[z].get(n) || 0; return c ? `${c} / ${Math.round(4 * t / c)} / ${f1(fr(t))}` : '·'; });
		if (cells.every(s => s === '·')) continue;
		out.push(`| ${n} | ${what} | ${cells.join(' | ')} |`);
	}
	// 2. классы инструкций в функциях рендера (без отладочной печати, интерфейса и ожидания)
	const nC = ops[0].length, cname = ops[0].map(o => o[0]);
	const render = n => { const g = grp(n); return g >= 0 && g < G.length - 2; };
	const cls = Z.map(z => { const s = new Array(nC).fill(0); for (const [n, v] of opsf[z]) if (render(n)) v.forEach((t, k) => s[k] += t); return s; });
	out.push('', '### Рендер по классам инструкций Z80, кадров (доля на зуме 0)', '',
		'| класс | ' + Z.map(z => `зум ${z}`).join(' | ') + ' | доля z0 |', '|---|' + '---:|'.repeat(7));
	const all0 = cls[0].reduce((a, b) => a + b, 0);
	const order = [...cname.keys()].sort((a, b) => cls[0][b] - cls[0][a]);
	for (const k of order) out.push(`| ${cname[k]} | ${Z.map(z => f1(fr(cls[z][k]))).join(' | ')} | ${(100 * cls[0][k] / all0).toFixed(1)}% |`);
	out.push(`| **всего рендер** | ${Z.map(z => `**${fr(cls[z].reduce((a, b) => a + b, 0)).toFixed(1)}**`).join(' | ')} | |`);
	// 3. этапы x крупные классы, зум 0 и 5
	const BIG = [['арифм. 8', ['add/adc 8', 'sub/sbc/neg 8', 'inc/dec 8', 'cp']], ['арифм. 16', ['add/adc 16', 'sbc 16', 'inc/dec 16']],
		['логика, сдвиги, биты', ['and/or/xor/cpl', 'shift/rotate', 'bit/set/res']], ['память', ['ld memory', 'ldi/ldir/outi']],
		['регистры', ['ld r,r / r,n', 'ld rr,nn', 'ex/exx']], ['переходы', ['jp/jr/djnz']], ['call/ret, стек', ['call/ret/rst', 'push/pop']],
		['порты (DMA)', ['in/out']], ['прочее', ['other']]];
	for (const z of [0, 2, 5]) {
		out.push('', `### Этапы по крупным классам, зум ${z}, кадров`, '', '| этап | ' + BIG.map(b => b[0]).join(' | ') + ' | всего |', '|---|' + '---:|'.repeat(BIG.length + 1));
		for (let g = 0; g < G.length - 2; g++) {
			const s = new Array(nC).fill(0);
			for (const [n, v] of opsf[z]) if (grp(n) === g) v.forEach((t, k) => s[k] += t);
			const tt = s.reduce((a, b) => a + b, 0);
			if (fr(tt) < 0.05) continue;
			out.push(`| ${G[g][0]} | ` + BIG.map(([, ks]) => f1(fr(ks.reduce((a, c) => a + s[cname.indexOf(c)], 0)))).join(' | ') + ` | ${f1(fr(tt))} |`);
		}
	}
	// 4. итерации горячих функций (наибольшее число исполнений одной инструкции)
	out.push('', '### Итерации и цена на единицу (тактов 14 МГц), зумы 0 / 2 / 5', '', '| функция | итераций | тактов на итерацию |', '|---|---|---|');
	for (const n of ['_gl_project', 'edge1', 'e_fast', 'e_store', 'ael_step', 'ael_ins', 'ai_redo', 'rw_emit', 'rw_emit_s', 'em_run_s', 'blk_eval', 'build', 'iv_dma', '_sh_rows'])
		out.push(`| ${n} | ${[0, 2, 5].map(z => (calls[z].get(n) || [0, 0])[1]).join(' / ')} | ${[0, 2, 5].map(z => { const it = (calls[z].get(n) || [0, 0])[1]; return it ? Math.round(4 * (data[z].get(n) || 0) / it) : '·'; }).join(' / ')} |`);
	console.log(out.join('\n'));
}
const out = ['| | ' + [0, 1, 2, 3, 4, 5].map(z => `зум ${z}`).join(' | ') + ' |', '|---|' + '---:|'.repeat(6)];
for (let g = 0; g < G.length; g++) {
	const ns = names.filter(n => grp(n) === g);
	const sum = data.map(m => ns.reduce((s, n) => s + (m.get(n) || 0), 0));
	out.push(`| **${G[g][0]}** | ` + sum.map(v => `**${f1(fr(v))}**`).join(' | ') + ' |');
	ns.sort((a, b) => (data[0].get(b) || 0) + (data[5].get(b) || 0) - (data[0].get(a) || 0) - (data[5].get(a) || 0));
	for (const n of ns) {
		const v = data.map(m => fr(m.get(n) || 0));
		if (Math.max(...v) < 0.1) continue;
		out.push(`| &nbsp;&nbsp;${n} | ` + v.map(f1).join(' | ') + ' |');
	}
}
out.push('| **Всего (окно профиля)** | ' + tot.map(t => `**${fr(t).toFixed(1)}**`).join(' | ') + ' |');
console.log(out.join('\n'));
