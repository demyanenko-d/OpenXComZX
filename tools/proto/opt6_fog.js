// Приём №6 плана боя: «рисовать меньше, потому что игрок видит меньше».
// Модель реального хода миссии: отряд высаживается, идёт по карте, копится туман войны
// (Tile::isDiscovered, TileEngine::calculateFOV). Считаем, сколько блитов и байт DMA остаётся
// на вид при честном учёте разведанности, во что обходится затенение и упрощённый вид при
// скролле (16_battlescape_plan.md §5.1–5.2, §8.3a, §8.4).
//
//   node tools/proto/opt6_fog.js                                     # TFTD ALART 50x50x4
//   node tools/proto/opt6_fog.js --level 0
//   node tools/proto/opt6_fog.js --game UFO --terrain URBAN --sets BLANKS,ROADS,URBITS,URBAN,FRNITURE \
//        --map URBAN00 --blocks 5 --level 0
//   node tools/proto/opt6_fog.js --night 1
//
// Метрики те же, что в §8.3a: «блит» — один запуск DMA BLT1 на часть клетки, «КБ» — сколько
// байт DMA прочитает и запишет (ширина 32 x непустая высота кадра).
'use strict';
const fs = require('fs');
const path = require('path');
const B = require('./battle');

function arg(name, def) {
	const i = process.argv.indexOf('--' + name);
	return i > 0 && i + 1 < process.argv.length ? process.argv[i + 1] : def;
}
const num = (n, d) => parseInt(arg(n, String(d)), 10);

const game = arg('game', 'TFTD');
// каталог игры: в battle.js для UFO записано другое написание, поэтому берём первый существующий
const root = arg('root', null) || [B.GAMES[game], 'Steam/XCom UFO Defense/XCOM',
	'Steam/X-COM Terror from the Deep/TFD'].find(p => fs.existsSync(path.join(p, 'TERRAIN')));
const terrain = arg('terrain', 'ALART');
const sets = arg('sets', 'BLANKS,SAND,ROCKS,WEEDS,PYRAMID,UFOBITS').split(',');
const mapName = arg('map', 'ALART00');
const nBlocks = num('blocks', 5);
const outDir = arg('out', 'tmp/proto/opt6');
const turnsTotal = num('turns', 6);
const squadN = num('soldiers', 8);
const stepsPerTurn = num('steps', 12);       // TU 55 / 4 на клетку ~ 13
const night = num('night', 0);
const wantLevel = arg('level', null);

const MAX_VIEW = 20, MAX_VIEW_SQR = 400;
// окно вида: по умолчанию карта боя 320x144; --vw/--vh — чтобы посчитать вид во весь буфер
// прокрутки 512x456 (аппаратный скролл, §8.6)
const VIEW_W = num('vw', B.VIEW_W), VIEW_H = num('vh', B.VIEW_H);

// ---------------------------------------------------------------- данные

const parts = [];                            // записи MCD всех наборов подряд (индексация как в MAP)
const frames = [];                           // спрайты тех же наборов подряд
for (const s of sets) {
	const dir = path.join(root, 'TERRAIN');
	const mcd = B.readMcd(fs.readFileSync(path.join(dir, s + '.MCD')));
	const pck = B.readPck(fs.readFileSync(path.join(dir, s + '.PCK')), fs.readFileSync(path.join(dir, s + '.TAB')));
	const base = frames.length;
	for (const r of mcd) { r.set = s; r.base = base; parts.push(r); }
	for (const f of pck) frames.push(f);
}

function buildMap(pattern, n) {
	const dir = path.join(root, 'MAPS');
	const names = fs.readdirSync(dir).filter(f => f.startsWith(pattern) && f.endsWith('.MAP')).sort();
	const blocks = [];
	for (const f of names) {
		const b = B.readMap(fs.readFileSync(path.join(dir, f)));
		if (blocks.length && (b.sx !== blocks[0].sx || b.sy !== blocks[0].sy)) continue;  // только блоки 10x10
		blocks.push(b);
	}
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

const map = buildMap(mapName.replace(/[0-9]+$/, ''), nBlocks);
const { sx, sy, sz } = map;
const N = sx * sy * sz;
const pal = B.readPalette(game, 6);
pal[255] = [0, 0, 0];                        // служебный индекс: чёрный силуэт неразведанного

const idx = (x, y, z) => (z * sy + y) * sx + x;
const inMap = (x, y, z) => x >= 0 && y >= 0 && z >= 0 && x < sx && y < sy && z < sz;
const part = (i, k) => map.cells[i * 4 + k];
const rec = id => (id ? parts[id - 1] : null);

// ---------------------------------------------------------------- свойства клеток

// блокировка обзора: объект/стена со Stop_LOS (blockage(..., DT_NONE) -> 255)
const blkObj = new Uint8Array(N), blkW = new Uint8Array(N), blkN = new Uint8Array(N);
const hasFloor = new Uint8Array(N), walk = new Uint8Array(N), lightSrc = new Uint8Array(N);
for (let i = 0; i < N; i++) {
	const f = rec(part(i, 0)), w = rec(part(i, 1)), n = rec(part(i, 2)), o = rec(part(i, 3));
	if (f && !f.noFloor) hasFloor[i] = 1;
	if (o && o.stopLOS) blkObj[i] = 1;
	if (w && w.stopLOS) blkW[i] = 1;
	if (n && n.stopLOS) blkN[i] = 1;
	if (f && f.lightSource) lightSrc[i] = Math.max(lightSrc[i], f.lightSource);
	if (o && o.lightSource) lightSrc[i] = Math.max(lightSrc[i], o.lightSource);
	// проходимость (грубо: пол есть, объект не глухой, стены проверяются на шаге)
	let ok = hasFloor[i] ? 1 : 0;
	if (o && (o.tuWalk === 0 || o.tuWalk >= 255 || o.bigWall)) ok = 0;
	walk[i] = ok;
}
// стена мешает шагу, если она есть и это не дверь
function wallBlocksWalk(id) { const r = rec(id); return r ? (r.door || r.ufoDoor ? 0 : 1) : 0; }
const wWalkW = new Uint8Array(N), wWalkN = new Uint8Array(N);
for (let i = 0; i < N; i++) { wWalkW[i] = wallBlocksWalk(part(i, 1)); wWalkN[i] = wallBlocksWalk(part(i, 2)); }

function stepBlockedWalk(x, y, nx, ny, z) {
	if (!inMap(nx, ny, z) || !walk[idx(nx, ny, z)]) return true;
	if (nx > x && wWalkW[idx(nx, y, z)]) return true;
	if (nx < x && wWalkW[idx(x, y, z)]) return true;
	if (ny > y && wWalkN[idx(x, ny, z)]) return true;
	if (ny < y && wWalkN[idx(x, y, z)]) return true;
	return false;
}

// блокировка обзора при шаге из (x,y,z) в (nx,ny,nz); диагональ блокируется, только если закрыты
// оба направления (даём обзору фору — оценка экономии получается осторожной)
function stepBlocksLOS(x, y, z, nx, ny, nz) {
	const dx = nx - x, dy = ny - y;
	const bx = dx > 0 ? blkW[idx(nx, y, z)] : dx < 0 ? blkW[idx(x, y, z)] : -1;
	const by = dy > 0 ? blkN[idx(x, ny, z)] : dy < 0 ? blkN[idx(x, y, z)] : -1;
	if (dx && dy) { if (bx && by) return true; }
	else { if (bx === 1 || by === 1) return true; }
	if (blkObj[idx(nx, ny, nz)]) return true;                 // объект в целевой клетке
	if (nz !== z && hasFloor[idx(nx, ny, Math.max(z, nz))]) return true;  // пол между ярусами
	return false;
}

// ---------------------------------------------------------------- туман войны

const disc = new Uint8Array(N);              // разведано (Tile::_discovered[2])
const vis = new Uint8Array(N);               // видно сейчас (Tile::_visible)
const shade = new Uint8Array(N);

const signX = [+1, +1, +1, +1, -1, -1, -1, -1];
const signY = [-1, -1, -1, +1, +1, +1, -1, -1];

// 3D-линия по клеткам (упрощённый calculateLine в тайловом режиме), помечает пройденное
function traceDiscover(x0, y0, z0, x1, y1, z1, mark) {
	let x = x0, y = y0, z = z0;
	const dx = Math.abs(x1 - x0), dy = Math.abs(y1 - y0), dz = Math.abs(z1 - z0);
	const sxs = x1 > x0 ? 1 : -1, sys = y1 > y0 ? 1 : -1, szs = z1 > z0 ? 1 : -1;
	const n = Math.max(dx, dy, dz);
	if (!n) { mark(x, y, z); return; }
	let ex = 0, ey = 0, ez = 0;
	mark(x, y, z);
	for (let i = 0; i < n; i++) {
		ex += dx; ey += dy; ez += dz;
		let nx = x, ny = y, nz = z;
		if (ex >= n) { ex -= n; nx += sxs; }
		if (ey >= n) { ey -= n; ny += sys; }
		if (ez >= n) { ez -= n; nz += szs; }
		if (!inMap(nx, ny, nz)) return;
		const blocked = stepBlocksLOS(x, y, z, nx, ny, nz);
		mark(nx, ny, nz);                     // саму преграду игрок видит (стена, объект)
		x = nx; y = ny; z = nz;
		if (blocked) return;
	}
}

// calculateFOV игрока: сектор 90 градусов по направлению, дальность 20, все ярусы Z
function calcFOV(ux, uy, uz, dir, markVis) {
	const swap = (dir === 0 || dir === 4);
	const mark = (x, y, z) => {
		const i = idx(x, y, z);
		disc[i] = 1;
		if (markVis) vis[i] = 1;
	};
	for (let x = 0; x <= MAX_VIEW; x++) {
		const y1 = (dir % 2) ? 0 : -x, y2 = (dir % 2) ? MAX_VIEW : x;
		for (let y = y1; y <= y2; y++) {
			if (x * x + y * y > MAX_VIEW_SQR) continue;
			const tx = ux + signX[dir] * (swap ? y : x);
			const ty = uy + signY[dir] * (swap ? x : y);
			if (tx < 0 || ty < 0 || tx >= sx || ty >= sy) continue;
			for (let z = 0; z < sz; z++) traceDiscover(ux, uy, uz, tx, ty, z, mark);
		}
	}
}

// ---------------------------------------------------------------- освещение

function calcLight(units) {
	const light = new Uint8Array(N);
	const globalShade = night ? 15 : 0;
	const sun = 15 - globalShade;
	for (let i = 0; i < N; i++) light[i] = sun;
	if (!night) {                              // днём под крышей -2 (calculateSunShading)
		for (let z = 0; z < sz; z++)
			for (let y = 0; y < sy; y++)
				for (let x = 0; x < sx; x++) {
					let block = 0;
					for (let zz = sz - 1; zz > z; zz--) if (hasFloor[idx(x, y, zz)]) block++;
					if (block) light[idx(x, y, z)] = sun - 2;
				}
	}
	const addLight = (cx, cy, power) => {
		for (let x = -power; x <= power; x++)
			for (let y = -power; y <= power; y++) {
				const d = Math.round(Math.sqrt(x * x + y * y));
				if (d > power) continue;
				const px = cx + x, py = cy + y;
				if (px < 0 || py < 0 || px >= sx || py >= sy) continue;
				for (let z = 0; z < sz; z++) {
					const i = idx(px, py, z);
					if (light[i] < power - d) light[i] = power - d;
				}
			}
	};
	for (let i = 0; i < N; i++) if (lightSrc[i]) {
		const z = Math.floor(i / (sx * sy)), r = i % (sx * sy);
		addLight(r % sx, Math.floor(r / sx), lightSrc[i]);
	}
	for (const u of units) addLight(u.x, u.y, 15);     // personalLightPower = 15
	for (let i = 0; i < N; i++) shade[i] = Math.max(0, 15 - light[i]);
}

// ---------------------------------------------------------------- отрисовка и метрики

const rowsCache = new Map();
function frameRows(img) {
	if (!img) return { top: 0, h: 0 };
	if (rowsCache.has(img)) return rowsCache.get(img);
	let top = B.TILE_H, bottom = -1;
	for (let y = 0; y < B.TILE_H; y++)
		for (let x = 0; x < B.TILE_W; x++)
			if (img[y * B.TILE_W + x]) { if (y < top) top = y; if (y > bottom) bottom = y; }
	const v = { top: bottom < 0 ? 0 : top, h: bottom < 0 ? 0 : bottom - top + 1 };
	rowsCache.set(img, v);
	return v;
}
function partFrame(id) { const r = rec(id); return r ? frames[r.base + r.frame[0]] : null; }

const buf = new Uint8Array(B.BUF_W * B.BUF_H);
function blit(img, px, py, stat, black) {
	const r = frameRows(img);
	if (!r.h) return;
	stat.blits++;
	stat.bytes += B.TILE_W * r.h;
	if (black) stat.black++;
	for (let y = 0; y < r.h; y++) {
		const yy = py + r.top + y;
		if (yy < 0 || yy >= B.BUF_H) continue;
		for (let x = 0; x < B.TILE_W; x++) {
			const v = img[(r.top + y) * B.TILE_W + x];
			if (!v) continue;
			const xx = px + x;
			if (xx < 0 || xx >= B.BUF_W) continue;
			buf[yy * B.BUF_W + xx] = black ? 255 : v;
		}
	}
}
const screenOf = (x, y, z, ox, oy) => ({ sx: (x - y) * B.HALF_W + ox, sy: (x + y) * B.QUART - z * B.LEVEL_H + oy });

// список клеток вида в порядке художника (Z снизу вверх, X, Y), только попадающие в окно
function viewCells(level, ox, oy) {
	const list = [];
	for (let z = 0; z <= level; z++)
		for (let x = 0; x < sx; x++)
			for (let y = 0; y < sy; y++) {
				const s = screenOf(x, y, z, ox, oy);
				if (s.sx <= -B.TILE_W || s.sx >= VIEW_W || s.sy <= -B.TILE_H || s.sy >= VIEW_H) continue;
				const i = idx(x, y, z);
				const p = [part(i, 0), part(i, 1), part(i, 2), part(i, 3)];
				if (!p[0] && !p[1] && !p[2] && !p[3]) continue;
				list.push({ x, y, z, i, p, s });
			}
	return list;
}

// Нужен ли чёрный силуэт неразведанной клетке: она перекрывает то, что уже нарисовано
// (правило, исполнимое на Z80: смотрим только на соседей, нарисованных раньше)
function needsMask(c) {
	const { x, y, z } = c;
	const behind = [[x - 1, y, z], [x, y - 1, z], [x - 1, y - 1, z],
		[x, y, z - 1], [x + 1, y, z - 1], [x, y + 1, z - 1], [x + 1, y + 1, z - 1]];
	for (const [bx, by, bz] of behind)
		if (inMap(bx, by, bz) && disc[idx(bx, by, bz)]) return true;
	return false;
}

// mode: full | fogorig | fog | fogmask | floors
function render(cells, mode) {
	buf.fill(0);
	const stat = { cells: 0, blits: 0, bytes: 0, black: 0, hidden: 0 };
	for (const c of cells) {
		const seen = disc[c.i];
		if (mode !== 'full' && !seen) {
			if (mode === 'fog' || mode === 'floors') { stat.hidden++; continue; }
			if (mode === 'fogmask' && !needsMask(c)) { stat.hidden++; continue; }
		}
		stat.cells++;
		const black = mode !== 'full' && !seen;
		for (let k = 0; k < 4; k++) {
			if (!c.p[k]) continue;
			if (mode === 'floors' && k !== 0) continue;
			if (mode === 'walls' && k === 3) continue;    // упрощение: без объектов (мебель, кусты, техника)
			const r0 = rec(c.p[k]);
			blit(partFrame(c.p[k]), c.s.sx, c.s.sy - (r0 ? r0.pLevel : 0), stat, black);
		}
	}
	return stat;
}

function savePng(file, w, h) {
	const rgb = Buffer.alloc(w * h * 3);
	for (let y = 0; y < h; y++)
		for (let x = 0; x < w; x++) {
			const c = pal[buf[y * B.BUF_W + x]];
			const o = (y * w + x) * 3;
			rgb[o] = c[0]; rgb[o + 1] = c[1]; rgb[o + 2] = c[2];
		}
	B.writePng(file, w, h, rgb);
}
function snapshot() { return buf.slice(); }
function diff(a, b) {                        // сколько точек эталона потеряно / появилось лишних
	let lost = 0, extra = 0;
	for (let i = 0; i < a.length; i++) {
		const pa = a[i] === 255 ? 0 : a[i], pb = b[i] === 255 ? 0 : b[i];
		if (pa && !pb) lost++; else if (!pa && pb) extra++;
	}
	return { lost, extra };
}

// ---------------------------------------------------------------- отряд и ходьба

// волна из (x0,y0): возвращает предков и порядок обхода — по ней строим и путь к цели,
// и выбор новой цели («идти туда, где ещё не были»)
function flood(x0, y0, z) {
	const W = sx * sy, prev = new Int32Array(W).fill(-1), dist = new Int32Array(W).fill(-1);
	const q = [y0 * sx + x0]; dist[q[0]] = 0;
	for (let head = 0; head < q.length; head++) {
		const cur = q[head], cx = cur % sx, cy = (cur / sx) | 0;
		for (let dy = -1; dy <= 1; dy++)
			for (let dx = -1; dx <= 1; dx++) {
				if (!dx && !dy) continue;
				const nx = cx + dx, ny = cy + dy;
				if (nx < 0 || ny < 0 || nx >= sx || ny >= sy) continue;
				const ni = ny * sx + nx;
				if (dist[ni] >= 0 || stepBlockedWalk(cx, cy, nx, ny, z)) continue;
				dist[ni] = dist[cur] + 1; prev[ni] = cur; q.push(ni);
			}
	}
	return { prev, dist, order: q };
}
function pathTo(fl, cell) {
	const p = [];
	for (let cur = cell; cur >= 0; cur = fl.prev[cur]) p.push({ x: cur % sx, y: (cur / sx) | 0 });
	p.reverse();
	return p;
}
function bfsPath(x0, y0, x1, y1, z) {
	const fl = flood(x0, y0, z);
	let best = y0 * sx + x0, bestD = 1e9;
	for (const cell of fl.order) {
		const d = Math.abs(cell % sx - x1) + Math.abs(((cell / sx) | 0) - y1);
		if (d < bestD) { bestD = d; best = cell; }
	}
	return pathTo(fl, best);
}
// новая цель, когда путь кончился: самая дальняя достижимая клетка, предпочтительно неразведанная
function newTarget(u) {
	const fl = flood(u.x, u.y, u.z);
	let best = -1, score = -1;
	for (const cell of fl.order) {
		const x = cell % sx, y = (cell / sx) | 0;
		let d = fl.dist[cell];
		if (!disc[idx(x, y, u.z)]) d += 30;
		if (d > score) { score = d; best = cell; }
	}
	return best >= 0 ? pathTo(fl, best) : [{ x: u.x, y: u.y }];
}

function dirOf(dx, dy) {                     // 8 направлений оригинала: 0 = север, по часовой
	const t = [[7, 0, 1], [6, -1, 2], [5, 4, 3]];
	return t[dy + 1][dx + 1];
}

function findOpen(x0, y0, z) {
	let best = null, bestD = 1e9;
	for (let y = 0; y < sy; y++)
		for (let x = 0; x < sx; x++) {
			if (!walk[idx(x, y, z)]) continue;
			let free = 0;
			for (let dy = -1; dy <= 1; dy++) for (let dx = -1; dx <= 1; dx++)
				if (inMap(x + dx, y + dy, z) && walk[idx(x + dx, y + dy, z)]) free++;
			if (free < 8) continue;
			const d = (x - x0) * (x - x0) + (y - y0) * (y - y0);
			if (d < bestD) { bestD = d; best = { x, y }; }
		}
	return best || { x: x0, y: y0 };
}

const start = findOpen(6, 6, 0);
const goal = findOpen(sx - 7, sy - 7, 0);
const units = [];
for (let i = 0; i < squadN; i++) {
	const ox = (i % 3) - 1, oy = ((i / 3) | 0) - 1;
	let x = Math.min(sx - 1, Math.max(0, start.x + ox)), y = Math.min(sy - 1, Math.max(0, start.y + oy));
	if (!walk[idx(x, y, 0)]) { x = start.x; y = start.y; }
	units.push({ x, y, z: 0, dir: dirOf(Math.sign(goal.x - x), Math.sign(goal.y - y)), path: null, pi: 0 });
}
for (const u of units) { u.path = bfsPath(u.x, u.y, goal.x, goal.y, 0); u.pi = 0; }

// ---------------------------------------------------------------- прогон

const level = wantLevel !== null ? Math.min(parseInt(wantLevel, 10), sz - 1) : 0;
fs.mkdirSync(outDir, { recursive: true });

console.log(`${game} ${terrain}: карта ${sx}x${sy}x${sz}, отряд ${squadN}, ${stepsPerTurn} клеток за ход, ` +
	`${night ? 'ночь (globalShade 15)' : 'день (globalShade 0)'}, камера на ярусе ${level}`);
console.log(`высадка ${start.x},${start.y} -> цель ${goal.x},${goal.y}`);

const report = [];
for (let turn = 0; turn <= turnsTotal; turn++) {
	vis.fill(0);
	for (const u of units) {
		if (turn && u.pi + 1 >= u.path.length) { u.path = newTarget(u); u.pi = 0; }
		for (let s = 0; turn && s < stepsPerTurn; s++) {
			calcFOV(u.x, u.y, u.z, u.dir, false);
			if (u.pi + 1 >= u.path.length) break;
			const nx = u.path[u.pi + 1];
			u.dir = dirOf(Math.sign(nx.x - u.x), Math.sign(nx.y - u.y));
			u.x = nx.x; u.y = nx.y; u.pi++;
		}
	}
	for (const u of units) calcFOV(u.x, u.y, u.z, u.dir, true);    // итоговая видимость хода
	calcLight(units);

	// камера — на первого бойца (Camera::centerOnPosition)
	const cam = units[0];
	const ox = Math.round(VIEW_W / 2 - (cam.x - cam.y) * B.HALF_W);
	const oy = Math.round(VIEW_H / 2 - ((cam.x + cam.y) * B.QUART - level * B.LEVEL_H));
	const cells = viewCells(level, ox, oy);

	let discAll = 0, visAll = 0;
	for (let i = 0; i < N; i++) { if (disc[i]) discAll++; if (vis[i]) visAll++; }
	let dView = 0, vView = 0;
	for (const c of cells) { if (disc[c.i]) dView++; if (vis[c.i]) vView++; }

	const res = {};
	const sFull = render(cells, 'full'); const imgFull = snapshot();
	const sOrig = render(cells, 'fogorig'); const imgOrig = snapshot();
	const sFog = render(cells, 'fog'); const imgFog = snapshot();
	const sMask = render(cells, 'fogmask'); const imgMask = snapshot();
	const sFloor = render(cells, 'floors');
	const sWalls = render(cells, 'walls');
	res.full = sFull; res.orig = sOrig; res.fog = sFog; res.mask = sMask; res.floors = sFloor; res.walls = sWalls;
	res.dFog = diff(imgOrig, imgFog); res.dMask = diff(imgOrig, imgMask);
	res.turn = turn; res.cells = cells.length; res.dView = dView; res.vView = vView;
	res.discAll = discAll; res.visAll = visAll;

	// затенение: сколько разных пар (кадр, тень) нужно на экране
	const shadeKeys = (q, extraFog) => {
		const set = new Map();
		for (const c of cells) {
			if (!disc[c.i]) continue;
			let sh = shade[c.i];
			if (extraFog && !vis[c.i]) sh = Math.min(15, sh + 6);
			const lv = q === 16 ? sh : [0, 4, 8, 12, 15].reduce((a, b) => Math.abs(b - sh) < Math.abs(a - sh) ? b : a);
			for (let k = 0; k < 4; k++) {
				if (!c.p[k]) continue;
				const r0 = rec(c.p[k]);
				const fi = r0.base + r0.frame[0];
				const key = fi * 32 + lv;
				if (!set.has(key)) set.set(key, B.TILE_W * frameRows(frames[fi]).h);
			}
		}
		let bytes = 0; for (const v of set.values()) bytes += v;
		return { combos: set.size, bytes };
	};
	const uniqFrames = new Set();
	for (const c of cells) if (disc[c.i]) for (let k = 0; k < 4; k++) if (c.p[k]) {
		const r0 = rec(c.p[k]); uniqFrames.add(r0.base + r0.frame[0]);
	}
	res.frames = uniqFrames.size;
	res.sh16 = shadeKeys(16, false);
	res.sh5 = shadeKeys(5, false);
	res.sh5fog = shadeKeys(5, true);

	// приоритеты прогрессивной отрисовки: рядом с активным бойцом / центр экрана / остальное
	const prio = [0, 0, 0];
	for (const c of cells) {
		if (!disc[c.i]) continue;
		let b = 0;
		for (let k = 0; k < 4; k++) if (c.p[k]) {
			const r0 = rec(c.p[k]); b += B.TILE_W * frameRows(frames[r0.base + r0.frame[0]]).h;
		}
		const d = Math.max(Math.abs(c.x - cam.x), Math.abs(c.y - cam.y));
		prio[d <= 4 ? 0 : d <= 9 ? 1 : 2] += b;
	}
	res.prio = prio;

	// камера уехала от отряда: среднее по сетке 4x4 положений камеры по всей карте
	const avg = { full: 0, fog: 0, fullB: 0, fogB: 0, n: 0, dv: 0, cells: 0 };
	for (let gy = 0; gy < 4; gy++)
		for (let gx = 0; gx < 4; gx++) {
			const ax = Math.round((gx + 0.5) * sx / 4), ay = Math.round((gy + 0.5) * sy / 4);
			const aox = Math.round(VIEW_W / 2 - (ax - ay) * B.HALF_W);
			const aoy = Math.round(VIEW_H / 2 - ((ax + ay) * B.QUART - level * B.LEVEL_H));
			const ac = viewCells(level, aox, aoy);
			const f = render(ac, 'full'), g = render(ac, 'fogmask');
			avg.full += f.blits; avg.fullB += f.bytes; avg.fog += g.blits; avg.fogB += g.bytes;
			avg.cells += ac.length;
			for (const c of ac) if (disc[c.i]) avg.dv++;
			avg.n++;
		}
	res.avg = avg;

	report.push(res);

	if (turn === 0 || turn === 1 || turn === 3 || turn === 5) {
		const tag = `${terrain}_L${level}${night ? '_night' : ''}_t${turn}`;
		render(cells, 'full'); savePng(path.join(outDir, tag + '_full.png'), VIEW_W, VIEW_H);
		render(cells, 'fogorig'); savePng(path.join(outDir, tag + '_fogorig.png'), VIEW_W, VIEW_H);
		render(cells, 'fogmask'); savePng(path.join(outDir, tag + '_fog.png'), VIEW_W, VIEW_H);
		render(cells, 'floors'); savePng(path.join(outDir, tag + '_lodfloors.png'), VIEW_W, VIEW_H);
		render(cells, 'walls'); savePng(path.join(outDir, tag + '_lodwalls.png'), VIEW_W, VIEW_H);
		// карта разведанного: вид сверху, ярус 0 (белое — видно сейчас, серое — разведано)
		const w = sx, h = sy, rgb = Buffer.alloc(w * h * 3);
		for (let y = 0; y < h; y++) for (let x = 0; x < w; x++) {
			let anyD = 0, anyV = 0;
			for (let z = 0; z < sz; z++) { if (disc[idx(x, y, z)]) anyD = 1; if (vis[idx(x, y, z)]) anyV = 1; }
			const o = (y * w + x) * 3;
			const wall = blkObj[idx(x, y, 0)] || blkW[idx(x, y, 0)] || blkN[idx(x, y, 0)];
			let c = anyV ? [255, 255, 255] : anyD ? [120, 120, 120] : [20, 20, 30];
			if (wall) c = [c[0] * 0.55 | 0, c[1] * 0.55 | 0, (c[2] * 0.55 | 0) + 40];
			rgb[o] = c[0]; rgb[o + 1] = c[1]; rgb[o + 2] = c[2];
		}
		B.writePng(path.join(outDir, tag + '_discovered.png'), w, h, rgb);
	}
}

// ---------------------------------------------------------------- вывод

const kb = b => (b / 1024).toFixed(0);
const pct = (a, b) => b ? (100 * a / b).toFixed(0) + '%' : '-';
console.log('');
console.log('ход  разв.карты  клеток вида  разв.  видно  | блитов полн/туман  КБ полн/туман  экономия  силуэтов  просвечивает');
for (const r of report) {
	console.log(
		String(r.turn).padStart(3) +
		pct(r.discAll, N).padStart(11) +
		String(r.cells).padStart(13) +
		pct(r.dView, r.cells).padStart(7) +
		pct(r.vView, r.cells).padStart(7) +
		'  | ' + (r.full.blits + '/' + r.mask.blits).padStart(16) +
		(kb(r.full.bytes) + '/' + kb(r.mask.bytes)).padStart(14) +
		pct(r.full.bytes - r.mask.bytes, r.full.bytes).padStart(10) +
		String(r.mask.black).padStart(10) +
		(r.dMask.extra + '/' + r.dFog.extra).padStart(17));
}
console.log('  (последняя колонка: «просвечивает» точек против оригинального тумана — с силуэтами / без силуэтов)');

console.log('');
console.log('камера не на отряде (среднее по 16 положениям по всей карте):');
console.log('ход  разв. в окне  блитов полн/туман  КБ полн/туман  экономия');
for (const r of report) {
	const a = r.avg;
	console.log(String(r.turn).padStart(3) + pct(a.dv, a.cells).padStart(13) +
		((a.full / a.n).toFixed(0) + '/' + (a.fog / a.n).toFixed(0)).padStart(19) +
		(kb(a.fullB / a.n) + '/' + kb(a.fogB / a.n)).padStart(15) +
		pct(a.fullB - a.fogB, a.fullB).padStart(10));
}

console.log('');
console.log('ход  кадров  пар (кадр,тень) 16 ур.  КБ кэша  5 ур.  КБ  5 ур.+затенение невидимого  КБ');
for (const r of report)
	console.log(String(r.turn).padStart(3) + String(r.frames).padStart(8) +
		String(r.sh16.combos).padStart(24) + kb(r.sh16.bytes).padStart(9) +
		String(r.sh5.combos).padStart(7) + kb(r.sh5.bytes).padStart(4) +
		String(r.sh5fog.combos).padStart(29) + kb(r.sh5fog.bytes).padStart(4));

console.log('');
console.log('ход  КБ: рядом с бойцом (<=4)  средний круг (<=9)  остальное  всего  кадров по 20 КБ');
for (const r of report) {
	const tot = r.prio[0] + r.prio[1] + r.prio[2];
	console.log(String(r.turn).padStart(3) + kb(r.prio[0]).padStart(24) + kb(r.prio[1]).padStart(20) +
		kb(r.prio[2]).padStart(11) + kb(tot).padStart(7) + (tot / 1024 / 20).toFixed(1).padStart(17));
}

// упрощённый вид: только полы (изометрия) и мини-карта SCANG 4x4 (вид сверху, как в оригинале)
const last = report[report.length - 1];
console.log('');
console.log('упрощённый вид (последний ход):');
console.log(`  полный (туман)      блитов ${last.mask.blits}, ${kb(last.mask.bytes)} КБ`);
console.log(`  только полы         блитов ${last.floors.blits}, ${kb(last.floors.bytes)} КБ ` +
	`(${pct(last.floors.bytes, last.mask.bytes)} байт, ${pct(last.floors.blits, last.mask.blits)} блитов)`);
console.log(`  полы и стены        блитов ${last.walls.blits}, ${kb(last.walls.bytes)} КБ ` +
	`(${pct(last.walls.bytes, last.mask.bytes)} байт, ${pct(last.walls.blits, last.mask.blits)} блитов)`);
// мини-карта SCANG (MiniMapView.cpp:87-95): вид СВЕРХУ, ячейка 4x4, индекс = MCD.ScanG + 35.
// Считаем её стоимость на то же окно 320x144 и рисуем макет (в т. ч. растянутый x4).
try {
	const scangFile = fs.existsSync(path.join(root, 'GEODATA', 'SCANG.DAT'))
		? path.join(root, 'GEODATA', 'SCANG.DAT') : path.join(root, 'TERRAIN', 'SCANG.DAT');
	const scang = fs.readFileSync(scangFile);
	const mw = 80, mh = 36;                   // 320x144 при ячейке 4x4
	const cam = units[0];
	let miniBlits = 0;
	const img = new Uint8Array(mw * 4 * mh * 4);
	for (let z = 0; z <= level; z++)
		for (let cy = 0; cy < mh; cy++)
			for (let cx = 0; cx < mw; cx++) {
				const x = cam.x - (mw >> 1) + cx, y = cam.y - (mh >> 1) + cy;
				if (!inMap(x, y, z)) continue;
				const i = idx(x, y, z);
				for (let k = 0; k < 4; k++) {
					const r0 = rec(part(i, k));
					if (!r0 || !r0.scang) continue;
					const fo = (r0.scang + 35) * 16;
					if (fo + 16 > scang.length) continue;
					miniBlits++;
					const seen = disc[i];
					for (let py = 0; py < 4; py++)
						for (let px = 0; px < 4; px++) {
							const v = scang[fo + py * 4 + px];
							if (!v) continue;
							img[(cy * 4 + py) * mw * 4 + cx * 4 + px] = seen ? v : 15;
						}
				}
			}
	const scale = 4, W = mw * 4 * scale, H = mh * 4 * scale, rgb = Buffer.alloc(W * H * 3);
	for (let y = 0; y < H; y++)
		for (let x = 0; x < W; x++) {
			const c = pal[img[((y / scale) | 0) * mw * 4 + ((x / scale) | 0)]];
			const o = (y * W + x) * 3;
			rgb[o] = c[0]; rgb[o + 1] = c[1]; rgb[o + 2] = c[2];
		}
	B.writePng(path.join(outDir, `${terrain}_L${level}${night ? '_night' : ''}_minimap_x4.png`), W, H, rgb);
	console.log(`  мини-карта SCANG    вид СВЕРХУ, не изометрия: ${mw}x${mh} клеток x ${level + 1} ярус(ов) = ` +
		`${miniBlits} блитов по 16 Б = ${kb(miniBlits * 16)} КБ, но ${miniBlits} запусков DMA ` +
		`(~${(miniBlits * 300 / 286720).toFixed(1)} кадра только на настройку по 300 T)`);
} catch (e) { console.log('  SCANG.DAT не прочитан: ' + e.message); }

// Стоимость въезжающей полосы при аппаратном скролле (GXOffs): считаем клетки, чьи спрайты
// попадают в правые 32 px окна, и клетки, попадающие в нижние 8 px (шаг камеры на клетку)
{
	const cam = units[0];
	const ox = Math.round(VIEW_W / 2 - (cam.x - cam.y) * B.HALF_W);
	const oy = Math.round(VIEW_H / 2 - ((cam.x + cam.y) * B.QUART - level * B.LEVEL_H));
	const cells = viewCells(level, ox, oy);
	const band = (test) => {
		const st = { blits: 0, bytes: 0, cells: 0 };
		for (const c of cells) {
			if (!disc[c.i] || !test(c.s)) continue;
			st.cells++;
			for (let k = 0; k < 4; k++) if (c.p[k]) {
				const r0 = rec(c.p[k]);
				st.blits++; st.bytes += B.TILE_W * frameRows(frames[r0.base + r0.frame[0]]).h;
			}
		}
		return st;
	};
	const vert = band(s => s.sx + B.TILE_W > VIEW_W - 32);
	const horz = band(s => s.sy + B.TILE_H > VIEW_H - 8);
	console.log(`  скролл вбок на клетку: полоса 32 px — ${vert.cells} клеток, ${vert.blits} блитов, ` +
		`${kb(vert.bytes)} КБ (${(vert.bytes / 1024 / 74).toFixed(2)} кадра DMA)`);
	console.log(`  скролл вниз на клетку: полоса 8 px — ${horz.cells} клеток, ${horz.blits} блитов, ` +
		`${kb(horz.bytes)} КБ (${(horz.bytes / 1024 / 74).toFixed(2)} кадра DMA)`);
}
console.log(`  картинки: ${outDir}`);
