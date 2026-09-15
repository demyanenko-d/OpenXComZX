// Сводка профилей рендера глобуса по группам и функциям (кадры = T / 71680): читает
// tmp/prof_z0..5.txt — вывод run.ps1 -Headless по сценариям tmp/globeprof_z*.oxs (их пишет
// tools/globeprof.ps1 [-Hour 18]). node tools/globeprof_sum.js > tmp/profsum.md
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
const data = [], tot = [];
for (let z = 0; z < 6; z++) {
	const m = new Map();
	for (const l of fs.readFileSync(`tmp/prof_z${z}.txt`, 'utf8').split(/\r?\n/)) {
		let r = l.match(/OXZ: profile (\d+) T/); if (r) tot[z] = +r[1];
		r = l.match(/OXZ: prof\s+[\d.]+%\s+(\d+)\s+(\S+)/); if (r) m.set(r[2], +r[1]);
	}
	data.push(m);
}
const names = [...new Set(data.flatMap(m => [...m.keys()]))];
const grp = n => G.findIndex(([, re]) => re.test(n));
const f1 = v => v < 0.05 ? '·' : v.toFixed(1);
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
