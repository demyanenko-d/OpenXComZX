// Тема 2: предрассчитанные «усечённые» варианты кадров тайлов (16_battlescape_plan.md §8).
//
// Идея: в изометрии соседняя клетка спереди (и клетка этажом выше) всегда закрывает одни и те же
// части спрайта. Конвертер может посчитать это заранее, а Z80 в кадре только выбирает вариант.
// Ограничение железа (02 §6): BLT1 читает источник ЛИНЕЙНО (S_ALGN не ставим), поэтому
// «срезать строки сверху/снизу» бесплатно по памяти (тот же массив, другой offset/h), а
// «срезать столбцы» требует отдельной копии кадра в тайлсете.
//
//   node tools/proto/opt2_variants.js [--game TFTD|UFO] [--terrain SEABED] [--blocks 4] [--verify]
//   node tools/proto/opt2_variants.js --all [--out tmp/proto/opt2]
//
// «байт» — что прочитает/запишет DMA BLT1 (w*h обрезанного кадра, w чётная), «блит» — один
// запуск DMA. Считаются только клетки, целиком попавшие в окно карты 320x144, с полным окружением.
'use strict';
const fs = require('fs');
const path = require('path');
const B = require('./battle');

const TW = B.TILE_W, TH = B.TILE_H;      // 32 x 40

function arg(name, def) {
	const i = process.argv.indexOf('--' + name);
	return i > 0 && i + 1 < process.argv.length ? process.argv[i + 1] : def;
}
const hasFlag = (n) => process.argv.indexOf('--' + n) > 0;

// Стоимость: BLT1 даёт ~74 КБ за кадр 286720 T -> ~3.8 T на байт; накладные на блит —
// 3000 T нынешнего кода на C (§8.3b) или ~500 T будущего ассемблерного
const T_PER_BYTE = 286720 / (74 * 1024);
const OVH_C = 3000, OVH_ASM = 500;

// ---------------------------------------------------------------- правила террейнов

function parseTerrains(rulPath) {
	const out = new Map();
	const lines = fs.readFileSync(rulPath, 'utf8').split(/\r?\n/);
	let cur = null, sect = null, blk = null;
	for (const raw of lines) {
		const line = raw.replace(/#.*$/, '');
		let m;
		if ((m = line.match(/^  - name:\s*(\S+)/))) {
			cur = { name: m[1], sets: [], blocks: [] };
			out.set(cur.name, cur); sect = null; blk = null; continue;
		}
		if (!cur) continue;
		if ((m = line.match(/^    (\w+):/))) { sect = m[1]; blk = null; continue; }
		if (sect === 'mapDataSets' && (m = line.match(/^      -\s*(\S+)/))) cur.sets.push(m[1]);
		if (sect === 'mapBlocks') {
			if ((m = line.match(/^      - name:\s*(\S+)/))) { blk = { name: m[1], w: 10, l: 10 }; cur.blocks.push(blk); }
			else if (blk && (m = line.match(/^        width:\s*(\d+)/))) blk.w = +m[1];
			else if (blk && (m = line.match(/^        length:\s*(\d+)/))) blk.l = +m[1];
		}
	}
	return out;
}

// ---------------------------------------------------------------- загрузка сцены

function loadSets(root, sets) {
	const parts = [], frames = [];
	for (const s of sets) {
		const dir = path.join(root, 'TERRAIN');
		const mcdPath = path.join(dir, s + '.MCD');
		if (!fs.existsSync(mcdPath)) throw new Error('нет ' + mcdPath);
		const mcd = B.readMcd(fs.readFileSync(mcdPath));
		const pck = B.readPck(fs.readFileSync(path.join(dir, s + '.PCK')), fs.readFileSync(path.join(dir, s + '.TAB')));
		const base = frames.length;
		for (const r of mcd) { r.set = s; r.base = base; parts.push(r); }
		for (const f of pck) frames.push(f);
	}
	return { parts, frames };
}

function buildField(root, blockNames, n) {
	const dir = path.join(root, 'MAPS');
	const blocks = [];
	for (const nm of blockNames) {
		const f = path.join(dir, nm + '.MAP');
		if (fs.existsSync(f)) blocks.push(B.readMap(fs.readFileSync(f)));
	}
	if (!blocks.length) throw new Error('нет блоков карты');
	const bw = blocks[0].sx, bh = blocks[0].sy;
	let sz = 0;
	for (const b of blocks) if (b.sz > sz) sz = b.sz;
	const sx = bw * n, sy = bh * n;
	const cells = new Uint8Array(sx * sy * sz * 4);
	for (let by = 0; by < n; by++)
		for (let bx = 0; bx < n; bx++) {
			const b = blocks[(by * n + bx) % blocks.length];
			if (b.sx !== bw || b.sy !== bh) continue;
			for (let z = 0; z < b.sz; z++)
				for (let y = 0; y < b.sy; y++)
					for (let x = 0; x < b.sx; x++) {
						const from = ((z * b.sy + y) * b.sx + x) * 4;
						const to = ((z * sy + by * bh + y) * sx + bx * bw + x) * 4;
						for (let k = 0; k < 4; k++) cells[to + k] = b.cells[from + k];
					}
		}
	return { sx, sy, sz, cells };
}

// ---------------------------------------------------------------- кадры: маски и габариты

const maskCache = new Map(), boxCache = new Map();
function frameMask(img) {
	let m = maskCache.get(img);
	if (m) return m;
	m = new Uint32Array(TH);
	for (let y = 0; y < TH; y++) {
		let v = 0;
		for (let x = 0; x < TW; x++) if (img[y * TW + x]) v |= (1 << x);
		m[y] = v >>> 0;
	}
	maskCache.set(img, m);
	return m;
}
// габариты как их режет конвертер (Sprites.cs:14-29): x0 чётный, ширина чётная
function frameBox(img) {
	let b = boxCache.get(img);
	if (b) return b;
	let x0 = TW, y0 = TH, x1 = -1, y1 = -1;
	for (let y = 0; y < TH; y++)
		for (let x = 0; x < TW; x++)
			if (img[y * TW + x]) {
				if (x < x0) x0 = x; if (x > x1) x1 = x;
				if (y < y0) y0 = y; if (y > y1) y1 = y;
			}
	if (x1 < 0) b = { x: 0, y: 0, w: 0, h: 0 };
	else {
		x0 &= ~1;
		let w = x1 - x0 + 1;
		if (w & 1) w++;
		if (x0 + w > TW) x0 = TW - w;
		b = { x: x0, y: y0, w, h: y1 - y0 + 1 };
	}
	boxCache.set(img, b);
	return b;
}

// Маски по всем кадрам анимации записи MCD (Frame[0..7]; у дверей НЛО кадры 0 и 7 — состояния):
// объединение — для «закрываемого» (усечение обязано быть верно для любого кадра),
// пересечение — для «закрывающего» (закрывает только то, что закрыто во всех кадрах)
const animCache = new Map();
function animMasks(rec, frames) {
	if (!rec) return { u: new Uint32Array(TH), i: new Uint32Array(TH), box: { x: 0, y: 0, w: 0, h: 0 } };
	const key = rec.set + ':' + rec.frame.join(',');
	let v = animCache.get(key);
	if (v) return v;
	const u = new Uint32Array(TH), i = new Uint32Array(TH).fill(0xFFFFFFFF);
	let x0 = TW, y0 = TH, x1 = -1, y1 = -1;
	for (const f of rec.frame) {
		const img = frames[rec.base + f];
		if (!img) continue;
		const m = frameMask(img), b = frameBox(img);
		for (let r = 0; r < TH; r++) { u[r] = (u[r] | m[r]) >>> 0; i[r] = (i[r] & m[r]) >>> 0; }
		if (b.w) {
			if (b.x < x0) x0 = b.x; if (b.x + b.w - 1 > x1) x1 = b.x + b.w - 1;
			if (b.y < y0) y0 = b.y; if (b.y + b.h - 1 > y1) y1 = b.y + b.h - 1;
		}
	}
	const box = x1 < 0 ? { x: 0, y: 0, w: 0, h: 0 } : { x: x0 & ~1, y: y0, w: ((x1 - (x0 & ~1) + 2) & ~1), h: y1 - y0 + 1 };
	v = { u, i, box };
	animCache.set(key, v);
	return v;
}

// ---------------------------------------------------------------- геометрия соседей

// Кто рисуется позже клетки (0,0,0) в порядке художника (Map.cpp: z вверх, x, y) и может её
// перекрыть: |dx-dy| <= 1 (спрайты 32 px по X) и вертикальное смещение меньше высоты спрайта
function neighbourhood(maxYofs) {
	const list = [];
	for (let dz = 0; dz <= 3; dz++)
		for (let dx = -2; dx <= 6; dx++)
			for (let dy = -2; dy <= 6; dy++) {
				if (Math.abs(dx - dy) > 1) continue;
				const oy = (dx + dy) * B.QUART - dz * B.LEVEL_H;
				if (Math.abs(oy) >= TH + maxYofs) continue;
				const later = dz > 0 || (dz === 0 && (dx > 0 || (dx === 0 && dy > 0)));
				if (!later && !(dx === 0 && dy === 0 && dz === 0)) continue;
				list.push({ dx, dy, dz, key: `${dx},${dy},${dz}` });
			}
	return list;
}

const shl = (v, s) => (s >= 32 || s <= -32) ? 0 : (s >= 0 ? (v << s) : (v >>> -s)) >>> 0;

// наборы соседей для схем (чем меньше — тем дешевле рантайм)
const SUBSETS = {
	'своя клетка': new Set(['0,0,0']),
	'своя+1 (1,1)': new Set(['0,0,0', '1,1,0']),
	'своя+3 ближних': new Set(['0,0,0', '1,1,0', '1,0,0', '0,1,0']),
	'своя+6 (свой этаж)': new Set(['0,0,0', '1,1,0', '1,0,0', '0,1,0', '2,1,0', '1,2,0', '2,2,0']),
	'своя+6+этаж выше': null,   // заполняется ниже
	'всё (предел)': null,
};

// ---------------------------------------------------------------- сцена

function makeScene(map, sets, level) {
	const { parts, frames } = sets;
	const partFrame = (id) => {
		const r = parts[id - 1];
		return r ? frames[r.base + r.frame[0]] : null;
	};
	const ox = map.sy * B.HALF_W + 16;
	const W = (map.sx + map.sy) * B.HALF_W + TW + 32;
	const H = (map.sx + map.sy) * B.QUART + level * B.LEVEL_H + TH + 64;
	const oyBase = level * B.LEVEL_H + 32;
	const cellAt = (x, y, z) => {
		const o = ((z * map.sy + y) * map.sx + x) * 4;
		return [map.cells[o], map.cells[o + 1], map.cells[o + 2], map.cells[o + 3]];
	};
	const inst = [];
	const index = new Map();
	for (let z = 0; z <= level; z++)
		for (let x = 0; x < map.sx; x++)
			for (let y = 0; y < map.sy; y++) {
				const p = cellAt(x, y, z);
				if (!p[0] && !p[1] && !p[2] && !p[3]) continue;
				const sx = (x - y) * B.HALF_W + ox;
				const sy = (x + y) * B.QUART - z * B.LEVEL_H + oyBase;
				for (let k = 0; k < 4; k++) {
					if (!p[k]) continue;
					const rec = parts[p[k] - 1];
					const img = partFrame(p[k]);
					if (!img) continue;
					const box = frameBox(img);
					if (!box.w) continue;
					const yofs = rec ? rec.pLevel : 0;
					if (!index.has(x + ',' + y + ',' + z)) index.set(x + ',' + y + ',' + z, inst.length);
					const an = animMasks(rec, frames);
					inst.push({ x, y, z, k, id: p[k], rec, img, box, bx: sx, by: sy - yofs, yofs,
						mask: frameMask(img), maskU: an.u, maskI: an.i, boxU: an.box, idx: inst.length });
				}
			}
	return { inst, index, W, H, cellAt, partFrame, parts, frames };
}

function cellInstances(sc, x, y, z) {
	const at = sc.index.get(x + ',' + y + ',' + z);
	if (at === undefined) return [];
	const out = [];
	for (let i = at; i < sc.inst.length; i++) {
		const it = sc.inst[i];
		if (it.x !== x || it.y !== y || it.z !== z) break;
		out.push(it);
	}
	return out;
}

// ---------------------------------------------------------------- перекрытия

// вклад каждого смещения: маска точек экземпляра it, закрытых позже нарисованными частями
function coverContribs(sc, it, nb) {
	const out = [];
	for (const d of nb) {
		const nx = it.x + d.dx, ny = it.y + d.dy, nz = it.z + d.dz;
		if (nx < 0 || ny < 0 || nz < 0) continue;
		let acc = null;
		for (const j of cellInstances(sc, nx, ny, nz)) {
			if (j.idx <= it.idx) continue;
			const sx = j.bx - it.bx, sy = j.by - it.by;
			for (let r = 0; r < TH; r++) {
				const rj = r - sy;
				if (rj < 0 || rj >= TH) continue;
				const bits = shl(j.mask[rj], sx);
				if (!bits) continue;
				if (!acc) acc = new Uint32Array(TH);
				acc[r] = (acc[r] | bits) >>> 0;
			}
		}
		if (acc) out.push({ d, mask: acc });
	}
	return out;
}

// объединение вкладов по набору соседей с огрублением до групп по g столбцов
function combine(contribs, subset, g) {
	const cov = new Uint32Array(TH);
	for (const c of contribs) {
		if (subset && !subset.has(c.d.key)) continue;
		for (let r = 0; r < TH; r++) cov[r] = (cov[r] | c.mask[r]) >>> 0;
	}
	if (g > 1) {
		for (let r = 0; r < TH; r++) {
			let v = 0;
			for (let x = 0; x < TW; x += g) {
				const gm = (g >= 32 ? 0xFFFFFFFF : ((1 << g) - 1) << x) >>> 0;
				if ((cov[r] & gm) >>> 0 === gm) v |= gm;
			}
			cov[r] = v >>> 0;
		}
	}
	return cov;
}

// ---------------------------------------------------------------- схемы усечения

function deadRows(it, cov) {
	const dead = new Uint8Array(TH);
	for (let r = 0; r < TH; r++) dead[r] = ((it.mask[r] & ~cov[r]) >>> 0) === 0 ? 1 : 0;
	return dead;
}

// срез строк сверху/снизу — память тайлсета не растёт (другой offset/h у той же картинки)
function rowTrim(box, dead) {
	let top = 0, bot = 0;
	while (top < box.h && dead[box.y + top]) top++;
	if (top === box.h) return { top, bot: 0, h: 0 };
	while (bot < box.h - top && dead[box.y + box.h - 1 - bot]) bot++;
	return { top, bot, h: box.h - top - bot };
}

// разрез на полосы: не более nbMax блитов
function bands(box, dead, nbMax) {
	const live = [];
	for (let r = box.y; r < box.y + box.h; r++) if (!dead[r]) live.push(r);
	if (!live.length) return [];
	const segs = [];
	let s = live[0], p = live[0];
	for (let i = 1; i < live.length; i++) {
		if (live[i] !== p + 1) { segs.push([s, p]); s = live[i]; }
		p = live[i];
	}
	segs.push([s, p]);
	while (segs.length > nbMax) {
		let best = -1, bestGap = 1e9;
		for (let i = 0; i + 1 < segs.length; i++) {
			const gap = segs[i + 1][0] - segs[i][1] - 1;
			if (gap < bestGap) { bestGap = gap; best = i; }
		}
		segs[best] = [segs[best][0], segs[best + 1][1]];
		segs.splice(best + 1, 1);
	}
	return segs;
}

// тесные габариты (строки и столбцы) — нужна отдельная копия кадра в тайлсете
function tightBox(it, cov) {
	let x0 = TW, x1 = -1, y0 = TH, y1 = -1;
	for (let r = 0; r < TH; r++) {
		const live = (it.mask[r] & ~cov[r]) >>> 0;
		if (!live) continue;
		if (r < y0) y0 = r; if (r > y1) y1 = r;
		for (let x = 0; x < TW; x++) if (live & (1 << x)) { if (x < x0) x0 = x; if (x > x1) x1 = x; }
	}
	if (x1 < 0) return { x: 0, y: 0, w: 0, h: 0 };
	x0 &= ~1;
	let w = x1 - x0 + 1;
	if (w & 1) w++;
	if (x0 + w > TW) x0 = TW - w;
	return { x: x0, y: y0, w, h: y1 - y0 + 1 };
}

// половины по 16 точек: тех же байт, но кадр хранится двумя кусками (строка 16 Б)
function halves(it, cov) {
	const out = [];
	for (const half of [0, 1]) {
		const lo = half * 16;
		let mask = 0;
		for (let x = lo; x < lo + 16; x++) mask |= (1 << x);
		mask >>>= 0;
		let y0 = -1, y1 = -1;
		for (let r = 0; r < TH; r++) {
			if (!((it.mask[r] & ~cov[r] & mask) >>> 0)) continue;
			if (y0 < 0) y0 = r;
			y1 = r;
		}
		out.push(y0 < 0 ? null : { x: lo, y: y0, w: 16, h: y1 - y0 + 1 });
	}
	return out;
}

// ---------------------------------------------------------------- подсчёт

function analyse(sc, opt) {
	const nb = neighbourhood(24);
	SUBSETS['своя+6+этаж выше'] = new Set([...SUBSETS['своя+6 (свой этаж)'], ...nb.filter(d => d.dz === 1).map(d => d.key)]);
	const view = opt.view, margin = 4;
	const schemes = [];                    // строки таблицы «схема -> блиты/байты/память»
	const mk = (name, mem) => { const s = { name, mem, blits: 0, bytes: 0 }; schemes.push(s); return s; };
	const base = mk('как сейчас (полный кадр)', '—');
	const rowSch = {}, tight = {}, half = {}, band2 = {};
	for (const sname of Object.keys(SUBSETS)) rowSch[sname] = {};
	for (const sname of Object.keys(SUBSETS))
		for (const g of [16, 8, 1])
			rowSch[sname][g] = mk(`строки: ${sname}, точность ${g === 1 ? 'точно' : g + ' px'}`, 'без роста данных');
	const bestSub = 'своя+6 (свой этаж)';
	const pairMax = mk(`макс. среза по одному соседу (${bestSub}, таблица пар кадров)`, 'таблица пар');
	band2.s = mk(`полосы до 2 блитов (${bestSub}, точно)`, 'без роста данных');
	half.s = mk(`половины 16 px + срез строк (${bestSub})`, 'те же байты, +1 запись');
	tight.s = mk(`тесные габариты (${bestSub})`, 'копия кадра на вариант');
	const ideal = mk('идеал по точкам (не реализуемо)', '—');

	const st = {
		cells: 0, px: 0, pxCov: 0, schemes,
		variants: new Map(), tightVariants: new Map(), byOffset: new Map(), pairs: new Set(),
		partBytes: [0, 0, 0, 0], partBytesRow: [0, 0, 0, 0], dropped: [0, 0, 0, 0], partCnt: [0, 0, 0, 0],
		trimHist: new Map(),
	};
	for (const it of sc.inst) {
		if (it.x < margin || it.y < margin || it.x >= opt.sx - margin || it.y >= opt.sy - margin) continue;
		const x0 = it.bx + it.box.x, y0 = it.by + it.box.y;
		if (x0 < view.x0 || y0 < view.y0 || x0 + it.box.w - 1 > view.x1 || y0 + it.box.h - 1 > view.y1) continue;
		st.cells++;
		const full = it.box.w * it.box.h;
		base.blits++; base.bytes += full;
		st.partBytes[it.k] += full; st.partCnt[it.k]++;
		const contribs = coverContribs(sc, it, nb);
		// точки
		const covAll = combine(contribs, null, 1);
		for (let r = 0; r < TH; r++) {
			const m = it.mask[r] >>> 0, c = (it.mask[r] & covAll[r]) >>> 0;
			for (let x = 0; x < TW; x++) {
				if (m & (1 << x)) st.px++;
				if (c & (1 << x)) st.pxCov++;
			}
		}
		ideal.blits++;
		for (let r = 0; r < TH; r++) {
			const live = (it.mask[r] & ~covAll[r]) >>> 0;
			for (let x = 0; x < TW; x++) if (live & (1 << x)) ideal.bytes++;
		}
		// вклад отдельных соседей
		for (const c of contribs) {
			let n = 0;
			for (let r = 0; r < TH; r++) {
				const v = (it.mask[r] & c.mask[r]) >>> 0;
				for (let x = 0; x < TW; x++) if (v & (1 << x)) n++;
			}
			st.byOffset.set(c.d.key, (st.byOffset.get(c.d.key) || 0) + n);
		}
		// схемы «срез строк»
		let covBest = null;
		for (const sname of Object.keys(SUBSETS)) {
			for (const g of [16, 8, 1]) {
				const cov = combine(contribs, SUBSETS[sname], g);
				const t = rowTrim(it.box, deadRows(it, cov));
				const s = rowSch[sname][g];
				s.bytes += it.box.w * t.h;
				if (t.h) s.blits++;
				if (sname === bestSub && g === 1) {
					covBest = cov;
					st.partBytesRow[it.k] += it.box.w * t.h;
					if (!t.h) st.dropped[it.k]++;
					st.variants.set(`${it.id}:${t.top}:${t.bot}`, (st.variants.get(`${it.id}:${t.top}:${t.bot}`) || 0) + 1);
					const key = t.h === 0 ? 'кадр целиком не нужен' : (t.top || t.bot ? `срез ${t.top}+${t.bot}` : 'без среза');
					st.trimHist.set(key, (st.trimHist.get(key) || 0) + 1);
				}
			}
		}
		// «максимум среза по одному соседу»: конвертер кладёт таблицу пар (закрываемый кадр,
		// закрывающий кадр, смещение) -> (срез сверху, срез снизу); рантайм берёт максимум
		{
			let top = 0, bot = 0;
			for (const c of contribs) {
				if (!SUBSETS[bestSub].has(c.d.key)) continue;
				const t = rowTrim(it.box, deadRows(it, c.mask));
				if (!t.h) { top = it.box.h; bot = 0; break; }
				if (t.top > top) top = t.top;
				if (t.bot > bot) bot = t.bot;
				for (const j of cellInstances(sc, it.x + c.d.dx, it.y + c.d.dy, it.z + c.d.dz))
					if (j.idx > it.idx) st.pairs.add(`${it.id}:${j.id}:${c.d.key}`);
			}
			const h = Math.max(0, it.box.h - top - bot);
			pairMax.bytes += it.box.w * h;
			if (h) pairMax.blits++;
		}
		// полосы
		const dead = deadRows(it, covBest);
		for (const sg of bands(it.box, dead, 2)) { band2.s.blits++; band2.s.bytes += it.box.w * (sg[1] - sg[0] + 1); }
		// половины
		for (const h of halves(it, covBest)) if (h) { half.s.blits++; half.s.bytes += h.w * h.h; }
		// тесные габариты
		const tb = tightBox(it, covBest);
		if (tb.w) { tight.s.blits++; tight.s.bytes += tb.w * tb.h; }
		const tk = `${it.id}:${tb.x}:${tb.y}:${tb.w}:${tb.h}`;
		if (!st.tightVariants.has(tk)) {
			// вариант с урезанной шириной нельзя получить сдвигом offset — нужна копия данных
			const same = tb.x === it.box.x && tb.w === it.box.w;
			st.tightVariants.set(tk, { n: 0, extra: same ? 0 : tb.w * tb.h });
		}
		st.tightVariants.get(tk).n++;
	}
	return st;
}

// ---------------------------------------------------------------- «накопитель занятости»
//
// Вместо опроса 20+ соседей — один проход по видимым клеткам от ближних к дальним с битовой
// картой занятости экрана (группа g точек в строке = 1 бит; группа считается закрытой, только
// если спрайт закрыл её целиком). Потом обычный проход от дальних к ближним уже с усечением.
// Данные от конвертера те же: маска кадра по группам (40 строк x 32/g бит).
function pushAnalyse(sc, view, opt, g, cost, animSafe) {
	// в режиме animSafe закрываемое берётся по объединению кадров анимации, закрывающее — по пересечению
	const MR = (it) => animSafe ? it.maskU : it.mask;      // маска для «меня закрывают»
	const MM = (it) => animSafe ? it.maskI : it.mask;      // маска для «я закрываю»
	const BX = (it) => animSafe ? (it.boxU.w ? it.boxU : it.box) : it.box;
	const VW = view.x1 - view.x0 + 1, VH = view.y1 - view.y0 + 1;
	const GW = Math.ceil(VW / g);
	const occ = new Uint8Array(GW * VH);
	const st = { rows: { blits: 0, bytes: 0 }, best: { blits: 0, bytes: 0 }, tight: { blits: 0, bytes: 0 },
		ops: 0, cells: 0, tightVar: new Map(), trims: [] };
	// экземпляры, пересекающие окно, в обратном порядке отрисовки
	const list = sc.inst.filter(it => {
		const x0 = it.bx + it.box.x, y0 = it.by + it.box.y;
		return x0 + it.box.w > view.x0 && x0 <= view.x1 && y0 + it.box.h > view.y0 && y0 <= view.y1;
	});
	for (let i = list.length - 1; i >= 0; i--) {
		const it = list[i];
		const inside = it.bx + it.box.x >= view.x0 && it.by + it.box.y >= view.y0 &&
			it.bx + it.box.x + it.box.w - 1 <= view.x1 && it.by + it.box.y + it.box.h - 1 <= view.y1 &&
			it.x >= opt.margin && it.y >= opt.margin && it.x < opt.sx - opt.margin && it.y < opt.sy - opt.margin;
		// 1) какие строки кадра уже закрыты
		const cov = new Uint32Array(TH);
		for (let r = 0; r < TH; r++) {
			const my = MR(it)[r];
			if (!my) continue;
			const py = it.by + r - view.y0;
			if (py < 0 || py >= VH) continue;
			let v = 0;
			for (let x = 0; x < TW; x += g) {
				const gx = Math.floor((it.bx + x - view.x0) / g);
				if (gx < 0 || gx >= GW) continue;
				if (occ[py * GW + gx]) {
					const gm = (g >= 32 ? 0xFFFFFFFF : (((1 << g) - 1) << x)) >>> 0;
					v |= gm;
				}
			}
			cov[r] = v >>> 0;
		}
		// байтовые операции рантайма: строка кадра занимает 32/g бит накопителя
		st.ops += 2 * it.box.h * Math.max(1, Math.ceil(TW / g / 8));
		// 2) отметить занятость своим кадром (группа целиком закрыта)
		for (let r = 0; r < TH; r++) {
			const my = MM(it)[r];
			if (!my) continue;
			const py = it.by + r - view.y0;
			if (py < 0 || py >= VH) continue;
			for (let x = 0; x < TW; x += g) {
				const gm = (g >= 32 ? 0xFFFFFFFF : (((1 << g) - 1) << x)) >>> 0;
				if (((my & gm) >>> 0) !== gm) continue;
				const gx = Math.floor((it.bx + x - view.x0) / g);
				if (gx < 0 || gx >= GW) continue;
				occ[py * GW + gx] = 1;
			}
		}
		if (!inside) continue;
		st.cells++;
		const dead = deadRows({ mask: MR(it) }, cov);
		const t = rowTrim(BX(it), dead);
		st.rows.bytes += BX(it).w * t.h;
		if (t.h) st.rows.blits++;
		st.trims.push(`${it.id}:${t.top}:${t.bot}:${t.h}`);
		// выбор дешевле: целый кадр со срезом строк либо две половины по 16 точек
		const hv = halves({ mask: MR(it) }, cov);
		let cb = cost(t.h ? 1 : 0, BX(it).w * t.h), bb = BX(it).w * t.h, bn = t.h ? 1 : 0;
		let hb = 0, hn = 0;
		for (const h of hv) if (h) { hn++; hb += h.w * h.h; }
		if (cost(hn, hb) < cb) { cb = cost(hn, hb); bb = hb; bn = hn; }
		st.best.blits += bn; st.best.bytes += bb;
		// тесные габариты (усечение и по столбцам — нужна копия кадра)
		const tb = tightBox({ mask: MR(it) }, cov);
		if (tb.w) { st.tight.blits++; st.tight.bytes += tb.w * tb.h; }
		const tk = `${it.id}:${tb.x}:${tb.y}:${tb.w}:${tb.h}`;
		if (!st.tightVar.has(tk)) st.tightVar.set(tk, (tb.x === BX(it).x && tb.w === BX(it).w) ? 0 : tb.w * tb.h);
	}
	return st;
}

// Проверка попиксельно для схемы «накопитель занятости»: честная картинка окна против
// картинки с усечением. Расхождений быть не должно.
function verifyPush(sc, view, g) {
	const VW = view.x1 - view.x0 + 1, VH = view.y1 - view.y0 + 1, GW = Math.ceil(VW / g);
	const a = new Uint8Array(VW * VH), b = new Uint8Array(VW * VH);
	const list = sc.inst.filter(it => {
		const x0 = it.bx + it.box.x, y0 = it.by + it.box.y;
		return x0 + it.box.w > view.x0 && x0 <= view.x1 && y0 + it.box.h > view.y0 && y0 <= view.y1;
	});
	const put = (buf, it, y0, y1) => {
		for (let r = y0; r <= y1; r++) {
			const py = it.by + r - view.y0;
			if (py < 0 || py >= VH) continue;
			for (let x = it.box.x; x < it.box.x + it.box.w; x++) {
				const v = it.img[r * TW + x];
				if (!v) continue;
				const px = it.bx + x - view.x0;
				if (px < 0 || px >= VW) continue;
				buf[py * VW + px] = v;
			}
		}
	};
	for (const it of list) put(a, it, it.box.y, it.box.y + it.box.h - 1);
	const occ = new Uint8Array(GW * VH), trim = new Map();
	for (let i = list.length - 1; i >= 0; i--) {
		const it = list[i];
		const cov = new Uint32Array(TH);
		for (let r = 0; r < TH; r++) {
			if (!it.mask[r]) continue;
			const py = it.by + r - view.y0;
			if (py < 0 || py >= VH) continue;
			let v = 0;
			for (let x = 0; x < TW; x += g) {
				const gx = Math.floor((it.bx + x - view.x0) / g);
				if (gx >= 0 && gx < GW && occ[py * GW + gx]) v |= ((g >= 32 ? 0xFFFFFFFF : ((1 << g) - 1) << x) >>> 0);
			}
			cov[r] = v >>> 0;
		}
		for (let r = 0; r < TH; r++) {
			const my = it.mask[r];
			if (!my) continue;
			const py = it.by + r - view.y0;
			if (py < 0 || py >= VH) continue;
			for (let x = 0; x < TW; x += g) {
				const gm = ((g >= 32 ? 0xFFFFFFFF : ((1 << g) - 1) << x)) >>> 0;
				if (((my & gm) >>> 0) !== gm) continue;
				const gx = Math.floor((it.bx + x - view.x0) / g);
				if (gx >= 0 && gx < GW) occ[py * GW + gx] = 1;
			}
		}
		trim.set(it, rowTrim(it.box, deadRows(it, cov)));
	}
	let dropped = 0, trimmed = 0;
	for (const it of list) {
		const t = trim.get(it);
		if (!t.h) { dropped++; continue; }
		if (t.top || t.bot) trimmed++;
		put(b, it, it.box.y + t.top, it.box.y + it.box.h - 1 - t.bot);
	}
	let diff = 0;
	for (let i = 0; i < a.length; i++) if (a[i] !== b[i]) diff++;
	return { diff, trimmed, dropped, a, b, W: VW, H: VH };
}

// ---------------------------------------------------------------- проверка попиксельно

function verify(sc, opt, subsetName) {
	const nb = neighbourhood(24);
	SUBSETS['своя+6+этаж выше'] = new Set([...SUBSETS['своя+6 (свой этаж)'], ...nb.filter(d => d.dz === 1).map(d => d.key)]);
	const subset = SUBSETS[subsetName];
	const W = sc.W, H = sc.H;
	const a = new Uint8Array(W * H), b = new Uint8Array(W * H);
	const put = (buf, it, y0, y1) => {
		for (let r = y0; r <= y1; r++) {
			const py = it.by + r;
			if (py < 0 || py >= H) continue;
			for (let x = it.box.x; x < it.box.x + it.box.w; x++) {
				const v = it.img[r * TW + x];
				if (!v) continue;
				const px = it.bx + x;
				if (px < 0 || px >= W) continue;
				buf[py * W + px] = v;
			}
		}
	};
	let trimmed = 0, dropped = 0;
	for (const it of sc.inst) {
		put(a, it, it.box.y, it.box.y + it.box.h - 1);
		if (it.x < 2 || it.y < 2 || it.x >= opt.sx - 2 || it.y >= opt.sy - 2) { put(b, it, it.box.y, it.box.y + it.box.h - 1); continue; }
		const cov = combine(coverContribs(sc, it, nb), subset, 1);
		const t = rowTrim(it.box, deadRows(it, cov));
		if (t.top || t.bot) trimmed++;
		if (!t.h) { dropped++; continue; }
		put(b, it, it.box.y + t.top, it.box.y + it.box.h - 1 - t.bot);
	}
	let diff = 0;
	for (let i = 0; i < a.length; i++) if (a[i] !== b[i]) diff++;
	return { diff, trimmed, dropped, a, b, W, H };
}

// ---------------------------------------------------------------- анимация кадров

// Усечение обязано быть верно для всех кадров анимации: у закрывающей записи берём
// пересечение масок Frame[0..7], у закрываемой — объединение
function animStats(map, sc) {
	const used = new Set();
	for (let i = 0; i < map.cells.length; i++) if (map.cells[i]) used.add(map.cells[i]);
	let anim = 0, animSame = 0;
	for (const id of used) {
		const r = sc.parts[id - 1];
		if (!r) continue;
		const f0 = r.frame[0];
		const diff = r.frame.some(f => f !== f0);
		if (!diff) { animSame++; continue; }
		anim++;
	}
	return { used: used.size, anim, animSame };
}

// ---------------------------------------------------------------- сценарий

function tilesetSize(map, sc) {
	const used = new Set();
	for (let i = 0; i < map.cells.length; i++) if (map.cells[i]) used.add(map.cells[i]);
	let bytes = 0;
	for (const id of used) {
		const img = sc.partFrame(id);
		if (!img) continue;
		const b = frameBox(img);
		bytes += b.w * b.h;
	}
	return { tiles: used.size, bytes, entries: used.size * 6 };
}

// путь к данным игры (в tools/proto/battle.js путь к UFO записан с ошибкой — там каталог
// «XCom UFO Defense», а не «X-COM UFO Defense»; battle.js по условию задачи не правим)
function gameRoot(game) {
	const cands = game === 'UFO'
		? ['Steam/XCom UFO Defense/XCOM', B.GAMES.UFO]
		: [B.GAMES.TFTD];
	for (const c of cands) if (fs.existsSync(c)) return c;
	return cands[0];
}

function runScenario(game, terrainName, opt) {
	const root = gameRoot(game);
	const rul = path.join('REF/OpenXcom/bin/standard', game === 'TFTD' ? 'xcom2' : 'xcom1', 'terrains.rul');
	const t = parseTerrains(rul).get(terrainName);
	if (!t) throw new Error('нет террейна ' + terrainName);
	const sets = loadSets(root, t.sets);
	const names = t.blocks.filter(b => b.w === 10 && b.l === 10).map(b => b.name);
	const map = buildField(root, names.length ? names : t.blocks.map(b => b.name), opt.blocks || 4);
	const level = map.sz - 1;
	const sc = makeScene(map, sets, level);
	const cx = Math.round(sc.W / 2), cy = Math.round(sc.H / 2);
	const view = { x0: cx - B.VIEW_W / 2, y0: cy - B.VIEW_H / 2, x1: cx + B.VIEW_W / 2 - 1, y1: cy + B.VIEW_H / 2 - 1 };
	const st = analyse(sc, { view, sx: map.sx, sy: map.sy });
	// схема «накопитель занятости» (весь набор соседей одним проходом)
	const costF = (n, b) => n * OVH_ASM + b * T_PER_BYTE;
	st.push = {};
	for (const g of [16, 8, 4, 1]) {
		const p = pushAnalyse(sc, view, { sx: map.sx, sy: map.sy, margin: 4 }, g, costF);
		st.push[g] = p;
		let extra = 0;
		for (const v of p.tightVar.values()) extra += v;
		st.schemes.push({ name: `накопитель занятости ${g === 1 ? 'по точкам' : 'по ' + g + ' px'}: срез строк`, mem: `маски кадров ${g === 1 ? 160 : 40 * 4 / g} Б/кадр`, blits: p.rows.blits, bytes: p.rows.bytes });
		st.schemes.push({ name: `накопитель ${g === 1 ? 'по точкам' : 'по ' + g + ' px'}: строки или половины`, mem: 'кадр хранится и целиком, и половинами', blits: p.best.blits, bytes: p.best.bytes });
		st.schemes.push({ name: `накопитель ${g === 1 ? 'по точкам' : 'по ' + g + ' px'}: тесные габариты`, mem: `+${(extra / 1024).toFixed(1)} КБ копий кадров`, blits: p.tight.blits, bytes: p.tight.bytes });
	}
	// то же, но с гарантией на все кадры анимации (закрывающий — пересечение кадров,
	// закрываемый — объединение): цена «безопасности» по анимации и дверям НЛО
	{
		const p = pushAnalyse(sc, view, { sx: map.sx, sy: map.sy, margin: 4 }, 1, costF, true);
		st.pushAnim = p;
		st.schemes.push({ name: 'накопитель по точкам, безопасно к анимации', mem: 'маски объединения и пересечения', blits: p.rows.blits, bytes: p.rows.bytes });
	}
	st.view = view;
	st.game = game; st.terrain = terrainName; st.map = `${map.sx}x${map.sy}x${map.sz}`; st.level = level;
	st.tileset = tilesetSize(map, sc);
	st.anim = animStats(map, sc);
	let pal = null;
	try { pal = B.readPalette(game, 6); } catch (e) { /* палитры нет — рисуем в оттенках серого */ }
	return { st, sc, map, pal, opt: { sx: map.sx, sy: map.sy } };
}

// ---------------------------------------------------------------- вывод

const pct = (a, b) => b ? (100 * a / b).toFixed(1) : '—';
const frames = (s, ovh) => (s.blits * ovh + s.bytes * T_PER_BYTE) / 286720;

function report(st) {
	const b0 = st.schemes[0];
	const L = [];
	L.push(`${st.game} ${st.terrain}: карта ${st.map}, уровень ${st.level}, частей клеток в окне 320x144: ${st.cells}`);
	L.push(`тайлсет: ${st.tileset.tiles} кадров, ${(st.tileset.bytes / 1024).toFixed(1)} КБ данных + ${st.tileset.entries} Б таблицы; ` +
		`анимированных записей ${st.anim.anim} из ${st.anim.used}`);
	L.push(`непрозрачных точек ${st.px}, закрыто позже нарисованным ${st.pxCov} (${pct(st.pxCov, st.px)} %)`);
	L.push('');
	L.push('схема                                             блитов     байт  %байт  кадров(asm/C)');
	for (const s of st.schemes)
		L.push(`${s.name.padEnd(48)} ${String(s.blits).padStart(5)} ${String(s.bytes).padStart(8)} ${pct(s.bytes, b0.bytes).padStart(6)}  ` +
			`${frames(s, OVH_ASM).toFixed(2)}/${frames(s, OVH_C).toFixed(2)}   ${s.mem}`);
	L.push('');
	let extra = 0;
	for (const v of st.tightVariants.values()) extra += v.extra;
	L.push(`вариантов (кадр, срез сверху, срез снизу): ${st.variants.size} (памяти не требуют); ` +
		`тесных вариантов: ${st.tightVariants.size}, из них копий данных на ${(extra / 1024).toFixed(1)} КБ ` +
		`(+${pct(extra, st.tileset.bytes)} % к тайлсету)`);
	L.push('цена накопителя занятости (байтовых операций на весь вид / на клетку / оценка T при 20 T на операцию): ' +
		Object.keys(st.push).map(g => `${g} px: ${st.push[g].ops} / ${(st.push[g].ops / Math.max(1, st.push[g].cells)).toFixed(0)} / ` +
			`${(st.push[g].ops * 20 / 286720).toFixed(2)} кадра`).join('; '));
	L.push(`таблицы конвертера: пар (кадр, сосед, смещение) ${st.pairs.size} -> ${(st.pairs.size * 1 / 1024).toFixed(1)} КБ; ` +
		`маски перекрытия 8 px: ${st.tileset.tiles} кадров x 7 смещений x 20 Б = ${(st.tileset.tiles * 7 * 20 / 1024).toFixed(1)} КБ`);
	L.push('что вышло:  ' + [...st.trimHist.entries()].sort((a, b) => b[1] - a[1]).map(([k, v]) => `${k}: ${v}`).join(', '));
	const parts = ['пол', 'зап.стена', 'сев.стена', 'объект'];
	L.push('по частям:  ' + parts.map((p, i) =>
		`${p} ${st.partCnt[i]} шт ${(st.partBytes[i] / 1024).toFixed(1)}→${(st.partBytesRow[i] / 1024).toFixed(1)} КБ (исчезло ${st.dropped[i]})`).join(', '));
	const off = [...st.byOffset.entries()].sort((a, b) => b[1] - a[1]).slice(0, 8);
	L.push('вклад соседей (доля закрытых точек):  ' + off.map(([k, v]) => `${k}: ${pct(v, st.px)}%`).join('  '));
	return L.join('\n');
}

// ---------------------------------------------------------------- main

const outDir = arg('out', 'tmp/proto/opt2');
fs.mkdirSync(outDir, { recursive: true });

const SCEN = hasFlag('all')
	? [['TFTD', 'SEABED'], ['TFTD', 'CORAL'], ['TFTD', 'VOLC'], ['TFTD', 'PORT'], ['TFTD', 'ALART'],
	   ['TFTD', 'XBASES'], ['TFTD', 'CARGO'], ['TFTD', 'ISLAND'],
	   ['UFO', 'CULTA'], ['UFO', 'DESERT'], ['UFO', 'FOREST'], ['UFO', 'JUNGLE'],
	   ['UFO', 'URBAN'], ['UFO', 'UBASE'], ['UFO', 'XBASE'], ['UFO', 'MARS'], ['UFO', 'POLAR']]
	: [[arg('game', 'TFTD'), arg('terrain', 'SEABED')]];

const all = [];
for (const [game, terr] of SCEN) {
	let r;
	try {
		r = runScenario(game, terr, { blocks: parseInt(arg('blocks', '4'), 10) });
	} catch (e) {
		console.log(`${game} ${terr}: пропущено (${e.message})`);
		continue;
	}
	console.log(report(r.st));
	if (hasFlag('verify')) {
		const v = verify(r.sc, r.opt, arg('subset', 'своя+6 (свой этаж)'));
		console.log(`проверка (соседи, вся карта): расхождений ${v.diff}, усечено ${v.trimmed}, исчезло ${v.dropped}`);
		r.st.verifyDiff = v.diff;
		for (const g of [1, 8, 16]) {
			const p = verifyPush(r.sc, r.st.view, g);
			console.log(`проверка (накопитель ${g} px, окно): расхождений ${p.diff}, усечено ${p.trimmed}, исчезло ${p.dropped}`);
			r.st['verifyPush' + g] = p.diff;
			if (hasFlag('png') && g === 8) {
				const pal = r.pal;
				for (const [nm, bufr] of [['honest', p.a], ['trim', p.b]]) {
					const rgb = Buffer.alloc(p.W * p.H * 3);
					for (let i = 0; i < p.W * p.H; i++) {
						const c = pal ? pal[bufr[i]] : [bufr[i], bufr[i], bufr[i]];
						rgb[i * 3] = c[0]; rgb[i * 3 + 1] = c[1]; rgb[i * 3 + 2] = c[2];
					}
					B.writePng(path.join(outDir, `${game}_${terr}_${nm}.png`), p.W, p.H, rgb);
				}
			}
		}
	}
	console.log('');
	all.push(r.st);
}

if (all.length > 1) {
	console.log('СВОДКА (сумма по сценариям, % от «как сейчас»)');
	const n = all[0].schemes.length;
	for (let i = 0; i < n; i++) {
		let blits = 0, bytes = 0, b0 = 0;
		for (const s of all) { blits += s.schemes[i].blits; bytes += s.schemes[i].bytes; b0 += s.schemes[0].bytes; }
		const bl0 = all.reduce((a, s) => a + s.schemes[0].blits, 0);
		console.log(`${all[0].schemes[i].name.padEnd(48)} блитов ${pct(blits, bl0).padStart(6)} %  байт ${pct(bytes, b0).padStart(6)} %`);
	}
}

// краткая сводка для отчёта
if (all.length > 1) {
	const f = (s, n) => s.schemes.find(x => x.name === n);
	const S = (n, k) => all.reduce((x, s) => x + f(s, n)[k], 0), S0 = (k) => all.reduce((x, s) => x + s.schemes[0][k], 0);
	const L = ['Тема 2: усечённые варианты кадров тайлов — сводка (tools/proto/opt2_variants.js --all)',
		'Окно карты 320x144, поле 4x4 блока, камера в центре, уровни 0..верхний.', '',
		'террейн        карта       частей     байт |  своя   +3сос  всё8px  всё точно | блитов'];
	for (const s of all) {
		const b0 = s.schemes[0];
		const P = (n) => ((100 * f(s, n).bytes / b0.bytes).toFixed(0) + '%').padStart(7);
		L.push((s.game + ' ' + s.terrain).padEnd(14) + s.map.padEnd(11) + String(s.cells).padStart(6) + String(b0.bytes).padStart(9) + ' ' +
			P('строки: своя клетка, точность точно') + P('строки: своя+3 ближних, точность точно') +
			P('накопитель занятости по 8 px: срез строк') + P('накопитель занятости по точкам: срез строк') +
			((100 * f(s, 'накопитель занятости по точкам: срез строк').blits / b0.blits).toFixed(0) + '%').padStart(9));
	}
	L.push('', 'ИТОГО (сумма сценариев), % от «полный кадр»:');
	for (const n of all[0].schemes.map(x => x.name).slice(1))
		L.push('  ' + n.padEnd(52) + 'байт ' + ((100 * S(n, 'bytes') / S0('bytes')).toFixed(1) + '%').padStart(7) +
			'   блитов ' + ((100 * S(n, 'blits') / S0('blits')).toFixed(1) + '%').padStart(7));
	fs.writeFileSync(path.join(outDir, 'summary.txt'), L.join('\n') + '\n');
}

fs.writeFileSync(path.join(outDir, 'stats.json'), JSON.stringify(all.map(s => ({
	game: s.game, terrain: s.terrain, map: s.map, cells: s.cells, px: s.px, pxCov: s.pxCov,
	tileset: s.tileset, anim: s.anim, verifyDiff: s.verifyDiff,
	schemes: s.schemes, variants: s.variants.size, tightVariants: s.tightVariants.size, pairs: s.pairs.size,
	pushTrims: s.push[1].trims, pushOps: Object.fromEntries(Object.keys(s.push).map(g => [g, s.push[g].ops])),
	trimHist: [...s.trimHist], byOffset: [...s.byOffset], partBytes: s.partBytes, partBytesRow: s.partBytesRow,
	dropped: s.dropped, partCnt: s.partCnt,
})), null, 1));
console.log('\nчисла — ' + path.join(outDir, 'stats.json'));
