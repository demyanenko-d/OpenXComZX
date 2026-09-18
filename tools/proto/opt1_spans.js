// Прототип №1 к 16_battlescape_plan.md §8.3a: отсев не клеток целиком, а ЧАСТЕЙ спрайтов —
// спан-буфер по строкам экрана (как visplane в Doom). Считает, сколько байт DMA и сколько
// запусков DMA стоит каждый способ, и проверяет картинку попиксельно с эталоном.
//
//   node tools/proto/opt1_spans.js                      # весь набор карт
//   node tools/proto/opt1_spans.js --only TFTD:ALART    # одна карта
//   node tools/proto/opt1_spans.js --png                # + картинки в tmp/proto/opt1/
//
// Модель железа (02 §6, src/battlescape/scr_battle.c):
//  * кадр тайла в SPRSET уже обрезан по непрозрачным пикселям, x0 и ширина чётные;
//  * блит = запуск BLT1 с D_ALGN: пачка = строка, пачек = строк. Источник идёт линейно,
//    поэтому МНОГОСТРОЧНЫЙ запуск возможен только при полной ширине кадра; любая обрезка
//    по X — отдельный запуск на каждую строку;
//  * экран 512 байт/строка, страница = 32 строки — многострочный запуск рвётся на границе;
//  * DMA адресует словами: спан выравнивается на 2 пикселя.
'use strict';
const fs = require('fs');
const path = require('path');
const B = require('./battle');

const W = B.VIEW_W, H = B.VIEW_H;          // окно карты 320x144
const TW = B.TILE_W, TH = B.TILE_H;

// --- стоимости (такты Z80 14 МГц)
const T_FRAME = 286720;                    // кадр 50 Гц
const DMA_FRAME = 74 * 1024;               // байт BLT1 за кадр при простое CPU (02 §6)
const T_RUN = 300;                         // настройка одного запуска DMA (замер §8.3b)
const T_PART = 250;                        // накладные на часть клетки (цель на ассемблере)
const T_COL = 30;                          // обращение к одной колонке горизонта
const T_ROW = 50;                          // вход в строку при работе со списком отрезков
const T_IVL = 70;                          // просмотр/правка одного отрезка списка

function arg(name, def) {
	const i = process.argv.indexOf('--' + name);
	return i > 0 && i + 1 < process.argv.length ? process.argv[i + 1] : def;
}
const wantPng = process.argv.includes('--png');
const only = arg('only', null);

const RUL = {
	TFTD: 'REF/OpenXcom/bin/standard/xcom2/terrains.rul',
	UFO: 'REF/OpenXcom/bin/standard/xcom1/terrains.rul',
};
// battle.js GAMES.UFO указывает на несуществующий каталог (правка чужого файла запрещена)
const DIR = { TFTD: B.GAMES.TFTD, UFO: 'Steam/XCom UFO Defense/XCOM' };

// ---------------------------------------------------------------- террейн из правил OpenXcom

function loadTerrain(game, name) {
	const lines = fs.readFileSync(RUL[game], 'utf8').split(/\r?\n/);
	let inside = false, sec = null, res = null, blk = null;
	for (const ln of lines) {
		let m = /^  - name: (\S+)/.exec(ln);
		if (m) {
			if (res) break;
			inside = m[1] === name;
			if (inside) res = { sets: [], blocks: [] };
			sec = null;
			continue;
		}
		if (!inside) continue;
		m = /^    (\w+):/.exec(ln);
		if (m) { sec = m[1]; continue; }
		if (sec === 'mapDataSets' && (m = /^      - (\S+)/.exec(ln))) res.sets.push(m[1]);
		if (sec === 'mapBlocks') {
			if ((m = /^      - name: (\S+)/.exec(ln))) { blk = { name: m[1], w: 10, l: 10 }; res.blocks.push(blk); }
			else if (blk && (m = /^        width: (\d+)/.exec(ln))) blk.w = +m[1];
			else if (blk && (m = /^        length: (\d+)/.exec(ln))) blk.l = +m[1];
		}
	}
	if (!res) throw new Error('terrain ' + name + ' not found');
	return res;
}

function loadSets(game, sets) {
	const dir = path.join(DIR[game], 'TERRAIN');
	const parts = [], frames = [];
	for (const s of sets) {
		const mcd = B.readMcd(fs.readFileSync(path.join(dir, s + '.MCD')));
		const pck = B.readPck(fs.readFileSync(path.join(dir, s + '.PCK')), fs.readFileSync(path.join(dir, s + '.TAB')));
		const base = frames.length;
		for (const r of mcd) { r.base = base; parts.push(r); }
		for (const f of pck) frames.push(f);
	}
	return { parts, frames };
}

// Поле: n x n блоков 10x10 террейна (как замащивает mapScript), детерминированно
function buildField(game, terr, n, seed) {
	const dir = path.join(DIR[game], 'MAPS');
	const have = terr.blocks.filter(b => fs.existsSync(path.join(dir, b.name + '.MAP')));
	let small = have.filter(b => b.w === 10 && b.l === 10);
	let step = 10;
	if (!small.length) {                        // террейны из одного большого блока (трюм корабля)
		const w = Math.min(...have.map(b => b.w)), l = Math.min(...have.map(b => b.l));
		step = Math.min(w, l);
		small = have.filter(b => b.w === step && b.l === step);
		n = Math.max(1, Math.round(50 / step));
	}
	if (!small.length) {                        // совсем неквадратные блоки — берём один крупнейший
		const b = have.reduce((a, c) => (c.w * c.l > a.w * a.l ? c : a), have[0]);
		const m = B.readMap(fs.readFileSync(path.join(dir, b.name + '.MAP')));
		return m;
	}
	const loaded = small.map(b => B.readMap(fs.readFileSync(path.join(dir, b.name + '.MAP'))));
	const sx = step * n, sy = step * n;
	let sz = 0;
	for (const b of loaded) if (b.sz > sz) sz = b.sz;
	const cells = new Uint8Array(sx * sy * sz * 4);
	let r = seed >>> 0;
	const rnd = () => (r = (Math.imul(r, 1103515245) + 12345) >>> 0) >>> 16;
	for (let by = 0; by < n; by++)
		for (let bx = 0; bx < n; bx++) {
			const b = loaded[rnd() % loaded.length];
			for (let z = 0; z < b.sz; z++)
				for (let y = 0; y < b.sy; y++)
					for (let x = 0; x < b.sx; x++) {
						const from = ((z * b.sy + y) * b.sx + x) * 4;
						const to = ((z * sy + by * step + y) * sx + bx * step + x) * 4;
						for (let k = 0; k < 4; k++) cells[to + k] = b.cells[from + k];
					}
		}
	return { sx, sy, sz, cells };
}

// ---------------------------------------------------------------- разбор кадра (как SPRSET)

const infoCache = new Map();
function frameInfo(img) {
	if (infoCache.has(img)) return infoCache.get(img);
	let x0 = TW, y0 = TH, x1 = -1, y1 = -1;
	for (let y = 0; y < TH; y++)
		for (let x = 0; x < TW; x++)
			if (img[y * TW + x]) {
				if (x < x0) x0 = x;
				if (x > x1) x1 = x;
				if (y < y0) y0 = y;
				if (y > y1) y1 = y;
			}
	let info;
	if (x1 < 0) info = { empty: true, w: 0, h: 0 };
	else {
		x0 &= ~1;
		let w = x1 - x0 + 1;
		if (w & 1) w++;
		if (x0 + w > TW) x0 = TW - w;
		const h = y1 - y0 + 1;
		const runs = [];                       // на строку кадра — пары [a,b) в координатах кадра
		let px = 0;
		for (let y = 0; y < h; y++) {
			const rr = [];
			let a = -1;
			for (let x = 0; x < w; x++) {
				const v = img[(y0 + y) * TW + x0 + x];
				if (v) { if (a < 0) a = x; px++; }
				else if (a >= 0) { rr.push(a, x); a = -1; }
			}
			if (a >= 0) rr.push(a, w);
			runs.push(rr);
		}
		const colTop = new Int16Array(w).fill(32767);
		const colRunTop = new Int16Array(w).fill(32767);
		const colBot = new Int16Array(w).fill(-1);
		for (let x = 0; x < w; x++) {
			for (let y = 0; y < h; y++) if (img[(y0 + y) * TW + x0 + x]) { colTop[x] = y; break; }
			for (let y = h - 1; y >= 0; y--) if (img[(y0 + y) * TW + x0 + x]) { colBot[x] = y; break; }
			if (colBot[x] >= 0) {                // верх нижнего сплошного отрезка колонки
				let y = colBot[x];
				while (y > 0 && img[(y0 + y - 1) * TW + x0 + x]) y--;
				colRunTop[x] = y;
			}
		}
		info = { empty: false, x0, y0, w, h, runs, colTop, colRunTop, colBot, px };
	}
	infoCache.set(img, info);
	return info;
}

// ---------------------------------------------------------------- комнаты (разметка конвертера)

// «Комната» — связная область клеток, у которых есть пол на своём уровне и пол (потолок) сверху.
// Такую область снаружи не видно, если камера выше. Возвращает маску по клеткам и статистику.
function findRooms(map, data) {
	const mask = new Uint8Array(map.sx * map.sy * map.sz);
	const id = new Int32Array(map.sx * map.sy * map.sz).fill(-1);
	const floorAt = (x, y, z) => {
		if (z >= map.sz) return false;
		const p = map.cells[((z * map.sy + y) * map.sx + x) * 4];
		if (!p) return false;
		const rec = data.parts[p - 1];
		return !(rec && rec.noFloor);
	};
	for (let z = 0; z < map.sz; z++)
		for (let y = 0; y < map.sy; y++)
			for (let x = 0; x < map.sx; x++)
				if (floorAt(x, y, z) && floorAt(x, y, z + 1)) mask[(z * map.sy + y) * map.sx + x] = 1;
	const rooms = [];
	for (let z = 0; z < map.sz; z++)
		for (let y = 0; y < map.sy; y++)
			for (let x = 0; x < map.sx; x++) {
				const o = (z * map.sy + y) * map.sx + x;
				if (!mask[o] || id[o] >= 0) continue;
				const r = rooms.length;
				let n = 0;
				const st = [o];
				id[o] = r;
				while (st.length) {
					const c = st.pop();
					n++;
					const cx = c % map.sx, cy = ((c / map.sx) | 0) % map.sy, cz = (c / (map.sx * map.sy)) | 0;
					for (const [dx, dy] of [[1, 0], [-1, 0], [0, 1], [0, -1]]) {
						const nx = cx + dx, ny = cy + dy;
						if (nx < 0 || ny < 0 || nx >= map.sx || ny >= map.sy) continue;
						const no = (cz * map.sy + ny) * map.sx + nx;
						if (mask[no] && id[no] < 0) { id[no] = r; st.push(no); }
					}
				}
				rooms.push({ z, n });
			}
	// «глубокая» клетка комнаты: ближние по экрану соседи тоже в комнате — тогда её не видно и сбоку
	const deep = new Uint8Array(mask.length);
	for (let z = 0; z < map.sz; z++)
		for (let y = 0; y < map.sy; y++)
			for (let x = 0; x < map.sx; x++) {
				const o = (z * map.sy + y) * map.sx + x;
				if (!mask[o]) continue;
				const nx = Math.min(x + 1, map.sx - 1), ny = Math.min(y + 1, map.sy - 1);
				if (mask[(z * map.sy + y) * map.sx + nx] && mask[(z * map.sy + ny) * map.sx + x]
					&& mask[(z * map.sy + ny) * map.sx + nx]) deep[o] = 1;
			}
	return { mask, deep, rooms };
}

// ---------------------------------------------------------------- сцена

function buildScene(map, data, level, ox, oy, rooms) {
	const list = [];
	for (let z = 0; z <= level; z++)
		for (let x = 0; x < map.sx; x++)
			for (let y = 0; y < map.sy; y++) {
				const o = ((z * map.sy + y) * map.sx + x) * 4;
				const cx = (x - y) * B.HALF_W + ox, cy = (x + y) * B.QUART - z * B.LEVEL_H + oy;
				if (cx <= -TW || cx >= W || cy <= -TH * 2 || cy >= H + TH) continue;
				for (let k = 0; k < 4; k++) {
					const p = map.cells[o + k];
					if (!p) continue;
					const rec = data.parts[p - 1];
					if (!rec) continue;
					const img = data.frames[rec.base + rec.frame[0]];
					if (!img) continue;
					const info = frameInfo(img);
					if (info.empty) continue;
					const sx = cx + info.x0, sy = cy - rec.pLevel + info.y0;
					if (sx + info.w <= 0 || sx >= W || sy + info.h <= 0 || sy >= H) continue;
					list.push({
						sx, sy, info, img, kind: k, z, tx: x, ty: y,
						solid: k === 0 && info.w >= 28 && info.h >= 14,
						inRoom: z < level && rooms && rooms.mask[(z * map.sy + y) * map.sx + x] === 1,
						inRoomDeep: z < level && rooms && rooms.deep[(z * map.sy + y) * map.sx + x] === 1,
					});
				}
			}
	return list;                                // порядок художника: ближние — в конце
}

// ---------------------------------------------------------------- учёт DMA

// rows[i] — список пар [a,b) в экранных координатах для строки кадра i (i = y - sy).
function account(part, rows, st) {
	const { sx, info } = part;
	const full = sx >= 0 && sx + info.w <= W;
	let i = 0;
	while (i < info.h) {
		const rr = rows[i];
		if (!rr || !rr.length) { i++; continue; }
		const isFull = full && rr.length === 2 && rr[0] === sx && rr[1] === sx + info.w;
		if (isFull) {
			const pageEnd = ((part.sy + i) | 31) + 1 - part.sy;   // страница экрана = 32 строки
			let j = i;
			while (j < info.h && j < pageEnd && rows[j] && rows[j].length === 2
				&& rows[j][0] === sx && rows[j][1] === sx + info.w) j++;
			st.runs++;
			st.bytes += info.w * (j - i);
			i = j;
		} else {
			for (let k = 0; k < rr.length; k += 2) { st.runs++; st.bytes += rr[k + 1] - rr[k]; }
			i++;
		}
	}
}

function paint(buf, part, rows) {
	const { sx, sy, img, info } = part;
	for (let i = 0; i < info.h; i++) {
		const rr = rows[i];
		if (!rr) continue;
		const yy = sy + i;
		if (yy < 0 || yy >= H) continue;
		for (let k = 0; k < rr.length; k += 2)
			for (let x = rr[k]; x < rr[k + 1]; x++) {
				const v = img[(info.y0 + i) * TW + info.x0 + (x - sx)];
				if (v) buf[yy * W + x] = v;
			}
	}
}

// Полные строки части, обрезанные окном («как сейчас», scr_battle.c:blit_tile)
function baseRows(part) {
	const { sx, sy, info } = part;
	const a = Math.max(0, sx) & ~1, b = Math.min(W, sx + info.w + 1) & ~1;
	const rows = new Array(info.h);
	if (b <= a) return rows;
	for (let i = 0; i < info.h; i++) {
		const yy = sy + i;
		if (yy < 0 || yy >= H) continue;
		rows[i] = [a, b];
	}
	return rows;
}

// ---------------------------------------------------------------- занятость: список отрезков

function newCov() { const c = new Array(H); for (let y = 0; y < H; y++) c[y] = []; return c; }

function covFree(cov, y, a, b, out, ops) {     // свободные куски [a,b)
	const l = cov[y];
	let cur = a;
	for (let i = 0; i < l.length; i += 2) {
		ops.ivl++;
		const s = l[i], e = l[i + 1];
		if (e <= cur) continue;
		if (s >= b) break;
		if (s > cur) out.push(cur, Math.min(s, b));
		if (e > cur) cur = e;
		if (cur >= b) return;
	}
	if (cur < b) out.push(cur, b);
}

function covAdd(cov, y, a, b, ops, cap) {
	const l = cov[y];
	let i = 0;
	while (i < l.length && l[i + 1] < a) { i += 2; ops.ivl++; }
	let s = a, e = b, j = i;
	while (j < l.length && l[j] <= e) { ops.ivl++; if (l[j] < s) s = l[j]; if (l[j + 1] > e) e = l[j + 1]; j += 2; }
	l.splice(i, j - i, s, e);
	if (cap && l.length / 2 > cap) {            // лишние отрезки выбрасываем (занятость — подмножество)
		let mi = 0, ml = 1 << 30;
		for (let k = 0; k < l.length; k += 2) { const len = l[k + 1] - l[k]; if (len < ml) { ml = len; mi = k; } }
		l.splice(mi, 2);
	}
}

// ---------------------------------------------------------------- проход 1: занятость отрезками

// Идём от ближних к дальним, копим занятость, для каждой части запоминаем ВИДИМЫЕ пиксели.
// cap = Infinity — точная занятость; cap = K — не больше K отрезков на строку (дешевле, отсев слабее).
// onlySolid — гибрид (патент US6069633 «Sprite engine»): занятость наращивают только заведомо
// непрозрачные части (сплошные ромбы полов/крыш), а обрезаются по ней все.
function coverPass(scene, cap, onlySolid) {
	const cov = newCov();
	const ops = { ivl: 0, row: 0, part: 0 };
	const vis = new Array(scene.length);
	const capN = cap === Infinity ? 0 : cap;
	for (let n = scene.length - 1; n >= 0; n--) {
		const p = scene[n];
		const { sx, sy, info } = p;
		const rows = new Array(info.h);
		let any = false, pxv = 0;
		ops.part++;
		for (let i = 0; i < info.h; i++) {
			const yy = sy + i;
			if (yy < 0 || yy >= H) continue;
			const rr = info.runs[i];
			if (!rr.length) continue;
			ops.row++;
			let out = null;
			for (let k = 0; k < rr.length; k += 2) {
				const a = Math.max(0, sx + rr[k]), b = Math.min(W, sx + rr[k + 1]);
				if (b <= a) continue;
				const tmp = [];
				covFree(cov, yy, a, b, tmp, ops);
				if (tmp.length) { if (!out) out = []; for (let q = 0; q < tmp.length; q++) out.push(tmp[q]); }
			}
			if (out) { rows[i] = out; any = true; for (let k = 0; k < out.length; k += 2) pxv += out[k + 1] - out[k]; }
		}
		vis[n] = any ? { rows, px: pxv } : null;
		if (!any) continue;
		if (onlySolid && !p.solid) continue;
		for (let i = 0; i < info.h; i++) {
			const yy = sy + i;
			if (yy < 0 || yy >= H) continue;
			const rr = info.runs[i];
			for (let k = 0; k < rr.length; k += 2) {
				const a = Math.max(0, sx + rr[k]), b = Math.min(W, sx + rr[k + 1]);
				if (b > a) covAdd(cov, yy, a, b, ops, capN);
			}
		}
	}
	return { vis, ops };
}

// ---------------------------------------------------------------- проход 1: горизонт по колонкам

// top[g] — верхняя строка, начиная с которой группа колонок g занята ДО НИЗА окна (гарантия).
// Часть невидима, если каждая её колонка закрыта с самого верха. У видимой части отрезаются
// нижние строки: строки >= limit закрыты во всех колонках.
function horizonPass(scene, group, floorsOnly) {
	const nG = Math.ceil(W / group);
	const top = new Int16Array(nG).fill(H);
	const ops = { col: 0, part: 0 };
	const plan = new Array(scene.length);
	for (let n = scene.length - 1; n >= 0; n--) {
		const p = scene[n];
		const { sx, sy, info } = p;
		ops.part++;
		let limit = -1;                          // строки [0, limit) рисуем
		for (let c = 0; c < info.w; c++) {
			if (info.colTop[c] === 32767) continue;
			const px = sx + c;
			if (px < 0 || px >= W) continue;
			ops.col++;
			const t = top[(px / group) | 0] - sy;
			if (t > limit) limit = t;
		}
		if (limit > info.h) limit = info.h;
		plan[n] = limit > 0 ? limit : 0;
		if (limit <= 0) continue;
		if (floorsOnly && !p.solid) continue;
		if (group === 1) {
			for (let c = 0; c < info.w; c++) {
				if (info.colBot[c] < 0) continue;
				const px = sx + c;
				if (px < 0 || px >= W) continue;
				ops.col++;
				if (sy + info.colBot[c] >= top[px] - 1 && sy + info.colRunTop[c] < top[px]) top[px] = sy + info.colRunTop[c];
			}
		} else {
			const g0 = Math.max(0, Math.ceil(sx / group)), g1 = Math.min(nG, Math.floor((sx + info.w) / group));
			for (let g = g0; g < g1; g++) {      // группа целиком внутри кадра
				ops.col++;
				let rt = -32768, bt = 32767, ok = true;
				for (let c = g * group - sx; c < g * group - sx + group; c++) {
					if (info.colBot[c] < 0) { ok = false; break; }
					if (info.colRunTop[c] > rt) rt = info.colRunTop[c];
					if (info.colBot[c] < bt) bt = info.colBot[c];
				}
				if (!ok) continue;
				if (sy + bt >= top[g] - 1 && sy + rt < top[g]) top[g] = sy + rt;
			}
		}
	}
	return { plan, ops };
}

// ---------------------------------------------------------------- построение спанов для прохода 2

function alignSpans(rr, gap) {                 // выравнивание на 2 пикселя + склейка дырок <= gap
	const out = [];
	for (let k = 0; k < rr.length; k += 2) {
		const a = rr[k] & ~1, b = (rr[k + 1] + 1) & ~1;
		if (out.length && a - out[out.length - 1] <= gap) { if (b > out[out.length - 1]) out[out.length - 1] = b; }
		else out.push(a, b);
	}
	return out;
}

function rowsFromVis(part, v, mode, gap) {
	const { sx, info } = part;
	const rows = new Array(info.h);
	const a0 = Math.max(0, sx) & ~1, b0 = Math.min(W, sx + info.w + 1) & ~1;
	for (let i = 0; i < info.h; i++) {
		const rr = v.rows[i];
		if (!rr) continue;
		if (mode === 'row') rows[i] = [a0, b0];
		else rows[i] = alignSpans(rr, gap);
	}
	return rows;
}

function rowsFromLimit(part, limit, clip) {
	const { sx, sy, info } = part;
	const rows = new Array(info.h);
	const a0 = Math.max(0, sx) & ~1, b0 = Math.min(W, sx + info.w + 1) & ~1;
	if (b0 <= a0) return rows;
	for (let i = 0; i < Math.min(limit, info.h); i++) {
		const yy = sy + i;
		if (yy < 0 || yy >= H) continue;
		if (!info.runs[i].length) continue;
		rows[i] = [a0, b0];
	}
	return rows;
}

// ---------------------------------------------------------------- прогон одной сцены

const KEEP = new Map();
function runScene(scene, ref) {
	const res = [];
	const cmp = (buf, name) => {
		let lost = 0, extra = 0;
		for (let i = 0; i < ref.length; i++) {
			if (ref[i] && ref[i] !== buf[i]) lost++;
			else if (!ref[i] && buf[i]) extra++;
		}
		if (name && KEEP.has(name)) KEEP.set(name, buf);
		return { lost, extra };
	};

	// 0. как сейчас
	{
		const st = { runs: 0, bytes: 0, parts: 0 };
		const buf = new Uint8Array(W * H);
		for (const p of scene) { const r = baseRows(p); st.parts++; account(p, r, st); paint(buf, p, r); }
		res.push(Object.assign({ name: 'base (как сейчас)', talg: 0 }, st, cmp(buf)));
	}

	// 1. точная занятость отрезками, разные способы использования
	const ex = coverPass(scene, Infinity);
	const talgExact = ex.ops.row * T_ROW + ex.ops.ivl * T_IVL;
	for (const [name, mode, gap] of [
		['spans g0 (идеал байт)', 'span', 0],
		['spans g8', 'span', 8],
		['spans g16', 'span', 16],
		['rows (строки целиком)', 'row', 0],
	]) {
		const st = { runs: 0, bytes: 0, parts: 0 };
		const buf = new Uint8Array(W * H);
		for (let n = 0; n < scene.length; n++) {
			const v = ex.vis[n];
			if (!v) continue;
			const r = rowsFromVis(scene[n], v, mode, gap);
			st.parts++; account(scene[n], r, st); paint(buf, scene[n], r);
		}
		res.push(Object.assign({ name, talg: talgExact }, st, cmp(buf, name)));
	}
	// 1a. отсев только целых частей (эталон §8.3a, «идеальный по клеткам»)
	{
		const st = { runs: 0, bytes: 0, parts: 0 };
		const buf = new Uint8Array(W * H);
		for (let n = 0; n < scene.length; n++) {
			if (!ex.vis[n]) continue;
			const r = baseRows(scene[n]);
			st.parts++; account(scene[n], r, st); paint(buf, scene[n], r);
		}
		res.push(Object.assign({ name: 'parts (часть целиком)', talg: talgExact }, st, cmp(buf)));
	}
	// 1b. порог видимости (с потерями)
	for (const thr of [0.1, 0.25]) {
		const st = { runs: 0, bytes: 0, parts: 0 };
		const buf = new Uint8Array(W * H);
		for (let n = 0; n < scene.length; n++) {
			const v = ex.vis[n];
			if (!v || v.px < scene[n].info.px * thr) continue;
			const r = baseRows(scene[n]);
			st.parts++; account(scene[n], r, st); paint(buf, scene[n], r);
		}
		res.push(Object.assign({ name: `part >= ${thr * 100}% (с потерями)`, talg: talgExact }, st, cmp(buf)));
	}

	// 1c. гибрид: занятость ведут только сплошные полы/крыши, обрезаются по ней все части
	{
		const hy = coverPass(scene, Infinity, true);
		const talg = hy.ops.row * T_ROW + hy.ops.ivl * T_IVL;
		for (const [suffix, mode, gap] of [[' spans', 'span', 8], [' rows', 'row', 0], [' parts', 'part', 0]]) {
			const st = { runs: 0, bytes: 0, parts: 0 };
			const buf = new Uint8Array(W * H);
			for (let n = 0; n < scene.length; n++) {
				const v = hy.vis[n];
				if (!v) continue;
				const r = mode === 'part' ? baseRows(scene[n]) : rowsFromVis(scene[n], v, mode, gap);
				st.parts++; account(scene[n], r, st); paint(buf, scene[n], r);
			}
			res.push(Object.assign({ name: `гибрид полы${suffix}`, talg }, st, cmp(buf)));
		}
	}

	// 1d. отсев по комнатам (разметка flood fill в конвертере): клетка внутри крытой комнаты
	// ниже уровня камеры не рисуется вовсе; стоимость на Z80 — чтение флага клетки
	for (const [name, fld] of [['комнаты (flood fill)', 'inRoom'], ['комнаты, глубокие', 'inRoomDeep']]) {
		const st = { runs: 0, bytes: 0, parts: 0 };
		const buf = new Uint8Array(W * H);
		for (let n = 0; n < scene.length; n++) {
			if (scene[n][fld]) continue;
			const r = baseRows(scene[n]);
			st.parts++; account(scene[n], r, st); paint(buf, scene[n], r);
		}
		res.push(Object.assign({ name, talg: scene.length * 40 }, st, cmp(buf)));
	}

	// 2. занятость с ограничением числа отрезков на строку
	for (const cap of [1, 2, 4]) {
		const c = coverPass(scene, cap);
		const talg = c.ops.row * T_ROW + c.ops.ivl * T_IVL;
		for (const [suffix, mode, gap] of [[' spans', 'span', 8], [' rows', 'row', 0]]) {
			const st = { runs: 0, bytes: 0, parts: 0 };
			const buf = new Uint8Array(W * H);
			for (let n = 0; n < scene.length; n++) {
				const v = c.vis[n];
				if (!v) continue;
				const r = rowsFromVis(scene[n], v, mode, gap);
				st.parts++; account(scene[n], r, st); paint(buf, scene[n], r);
			}
			res.push(Object.assign({ name: `cov<=${cap}${suffix}`, talg }, st, cmp(buf)));
		}
	}

	// 3. горизонт по колонкам / группам колонок
	for (const [name, group, floors] of [
		['hz col', 1, false], ['hz col (полы)', 1, true], ['hz grp4', 4, false], ['hz grp8', 8, false]]) {
		const hz = horizonPass(scene, group, floors);
		const talg = hz.ops.col * T_COL;
		const st = { runs: 0, bytes: 0, parts: 0 };
		const buf = new Uint8Array(W * H);
		for (let n = 0; n < scene.length; n++) {
			const lim = hz.plan[n];
			if (!lim) continue;
			const r = rowsFromLimit(scene[n], lim, false);
			st.parts++; account(scene[n], r, st); paint(buf, scene[n], r);
		}
		res.push(Object.assign({ name, talg }, st, cmp(buf, name)));
		// тот же отсев, но горизонт посчитан заранее (таблица среза на уровень, см. отчёт)
		if (group === 1 && !floors) res.push(Object.assign({ name: 'hz col (таблица)', talg: scene.length * 40 }, st, cmp(buf)));
	}
	return { res, exVis: ex.vis };
}

// ---------------------------------------------------------------- обратный порядок (строгий)

// Рисуем от ближних к дальним одним проходом: блитить можно только туда, где ничего не нарисовано.
// Спан выравнен на 2 пикселя, поэтому ячейка, где непрозрачная точка источника попадает на уже
// занятую точку, блиту недоступна — считаем такие потери.
function revPass(scene, ref, gap) {
	const cov = newCov();
	const ops = { ivl: 0, row: 0 };
	const st = { runs: 0, bytes: 0, parts: 0 };
	const buf = new Uint8Array(W * H);
	let lostAlign = 0;
	for (let n = scene.length - 1; n >= 0; n--) {
		const p = scene[n];
		const { sx, sy, info } = p;
		const rows = new Array(info.h);
		let any = false;
		for (let i = 0; i < info.h; i++) {
			const yy = sy + i;
			if (yy < 0 || yy >= H) continue;
			const rr = info.runs[i];
			if (!rr.length) continue;
			ops.row++;
			// занятость строки -> массив флагов по ячейкам 2 пикселя (модель; счёт ops — по отрезкам)
			const need = [], forb = [];
			for (let k = 0; k < rr.length; k += 2) {
				const a = Math.max(0, sx + rr[k]), b = Math.min(W, sx + rr[k + 1]);
				if (b <= a) continue;
				const free = [];
				covFree(cov, yy, a, b, free, ops);
				let q = 0;
				for (let x = a; x < b; x++) {
					while (q < free.length && free[q + 1] <= x) q += 2;
					const isFree = q < free.length && free[q] <= x;
					(isFree ? need : forb).push(x);
				}
			}
			if (!need.length) continue;
			const bad = new Set();
			for (const x of forb) bad.add(x & ~1);
			const cells = [];
			for (const x of need) { const c = x & ~1; if (bad.has(c)) lostAlign++; else if (!cells.length || cells[cells.length - 1] !== c) cells.push(c); }
			if (!cells.length) continue;
			const out = [];
			for (const c of cells) {
				if (out.length && c - out[out.length - 1] <= gap) out[out.length - 1] = c + 2;
				else out.push(c, c + 2);
			}
			// склеенные дырки не должны накрыть занятые точки
			let okOut = [];
			for (let k = 0; k < out.length; k += 2) {
				let a = out[k];
				for (let x = out[k]; x < out[k + 1]; x += 2) {
					if (bad.has(x)) { if (x > a) okOut.push(a, x); a = x + 2; }
				}
				if (out[k + 1] > a) okOut.push(a, out[k + 1]);
			}
			if (okOut.length) { rows[i] = okOut; any = true; }
		}
		if (!any) continue;
		st.parts++;
		account(p, rows, st);
		paint(buf, p, rows);
		for (let i = 0; i < info.h; i++) {       // занятость += реально записанные точки
			const rr = rows[i];
			if (!rr) continue;
			const yy = sy + i;
			for (let k = 0; k < rr.length; k += 2) {
				let a = -1;
				for (let x = rr[k]; x <= rr[k + 1]; x++) {
					const v = x < rr[k + 1] ? img_at(p, i, x) : 0;
					if (v) { if (a < 0) a = x; }
					else if (a >= 0) { covAdd(cov, yy, a, x, ops, 0); a = -1; }
				}
			}
		}
	}
	let lost = 0, extra = 0;
	for (let i = 0; i < ref.length; i++) {
		if (ref[i] && ref[i] !== buf[i]) lost++;
		else if (!ref[i] && buf[i]) extra++;
	}
	return Object.assign({ name: 'rev spans (обратный ход)', talg: ops.row * T_ROW + ops.ivl * T_IVL, lost, extra, lostAlign }, st);
}

function img_at(p, i, x) {
	const c = x - p.sx;
	if (c < 0 || c >= p.info.w) return 0;
	return p.img[(p.info.y0 + i) * TW + p.info.x0 + c];
}

// ---------------------------------------------------------------- прогон набора

const SCENES = [
	{ game: 'TFTD', terr: 'SEABED' },
	{ game: 'TFTD', terr: 'CORAL' },
	{ game: 'TFTD', terr: 'VOLC' },
	{ game: 'TFTD', terr: 'ALART' },
	{ game: 'UFO', terr: 'CULTA' },
	{ game: 'UFO', terr: 'FOREST' },
	{ game: 'UFO', terr: 'URBAN' },
	{ game: 'UFO', terr: 'UBASE' },
	{ game: 'UFO', terr: 'XBASE' },
	{ game: 'TFTD', terr: 'CARGO' },
];

const CAMS = [[0, 0], [8, 5], [-24, 13]];      // сдвиги камеры от центра поля (чётный X)

function fmtFrames(bytes, talg, runs, parts) {
	const fd = bytes / DMA_FRAME;
	const tc = talg + runs * T_RUN + parts * T_PART;
	return { fd, fc: tc / T_FRAME, tc };
}

// Проверка: зависит ли срез (число нерисуемых нижних строк части) от положения камеры.
// Если нет — таблицу среза может посчитать конвертер один раз на карту.
function checkCamIndependence(map, data, rooms, level) {
	const runs = [];
	for (const [dx, dy] of CAMS) {
		const cx = map.sx / 2 + dx / 4, cy = map.sy / 2 + dy / 4;
		let ox = Math.round(W / 2 - (cx - cy) * B.HALF_W); ox &= ~1;
		const oy = Math.round(H / 2 - ((cx + cy) * B.QUART - level * B.LEVEL_H));
		const scene = buildScene(map, data, level, ox, oy, rooms);
		const hz = horizonPass(scene, 1, false);
		const m = new Map();
		for (let n = 0; n < scene.length; n++) {
			const p = scene[n];
			if (p.sx < 0 || p.sx + p.info.w > W || p.sy < 0 || p.sy + p.info.h > H) continue;  // не у края окна
			m.set(`${p.tx},${p.ty},${p.z},${p.kind}`, {
				cut: Math.min(hz.plan[n], p.info.h),
				edge: Math.min(p.sx, W - p.sx - p.info.w, H - p.sy - p.info.h),
			});
		}
		runs.push(m);
	}
	let same = 0, diff = 0, both = 0, farEdge = 0, diffFar = 0;
	for (const [k, v] of runs[0])
		for (let i = 1; i < runs.length; i++) {
			const o = runs[i].get(k);
			if (!o) continue;
			both++;
			const e = Math.max(v.edge, o.edge);
			if (o.cut === v.cut) same++;
			else { diff++; if (e > farEdge) farEdge = e; if (e >= 48) diffFar++; }
		}
	return { both, same, diff, farEdge, diffFar };
}

const totals = new Map();
let pal = null;
for (const sc of SCENES) {
	if (only && only !== sc.game + ':' + sc.terr) continue;
	const terr = loadTerrain(sc.game, sc.terr);
	const data = loadSets(sc.game, terr.sets);
	const map = buildField(sc.game, terr, 5, 12345);
	if (!pal) pal = B.readPalette(sc.game, 6);
	const rooms = findRooms(map, data);
	const levels = [];
	for (let z = 0; z < map.sz; z++) levels.push(z);
	const ci = checkCamIndependence(map, data, rooms, map.sz - 1);
	const inRoom = rooms.mask.reduce((a, v) => a + v, 0);
	console.log(`\n=== ${sc.game} ${sc.terr}: поле ${map.sx}x${map.sy}x${map.sz}, наборы ${terr.sets.join('+')}`);
	console.log(`    комнат ${rooms.rooms.length}, клеток в них ${inRoom} из ${map.sx * map.sy * map.sz}` +
		` (${(100 * inRoom / (map.sx * map.sy * map.sz)).toFixed(1)}%), крупнейшая ${rooms.rooms.reduce((a, r) => Math.max(a, r.n), 0)}`);
	console.log(`    срез не зависит от камеры: ${ci.same} из ${ci.both} частей (${(100 * ci.same / Math.max(1, ci.both)).toFixed(1)}%), расходится ${ci.diff} (дальше 48 px от края окна — ${ci.diffFar}, макс. удаление ${ci.farEdge} px)`);
	for (const level of levels) {
		for (let ci = 0; ci < CAMS.length; ci++) {
			const [dx, dy] = CAMS[ci];
			const cx = map.sx / 2 + dx / 4, cy = map.sy / 2 + dy / 4;
			let ox = Math.round(W / 2 - (cx - cy) * B.HALF_W); ox &= ~1;
			const oy = Math.round(H / 2 - ((cx + cy) * B.QUART - level * B.LEVEL_H));
			const scene = buildScene(map, data, level, ox, oy, rooms);
			if (scene.length < 20) continue;
			const ref = new Uint8Array(W * H);
			for (const p of scene) paint(ref, p, baseRows(p));
			KEEP.clear();
			if (wantPng && ci === 0) { KEEP.set('spans g8', null); KEEP.set('hz col', null); }
			const { res } = runScene(scene, ref);
			res.push(revPass(scene, ref, 0));
			const tag = `${sc.game}:${sc.terr} z=${level} cam#${ci}`;
			if (level === levels[levels.length - 1] && ci === 0)
				console.log(`--- ${tag}: частей ${scene.length}`);
			for (const r of res) {
				const f = fmtFrames(r.bytes, r.talg, r.runs, r.parts);
				const key = r.name;
				if (!totals.has(key)) totals.set(key, { n: 0, parts: 0, runs: 0, bytes: 0, talg: 0, lost: 0, extra: 0, lostAlign: 0, fd: 0, fc: 0 });
				const t = totals.get(key);
				t.n++; t.parts += r.parts; t.runs += r.runs; t.bytes += r.bytes; t.talg += r.talg;
				t.lost += r.lost; t.extra += r.extra; t.lostAlign += r.lostAlign || 0;
				t.fd += f.fd; t.fc += f.fc;
				if (level === levels[levels.length - 1] && ci === 0)
					console.log(`  ${r.name.padEnd(24)} частей ${String(r.parts).padStart(4)}  DMA ${String(r.runs).padStart(5)} зап  ${(r.bytes / 1024).toFixed(1).padStart(7)} КБ  = ${f.fd.toFixed(2)} кадра DMA + ${f.fc.toFixed(2)} кадра CPU   потеряно ${r.lost}`);
			}
			if (wantPng && ci === 0) {
				const save = (buf, suffix) => {
					const rgb = Buffer.alloc(W * H * 3);
					for (let i = 0; i < W * H; i++) { const c = pal[buf[i]]; rgb[i * 3] = c[0]; rgb[i * 3 + 1] = c[1]; rgb[i * 3 + 2] = c[2]; }
					B.writePng(path.join('tmp/proto/opt1', `${sc.game}_${sc.terr}_z${level}${suffix}.png`), W, H, rgb);
				};
				save(ref, '');
				for (const [nm, buf] of KEEP) {
					if (!buf) continue;
					save(buf, '_' + nm.replace(/\W+/g, '_'));
					const d = Buffer.alloc(W * H * 3);       // расхождения: красный — потеряно, зелёный — лишнее
					for (let i = 0; i < W * H; i++) {
						if (ref[i] !== buf[i]) { d[i * 3] = ref[i] ? 255 : 0; d[i * 3 + 1] = ref[i] ? 0 : 255; }
						else { const v = ref[i] ? 40 : 0; d[i * 3] = d[i * 3 + 1] = d[i * 3 + 2] = v; }
					}
					B.writePng(path.join('tmp/proto/opt1', `${sc.game}_${sc.terr}_z${level}_${nm.replace(/\W+/g, '_')}_diff.png`), W, H, d);
				}
			}
		}
	}
}

console.log('\n================ ИТОГО по всем сценам ================');
console.log('режим                     частей   зап.DMA    КБ     кадров DMA  кадров CPU  всего  потеряно');
const base = totals.get('base (как сейчас)');
for (const [name, t] of totals) {
	const tot = Math.max(t.fd / t.n, t.fc / t.n);
	console.log(`${name.padEnd(24)} ${(t.parts / t.n).toFixed(0).padStart(6)} ${(t.runs / t.n).toFixed(0).padStart(8)} ${(t.bytes / t.n / 1024).toFixed(1).padStart(7)} ${(t.fd / t.n).toFixed(2).padStart(11)} ${(t.fc / t.n).toFixed(2).padStart(11)} ${tot.toFixed(2).padStart(7)} ${String(t.lost).padStart(9)}${t.lostAlign ? '  (по выравниванию ' + t.lostAlign + ')' : ''}`);
}
console.log(`\nбаза: ${(base.bytes / base.n / 1024).toFixed(1)} КБ, ${(base.runs / base.n).toFixed(0)} запусков DMA; сцен ${base.n}`);

// --- чувствительность к цене запуска DMA: сколько байт «стоит» один запуск
console.log('\n--- цена запуска DMA: кадров на полную перерисовку (DMA и CPU совмещены) ---');
const show = ['base (как сейчас)', 'spans g8', 'rows (строки целиком)', 'гибрид полы rows', 'hz col', 'hz col (таблица)', 'комнаты, глубокие'];
process.stdout.write('запуск DMA, T'.padEnd(16));
for (const s of show) process.stdout.write(s.padStart(22));
process.stdout.write('\n');
for (const tr of [100, 150, 200, 300, 450]) {
	process.stdout.write(String(tr).padEnd(16));
	for (const s of show) {
		const t = totals.get(s);
		if (!t) { process.stdout.write(''.padStart(22)); continue; }
		const fd = t.bytes / t.n / DMA_FRAME;
		const fc = (t.talg / t.n + t.runs / t.n * tr + t.parts / t.n * T_PART) / T_FRAME;
		process.stdout.write(`${Math.max(fd, fc).toFixed(2)} (D${fd.toFixed(2)}/C${fc.toFixed(2)})`.padStart(22));
	}
	process.stdout.write('\n');
}
// --- чувствительность к реальной пропускной способности BLT1 (74 КБ/кадр — при простое CPU)
console.log('\n--- пропускная способность BLT1: кадров на полную перерисовку ---');
process.stdout.write('БЛТ1, КБ/кадр'.padEnd(16));
for (const s of show) process.stdout.write(s.padStart(22));
process.stdout.write('\n');
for (const bw of [74, 50, 35, 25, 18]) {
	process.stdout.write(String(bw).padEnd(16));
	for (const s of show) {
		const t = totals.get(s);
		if (!t) { process.stdout.write(''.padStart(22)); continue; }
		const fd = t.bytes / t.n / (bw * 1024);
		const fc = (t.talg / t.n + t.runs / t.n * T_RUN + t.parts / t.n * T_PART) / T_FRAME;
		process.stdout.write(`${Math.max(fd, fc).toFixed(2)} (D${fd.toFixed(2)}/C${fc.toFixed(2)})`.padStart(22));
	}
	process.stdout.write('\n');
}

console.log('\n--- сколько байт DMA «оплачивает» один запуск ---');
console.log(`при ${T_RUN} T на запуск один запуск стоит столько же времени кадра, сколько ${(T_RUN / T_FRAME * DMA_FRAME).toFixed(0)} Б переноса BLT1;`);
for (const s of show) {
	const t = totals.get(s);
	if (!t) continue;
	console.log(`  ${s.padEnd(24)} средний запуск переносит ${(t.bytes / t.runs).toFixed(1)} Б`);
}
console.log(`модель: кадр ${T_FRAME} T, BLT1 ${DMA_FRAME} Б/кадр, запуск DMA ${T_RUN} T, часть ${T_PART} T, колонка ${T_COL} T, строка ${T_ROW} T, отрезок ${T_IVL} T`);
