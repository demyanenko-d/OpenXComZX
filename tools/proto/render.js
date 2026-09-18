// Прототип вида боя: собирает изометрическую картинку блок-карты и считает работу процессора
// при разных способах отсечения (16_battlescape_plan.md §8). Картинки — PNG в tmp/proto/.
//
//   node tools/proto/render.js                               # SEABED00 как есть
//   node tools/proto/render.js --map SEABED03 --level 1
//   node tools/proto/render.js --game UFO --terrain CULTA --sets BLANKS,CULTIVAT,BARN --map CULTA00
//
// Метрики считаются в «блитах» (одна часть тайла = один вызов DMA BLT1 у нас) и в байтах,
// которые DMA прочитает и запишет: спрайт 32x40 — это 1280 байт, но прозрачные строки сверху
// пропускаются, поэтому считается фактическая высота непустой части кадра.
'use strict';
const fs = require('fs');
const path = require('path');
const B = require('./battle');

function arg(name, def) {
	const i = process.argv.indexOf('--' + name);
	return i > 0 && i + 1 < process.argv.length ? process.argv[i + 1] : def;
}

const game = arg('game', 'TFTD');
const root = B.GAMES[game];
const terrain = arg('terrain', 'SEABED');
const sets = arg('sets', 'BLANKS,SAND,ROCKS,WEEDS,DEBRIS,UFOBITS').split(',');
const mapName = arg('map', 'SEABED00');
const outDir = arg('out', 'tmp/proto');
const camLevel = parseInt(arg('level', '0'), 10);

// ---------------------------------------------------------------- данные

const parts = [];                          // записи MCD всех наборов подряд (индексация как в MAP)
const frames = [];                         // спрайты тех же наборов подряд
for (const s of sets) {
	const dir = path.join(root, 'TERRAIN');
	const mcd = B.readMcd(fs.readFileSync(path.join(dir, s + '.MCD')));
	const pck = B.readPck(fs.readFileSync(path.join(dir, s + '.PCK')), fs.readFileSync(path.join(dir, s + '.TAB')));
	const base = frames.length;
	for (const r of mcd) { r.set = s; r.base = base; parts.push(r); }
	for (const f of pck) frames.push(f);
}

// Карта боя — поле из блоков (mapScript замащивает 10x10-блоками): --blocks N берёт N x N
// блоков по маске имени, как это делает генератор
function buildMap(pattern, n) {
	const dir = path.join(root, 'MAPS');
	const names = fs.readdirSync(dir).filter(f2 => f2.startsWith(pattern) && f2.endsWith('.MAP')).sort();
	const blocks = names.map(f2 => B.readMap(fs.readFileSync(path.join(dir, f2))));
	if (n <= 1) return blocks[0];
	const bw = blocks[0].sx, bh = blocks[0].sy;
	let sz = 0;
	for (const b of blocks) if (b.sz > sz) sz = b.sz;
	const sx = bw * n, sy = bh * n;
	const cells = new Uint8Array(sx * sy * sz * 4);
	for (let by = 0; by < n; by++)
		for (let bx = 0; bx < n; bx++) {
			const b = blocks[(by * n + bx) % blocks.length];
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

const map = buildMap(arg('blocks-of', mapName.replace(/[0-9]+$/, '')), parseInt(arg('blocks', '1'), 10));
const pal = B.readPalette(game, 6);        // PAL_BATTLESCAPE из нашего PAL.PAK

// Кадр части тайла: MCD.Frame[0] — первый кадр анимации, смещение — по набору
function partFrame(id) {
	const r = parts[id - 1];
	return r ? frames[r.base + r.frame[0]] : null;
}

// Разбор кадра: непустые строки (для оценки DMA), непрозрачность по колонкам (для горизонта
// занятости) и «сплошной ромб пола» (для отсева крышами). colTop[i] — первая непрозрачная
// строка колонки, дальше вниз непрозрачно; 255 — колонка пустая.
const rowsCache = new Map();
function frameRows(img) {
	if (!img) return { top: 0, h: 0, floor: false, colTop: null };
	if (rowsCache.has(img)) return rowsCache.get(img);
	let top = B.TILE_H, bottom = -1, lower = 0;
	const colTop = new Uint8Array(B.TILE_W).fill(255);
	for (let y = 0; y < B.TILE_H; y++)
		for (let x = 0; x < B.TILE_W; x++)
			if (img[y * B.TILE_W + x]) {
				if (y < top) top = y;
				if (y > bottom) bottom = y;
				if (colTop[x] === 255) colTop[x] = y;
				if (y >= B.TILE_H - 16) lower++;
			}
	const v = { top: bottom < 0 ? 0 : top, h: bottom < 0 ? 0 : bottom - top + 1, floor: lower >= 240, colTop };
	rowsCache.set(img, v);
	return v;
}

// ---------------------------------------------------------------- отрисовка

const buf = new Uint8Array(B.BUF_W * B.BUF_H);

function blit(img, sx, sy, stat) {
	const r = frameRows(img);
	if (!r.h) return;
	stat.blits++;
	stat.bytes += B.TILE_W * r.h;
	for (let y = 0; y < r.h; y++) {
		const py = sy + r.top + y;
		if (py < 0 || py >= B.BUF_H) continue;
		for (let x = 0; x < B.TILE_W; x++) {
			const v = img[(r.top + y) * B.TILE_W + x];
			if (!v) continue;
			const px = sx + x;
			if (px < 0 || px >= B.BUF_W) continue;
			buf[py * B.BUF_W + px] = v;
		}
	}
}

// Клетка карты: индексы четырёх частей (пол, западная стена, северная стена, объект)
function cellParts(x, y, z) {
	const o = ((z * map.sy + y) * map.sx + x) * 4;
	return [map.cells[o], map.cells[o + 1], map.cells[o + 2], map.cells[o + 3]];
}

// «Сверху сплошной пол» — клетку под ним не видно
function roofed(x, y, z) {
	if (z + 1 >= map.sz) return false;
	const p = cellParts(x, y, z + 1)[0];
	if (!p) return false;
	const rec = parts[p - 1];
	if (rec && rec.noFloor) return false;
	return frameRows(partFrame(p)).floor;
}

// Экранная позиция клетки (Camera::convertMapToScreen) с учётом начала буфера
function screenOf(x, y, z, ox, oy) {
	return { sx: (x - y) * B.HALF_W + ox, sy: (x + y) * B.QUART - z * B.LEVEL_H + oy };
}

// mode: 'all' — как оригинал (все уровни до текущего), 'roof' — плюс отсев крышами,
// 'roof+cover' — плюс горизонт занятости по колонкам экрана
function render(mode, level, ox, oy) {
	buf.fill(0);
	const stat = { cells: 0, skipRoof: 0, skipCover: 0, skipEmpty: 0, skipOff: 0, blits: 0, bytes: 0 };
	const cover = new Int16Array(B.BUF_W).fill(B.BUF_H);   // верхняя граница занятости колонки
	const visible = [];
	for (let z = 0; z <= level; z++)
		for (let x = 0; x < map.sx; x++)
			for (let y = 0; y < map.sy; y++) {
					const sxy = screenOf(x, y, z, ox, oy);
					if (sxy.sx <= -B.TILE_W || sxy.sx >= B.VIEW_W || sxy.sy <= -B.TILE_H || sxy.sy >= B.VIEW_H) { stat.skipOff++; continue; }
					const p = cellParts(x, y, z);
					if (!p[0] && !p[1] && !p[2] && !p[3]) { stat.skipEmpty++; continue; }
				// Клетка не видна, если сплошной пол есть и над ней, и над соседями, через которых
				// её можно было бы увидеть сбоку (ближние по экрану: x+1, y+1 и их диагональ)
				const hidden = mode !== 'all' && z < level && roofed(x, y, z)
					&& roofed(Math.min(x + 1, map.sx - 1), y, z)
					&& roofed(x, Math.min(y + 1, map.sy - 1), z)
					&& roofed(Math.min(x + 1, map.sx - 1), Math.min(y + 1, map.sy - 1), z);
				if (hidden) { stat.skipRoof++; continue; }
				visible.push({ x, y, z, p, skipFloor: false });
			}
	if (mode === 'exact') {
		// эталон: маска занятости по точкам, проход от ближних к дальним. На Z80 так не сделать —
		// это верхняя граница того, сколько вообще можно не рисовать
		const mask = new Uint8Array(B.BUF_W * B.BUF_H);
		const order = visible.slice().sort((a, b) => (b.x + b.y) - (a.x + a.y) || b.z - a.z);
		const keep = new Set();
		for (const c of order) {
			const sp = screenOf(c.x, c.y, c.z, ox, oy);
			let seen = false;
			for (let k = 0; k < 4 && !seen; k++) {
				if (!c.p[k] || (k === 0 && c.skipFloor)) continue;
				const img = partFrame(c.p[k]);
				if (!img) continue;
				const yoff = parts[c.p[k] - 1] ? parts[c.p[k] - 1].pLevel : 0;
				for (let y = 0; y < B.TILE_H && !seen; y++)
					for (let x = 0; x < B.TILE_W; x++) {
						if (!img[y * B.TILE_W + x]) continue;
						const px = sp.sx + x, py = sp.sy - yoff + y;
						if (px < 0 || px >= B.BUF_W || py < 0 || py >= B.BUF_H) continue;
						if (!mask[py * B.BUF_W + px]) { seen = true; break; }
					}
			}
			if (!seen) { stat.skipCover++; continue; }
			keep.add(c);
			for (let k = 0; k < 4; k++) {
				if (!c.p[k] || (k === 0 && c.skipFloor)) continue;
				const img = partFrame(c.p[k]);
				if (!img) continue;
				const yoff = parts[c.p[k] - 1] ? parts[c.p[k] - 1].pLevel : 0;
				for (let y = 0; y < B.TILE_H; y++)
					for (let x = 0; x < B.TILE_W; x++) {
						if (!img[y * B.TILE_W + x]) continue;
						const px = sp.sx + x, py = sp.sy - yoff + y;
						if (px < 0 || px >= B.BUF_W || py < 0 || py >= B.BUF_H) continue;
						mask[py * B.BUF_W + px] = 1;
					}
			}
		}
		for (let i = visible.length - 1; i >= 0; i--) if (!keep.has(visible[i])) visible.splice(i, 1);
	}
	if (mode === 'roof+cover') {
		// проход от ближних к дальним: ближние закрывают то, что выше по экрану
		const order = visible.slice().sort((a, b) => (b.x + b.y) - (a.x + a.y) || b.z - a.z);
		const keep = new Set();
		for (const c of order) {
			const s = screenOf(c.x, c.y, c.z, ox, oy);
			let seen = false;
			for (let k = 0; k < 4 && !seen; k++) {
				if (!c.p[k]) continue;
				const r = frameRows(partFrame(c.p[k]));
				if (!r.colTop) continue;
				const yoff = parts[c.p[k] - 1] ? parts[c.p[k] - 1].pLevel : 0;
				for (let i = 0; i < B.TILE_W; i++) {
					if (r.colTop[i] === 255) continue;
					const px = s.sx + i;
					if (px < 0 || px >= B.BUF_W) continue;
					if (s.sy - yoff + r.colTop[i] < cover[px]) { seen = true; break; }
				}
			}
			if (!seen) { stat.skipCover++; continue; }
			keep.add(c);
			for (let k = 0; k < 1; k++) {                           // горизонт наращивают только сплошные полы:
				if (!c.p[k]) continue;                              //   у стен и объектов бывают окна и дыры
				const r = frameRows(partFrame(c.p[k]));
				if (!r.colTop || !r.floor) continue;
				const yoff = parts[c.p[k] - 1] ? parts[c.p[k] - 1].pLevel : 0;
				for (let i = 0; i < B.TILE_W; i++) {
					if (r.colTop[i] === 255) continue;
					const px = s.sx + i;
					if (px < 0 || px >= B.BUF_W) continue;
					const top = s.sy - yoff + r.colTop[i];
					if (top < cover[px]) cover[px] = top;
				}
			}
		}
		for (let i = visible.length - 1; i >= 0; i--) if (!keep.has(visible[i])) visible.splice(i, 1);
	}
	for (const c of visible) {
		stat.cells++;
		const s = screenOf(c.x, c.y, c.z, ox, oy);
		for (let k = 0; k < 4; k++) {
			if (!c.p[k] || (k === 0 && c.skipFloor)) continue;
			const rec = parts[c.p[k] - 1];
			const yoff = rec ? rec.pLevel : 0;   // MCD.P_Level -> MapData::getYOffset (MapDataSet.cpp:175)
			blit(partFrame(c.p[k]), s.sx, s.sy - yoff, stat);
		}
	}
	return stat;
}

// ---------------------------------------------------------------- вывод

function savePng(file, x0, y0, w, h) {
	const rgb = Buffer.alloc(w * h * 3);
	for (let y = 0; y < h; y++)
		for (let x = 0; x < w; x++) {
			const v = buf[(y0 + y) * B.BUF_W + (x0 + x)];
			const c = pal[v];
			const o = (y * w + x) * 3;
			rgb[o] = c[0]; rgb[o + 1] = c[1]; rgb[o + 2] = c[2];
		}
	B.writePng(file, w, h, rgb);
}

const level = Math.min(camLevel, map.sz - 1);
// камера на центр карты: центр блока попадает в середину окна 320x144
const cx = map.sx / 2, cy = map.sy / 2;
const ox = Math.round(B.VIEW_W / 2 - (cx - cy) * B.HALF_W);
const oy = Math.round(B.VIEW_H / 2 - ((cx + cy) * B.QUART - level * B.LEVEL_H));

console.log(`${game} ${terrain}/${mapName}: карта ${map.sx}x${map.sy}x${map.sz}, уровень ${level}, окно ${B.VIEW_W}x${B.VIEW_H}`);
console.log('режим            клеток  блитов   КБ DMA  пропущено (крыши/перекрыто/пусто)');
let ref = null;
for (const mode of ['all', 'roof', 'roof+cover', 'exact']) {
	const s = render(mode, level, ox, oy);
	if (!ref) ref = buf.slice();
	else {
		let lost = 0, extra = 0;
		for (let i = 0; i < ref.length; i++) {
			if (ref[i] && !buf[i]) lost++;
			else if (!ref[i] && buf[i]) extra++;
		}
		s.lost = lost; s.extra = extra;
	}
	const diff = s.lost === undefined ? '' : `   потеряно точек ${s.lost}, лишних ${s.extra}`;
	console.log(`${mode.padEnd(16)} ${String(s.cells).padStart(6)} ${String(s.blits).padStart(7)} ${(s.bytes / 1024).toFixed(1).padStart(8)}   ${s.skipRoof}/${s.skipCover}/${s.skipEmpty}${diff}`);
	savePng(path.join(outDir, `${mapName}_L${level}_${mode.replace('+', '_')}.png`), 0, 0, B.VIEW_W, B.VIEW_H);
	savePng(path.join(outDir, `${mapName}_L${level}_${mode.replace('+', '_')}_full.png`), 0, 0, B.BUF_W, Math.min(B.BUF_H, 320));
}
