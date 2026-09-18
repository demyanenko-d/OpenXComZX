// Тема 3: полы и плоские поверхности — рисовать их не спрайтами, а «подложкой».
//
// Наблюдение: ромбы пола 32x16 замощают экран без щелей и без нахлёста (проверяется здесь же),
// поэтому вместо ~200 блитов с прозрачностью (BLT1, 3 цикла DRAM на слово, из которых половина
// байт — нули) плоскость пола можно закрашивать длинными непрозрачными прогонами RAM->RAM
// (2 цикла) из заранее собранной «обойной» полосы, а одноцветные участки — FILL (1 цикл).
//
// Считаются три варианта (все проверены попиксельно против честной отрисовки):
//   «прогоны»  — полоса-обои на каждый спрайт пола (16 строк периода), прогон на строку экрана;
//   «прямоуг.» — то же, но соседние строки сливаются в прямоугольник (S_ALGN+D_ALGN, до 16 строк);
//   «кэш»      — вся плоскость пола уровня лежит готовой в отдельном буфере (строится один раз),
//                кадр берёт из неё прямоугольники; тип пола и тень уже не важны.
// Ромб выносится в подложку, «остаток» спрайта выше ромба рисуется обычным блитом на своём месте
// в порядке художника (конвертер режет спрайт пола на «ромб» и «остаток»).
//
//   node tools/proto/opt3_floors.js                 # все сцены, сводные таблицы
//   node tools/proto/opt3_floors.js --scene SEABED  # одна сцена + PNG
//   node tools/proto/opt3_floors.js --png           # PNG для всех сцен
//
// Вывод — tmp/proto/opt3/. Метрики: «запуск» = один старт DMA (≈300 T настройки на ассемблере),
// «байт» = сколько байт прочитает/запишет DMA (с обрезкой по окну 320x144),
// «цикл» = циклы DRAM: BLT1 3/слово, RAM->RAM 2/слово, FILL 1/слово (02 §6).
'use strict';
const fs = require('fs');
const path = require('path');
const B = require('./battle');

// battle.js даёт путь 'Steam/X-COM UFO Defense/XCOM', а каталог у нас 'Steam/XCom UFO Defense/XCOM'
if (!fs.existsSync(B.GAMES.UFO)) for (const d of fs.readdirSync('Steam'))
	if (/ufo/i.test(d) && fs.existsSync(path.join('Steam', d, 'XCOM', 'TERRAIN'))) B.GAMES.UFO = path.join('Steam', d, 'XCOM');

function arg(name, def) {
	const i = process.argv.indexOf('--' + name);
	return i > 0 && i + 1 < process.argv.length ? process.argv[i + 1] : def;
}
const has = (name) => process.argv.indexOf('--' + name) > 0;

// ---------------------------------------------------------------- константы бюджета

const VIEW_W = B.VIEW_W, VIEW_H = B.VIEW_H;      // окно карты 320x144
const FRAME_T = 286720;                          // тактов Z80 в кадре на 14 МГц
const FRAME_DRAM = 111000;                       // циклов DRAM на кадр, остающихся DMA (02 §6)
const T_PER_DRAM = FRAME_T / FRAME_DRAM;         // 2.58 T на цикл DRAM — для сведения в одну шкалу
const SETUP_T = 300;                             // настройка DMA на ассемблере (08/16 §8.3)
const CYC = { blt1: 3, copy: 2, fill: 1 };       // циклов DRAM на слово

// ---------------------------------------------------------------- геометрия ромба

// Ромб пола лежит в нижних 16 строках кадра 32x40 (строки 24..39). Ряд 0 пуст,
// ряды 1..8 расширяются 4,8,..,32, ряды 9..15 сужаются 28,..,4 — итого ровно 256 точек.
const DIA = [];
for (let r = 0; r < 16; r++) {
	if (r === 0) DIA.push(null);
	else if (r <= 8) DIA.push([16 - 2 * r, 15 + 2 * r]);
	else DIA.push([2 * (r - 8), 31 - 2 * (r - 8)]);
}
const DIA_AREA = DIA.reduce((s, v) => s + (v ? v[1] - v[0] + 1 : 0), 0);
const FLOOR_TOP = 24;                            // строка кадра, с которой начинается ромб

// маска ромба в координатах кадра 32x40
const DIA_MASK = new Uint8Array(32 * 40);
for (let r = 0; r < 16; r++) if (DIA[r]) for (let x = DIA[r][0]; x <= DIA[r][1]; x++) DIA_MASK[(FLOOR_TOP + r) * 32 + x] = 1;
// нижние («ближние») углы коробки 32x16: они принадлежат ромбам соседей x+1,y и x,y+1
const DIA_LOWCORNER = new Uint8Array(32 * 40);
for (let r = 8; r < 16; r++)
	for (let x = 0; x < 32; x++) {
		if (DIA_MASK[(FLOOR_TOP + r) * 32 + x]) continue;
		DIA_LOWCORNER[(FLOOR_TOP + r) * 32 + x] = 1;
	}

// ---------------------------------------------------------------- сцены

const SETS = {
	TFTD: {
		SEABED: 'BLANKS,SAND,ROCKS,WEEDS,DEBRIS,UFOBITS', CORAL: 'BLANKS,SAND,WEEDS,CORAL,ROCKS',
		ALART: 'BLANKS,SAND,ROCKS,WEEDS,PYRAMID,UFOBITS', VOLC: 'BLANKS,SAND,VOLC,UFOBITS',
		PORT: 'BLANKS,SEA,PORT01,PORT02', ISLAND: 'BLANKS,SEA,ISLAND1,ISLAND2,ISLAND3',
		CARGO: 'BLANKS,SEA,CARGO1,CARGO2,XBITS,CARGO3',
		XBASES: 'BLANKS,XBASES01,XBASES02,XBASES03,XBASES04,XBASES05,XBITS',
		GRUNGE: 'BLANKS,GRUNGE1,GRUNGE2,GRUNGE3,GRUNGE4,GRUNGE5,UFOBITS',
		LINERB: 'BLANKS,SEA,LINERA,LINERB,LINERC,LINERD,DECKC',
	},
	UFO: {
		CULTA: 'BLANKS,CULTIVAT,BARN', DESERT: 'BLANKS,DESERT', FOREST: 'BLANKS,FOREST',
		JUNGLE: 'BLANKS,JUNGLE', MOUNT: 'BLANKS,MOUNT', POLAR: 'BLANKS,POLAR',
		URBAN: 'BLANKS,ROADS,URBITS,URBAN,FRNITURE', UBASE: 'BLANKS,U_BASE,U_WALL02,U_PODS,BRAIN',
		XBASE: 'BLANKS,XBASE1,XBASE2', MARS: 'BLANKS,MARS,U_WALL02',
	},
};

const SCENES = [
	{ id: 'SEABED', game: 'TFTD', terrain: 'SEABED', prefix: 'SEABED', level: 0 },
	{ id: 'CORAL', game: 'TFTD', terrain: 'CORAL', prefix: 'CORAL', level: 0 },
	{ id: 'ALART3', game: 'TFTD', terrain: 'ALART', prefix: 'ALART', level: 3 },
	{ id: 'VOLC', game: 'TFTD', terrain: 'VOLC', prefix: 'VOLC', level: 0 },
	{ id: 'PORT1', game: 'TFTD', terrain: 'PORT', prefix: 'PORT', level: 1 },
	{ id: 'ISLAND', game: 'TFTD', terrain: 'ISLAND', prefix: 'ISLAND', level: 0 },
	{ id: 'CARGO1', game: 'TFTD', terrain: 'CARGO', prefix: 'CARGO', level: 1 },
	{ id: 'XBASES', game: 'TFTD', terrain: 'XBASES', prefix: 'XBASES', level: 0 },
	{ id: 'GRUNGE', game: 'TFTD', terrain: 'GRUNGE', prefix: 'GRUNGE', level: 0 },
	{ id: 'LINERB1', game: 'TFTD', terrain: 'LINERB', prefix: 'LINERB', level: 1 },
	{ id: 'CULTA', game: 'UFO', terrain: 'CULTA', prefix: 'CULTA', level: 0 },
	{ id: 'DESERT', game: 'UFO', terrain: 'DESERT', prefix: 'DESERT', level: 0 },
	{ id: 'FOREST', game: 'UFO', terrain: 'FOREST', prefix: 'FOREST', level: 0 },
	{ id: 'MOUNT', game: 'UFO', terrain: 'MOUNT', prefix: 'MOUNT', level: 0 },
	{ id: 'URBAN1', game: 'UFO', terrain: 'URBAN', prefix: 'URBAN', level: 1 },
	{ id: 'UBASE', game: 'UFO', terrain: 'UBASE', prefix: 'UBASE_', level: 0 },
	{ id: 'XBASE', game: 'UFO', terrain: 'XBASE', prefix: 'XBASE_', level: 0 },
	{ id: 'MARS', game: 'UFO', terrain: 'MARS', prefix: 'MARS', level: 0 },
];

// ---------------------------------------------------------------- загрузка

const terrainCache = new Map();
function loadTerrain(game, terrain) {
	const key = game + '/' + terrain;
	if (terrainCache.has(key)) return terrainCache.get(key);
	const root = B.GAMES[game], dir = path.join(root, 'TERRAIN');
	const parts = [], frames = [];
	for (const s of SETS[game][terrain].split(',')) {
		const mcd = B.readMcd(fs.readFileSync(path.join(dir, s + '.MCD')));
		const pck = B.readPck(fs.readFileSync(path.join(dir, s + '.PCK')), fs.readFileSync(path.join(dir, s + '.TAB')));
		const base = frames.length;
		for (const r of mcd) { r.set = s; r.base = base; parts.push(r); }
		for (const f of pck) frames.push(f);
	}
	const v = { parts, frames };
	terrainCache.set(key, v);
	return v;
}

function buildMap(game, prefix, n) {
	const dir = path.join(B.GAMES[game], 'MAPS');
	const names = fs.readdirSync(dir).filter(f => f.startsWith(prefix) && f.endsWith('.MAP')).sort();
	let blocks = names.map(f => B.readMap(fs.readFileSync(path.join(dir, f))));
	const bw = blocks[0].sx, bh = blocks[0].sy;
	blocks = blocks.filter(b => b.sx === bw && b.sy === bh);
	if (n <= 1) return blocks[0];
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

// ---------------------------------------------------------------- сцена

class Scene {
	constructor(sc, nblocks) {
		const t = loadTerrain(sc.game, sc.terrain);
		this.sc = sc; this.parts = t.parts; this.frames = t.frames;
		this.map = buildMap(sc.game, sc.prefix, nblocks);
		this.level = Math.min(sc.level, this.map.sz - 1);
		const cx = this.map.sx / 2, cy = this.map.sy / 2;
		this.ox = Math.round(VIEW_W / 2 - (cx - cy) * B.HALF_W);
		this.oy = Math.round(VIEW_H / 2 - ((cx + cy) * B.QUART - this.level * B.LEVEL_H));
		this.info = new Map();                       // id части -> разбор спрайта
		this.buf = new Uint8Array(B.BUF_W * B.BUF_H);
	}
	cell(x, y, z) {
		const o = ((z * this.map.sy + y) * this.map.sx + x) * 4;
		return [this.map.cells[o], this.map.cells[o + 1], this.map.cells[o + 2], this.map.cells[o + 3]];
	}
	sx(x, y, z) { return (x - y) * B.HALF_W + this.ox; }
	sy(x, y, z) { return (x + y) * B.QUART - z * B.LEVEL_H + this.oy; }
	// разбор кадра части: габариты, «ромбовость», непрозрачность
	part(id) {
		if (this.info.has(id)) return this.info.get(id);
		const rec = this.parts[id - 1];
		const img = rec ? this.frames[rec.base + rec.frame[0]] : null;
		const yoff = rec ? rec.pLevel : 0;
		let top = 40, bottom = -1, inDia = 0, outDia = 0, lowCorner = 0, lowEff = 0, opaque = 0;
		let ex0 = 32, ex1 = -1, ey0 = 40, ey1 = -1;
		if (img) for (let y = 0; y < 40; y++)
			for (let x = 0; x < 32; x++)
				if (img[y * 32 + x]) {
					opaque++;
					if (y < top) top = y;
					if (y > bottom) bottom = y;
					if (DIA_MASK[y * 32 + x]) inDia++;
					else {
						outDia++;
						if (!DIA_LOWCORNER[y * 32 + x]) {          // «остаток» — только то, что выше ромба
							if (x < ex0) ex0 = x;
							if (x > ex1) ex1 = x;
							if (y < ey0) ey0 = y;
							if (y > ey1) ey1 = y;
						}
					}
					if (DIA_LOWCORNER[y * 32 + x]) lowCorner++;
					const ys = y - yoff;                        // с учётом подъёма спрайта (P_Level)
					if (ys >= 0 && DIA_LOWCORNER[ys * 32 + x]) lowEff++;
				}
		// одинаковые по точкам кадры из разных наборов — один и тот же «обой»
		let hash = 5381;
		if (img) for (let i = 0; i < img.length; i++) hash = ((hash * 33) ^ img[i]) >>> 0;
		if (!this.canon) this.canon = new Map();
		if (!this.canon.has(hash)) this.canon.set(hash, id);
		// плотный габарит и «сплошной» ли он — сплошной кусок можно лить RAM->RAM (2 цикла)
		let bx0 = 32, bx1 = -1;
		if (img) for (let y = 0; y < 40; y++)
			for (let x = 0; x < 32; x++)
				if (img[y * 32 + x]) { if (x < bx0) bx0 = x; if (x > bx1) bx1 = x; }
		let solid = bx1 >= 0;
		if (solid) for (let y = top; y <= bottom && solid; y++)
			for (let x = bx0; x <= bx1; x++) if (!img[y * 32 + x]) { solid = false; break; }
		const v = {
			id, rec, img, yoff, sprite: this.canon.get(hash),
			box: bx1 < 0 ? null : { x: bx0, y: top, w: bx1 - bx0 + 1, h: bottom - top + 1 }, solid,
			top: bottom < 0 ? 0 : top, h: bottom < 0 ? 0 : bottom - top + 1,
			opaque, inDia, outDia, lowCorner, lowEff,
			ex: ex1 < 0 ? null : { x: ex0, y: ey0, w: ex1 - ex0 + 1, h: ey1 - ey0 + 1 },
			diaExact: inDia === DIA_AREA && outDia === 0,      // ровно ромб: годен в подложку
			diaFull: inDia === DIA_AREA,                       // ромб цел, но есть «лишнее» сверху
		};
		this.info.set(id, v);
		return v;
	}
}

// ---------------------------------------------------------------- блит и учёт

function clipRect(x0, y0, w, h) {                  // обрезка по окну карты
	const x1 = Math.min(x0 + w, VIEW_W), y1 = Math.min(y0 + h, VIEW_H);
	const cx0 = Math.max(x0, 0), cy0 = Math.max(y0, 0);
	return (cx0 >= x1 || cy0 >= y1) ? null : { x: cx0, y: cy0, w: x1 - cx0, h: y1 - cy0 };
}

function newStat() { return { setups: 0, bytes: 0, dram: 0, blits: 0, runs: 0, rects: 0, fills: 0, fillBytes: 0, byClass: [0, 0, 0, 0], bytesByClass: [0, 0, 0, 0] }; }
function account(st, bytes, mode) {
	st.setups++; st.bytes += bytes; st.dram += Math.ceil(bytes / 2) * CYC[mode];
}

// блит части тайла с прозрачностью (BLT1). box — обрезанный кусок спрайта (для «остатка» пола),
// в этом случае точки внутри ромба пропускаются: ромб уже положен подложкой.
function blitPart(sc, buf, info, px, py, st, cls, box, trimLow) {
	if (!info.img || !info.h) return;
	const r = box ? clipRect(px + box.x, py + box.y, box.w, box.h) : clipRect(px, py + info.top, 32, info.h);
	if (!r) return;
	if (st) { account(st, r.w * r.h, 'blt1'); st.blits++; if (cls !== undefined) { st.byClass[cls]++; st.bytesByClass[cls] += r.w * r.h; } }
	for (let y = 0; y < r.h; y++) {
		const sy = (r.y - py) + y;
		for (let x = 0; x < r.w; x++) {
			const sx = r.x - px + x;
			const v = info.img[sy * 32 + sx];
			// «остаток» пола: ромб уже в подложке, ближние углы всё равно перекроет сосед
			if (!v || ((box || trimLow) && DIA_LOWCORNER[sy * 32 + sx]) || (box && DIA_MASK[sy * 32 + sx])) continue;
			buf[(r.y + y) * B.BUF_W + (r.x + x)] = v;
		}
	}
}

// ---------------------------------------------------------------- честная отрисовка (эталон)

function renderRef(sc, st) {
	sc.buf.fill(0);
	for (let z = 0; z <= sc.level; z++)
		for (let x = 0; x < sc.map.sx; x++)
			for (let y = 0; y < sc.map.sy; y++) {
				const px = sc.sx(x, y, z), py = sc.sy(x, y, z);
				if (px <= -32 || px >= VIEW_W || py <= -40 || py >= VIEW_H) continue;
				const p = sc.cell(x, y, z);
				for (let k = 0; k < 4; k++) {
					if (!p[k]) continue;
					const info = sc.part(p[k]);
					blitPart(sc, sc.buf, info, px, py - info.yoff, st, k);
				}
			}
	return sc.buf.slice();
}

// ---------------------------------------------------------------- «обойная» полоса

// Плоскость пола из одинаковых ромбов периодична по решётке (32,0),(16,8), а значит и по
// (32,0),(0,16): полосу достаточно хранить 16 строк x 32 байта и повторять по X.
// Строится отрисовкой однородного поля — так проверяется и сама периодичность.
const stripCache = new Map();
function buildStrip(sc, id, width, whole) {
	const key = id + ':' + width + ':' + (whole ? 1 : 0);
	if (stripCache.has(key)) return stripCache.get(key);
	const info = sc.part(id);
	const W = 192, H = 128, tmp = new Uint8Array(W * H);
	// коробка тайла (a,b): (16a + 64, 8b + 48); a≡b (mod 2); порядок художника — по b
	for (let b = -8; b <= 16; b++)
		for (let a = -8; a <= 16; a++) {
			if (((a - b) & 1) !== 0) continue;
			const bx = 16 * a + 64, by = 8 * b + 48;      // левый верх коробки ромба
			for (let r = whole ? -FLOOR_TOP : 0; r < 16; r++) {
				const sy = FLOOR_TOP + r, dy = by + r;
				if (sy < 0 || dy < 0 || dy >= H) continue;
				for (let x = 0; x < 32; x++) {
					const v = info.img[sy * 32 + x];
					if (!v) continue;
					if (!whole && !DIA_MASK[sy * 32 + x]) continue;   // в подложку идёт только ромб
					const dx = bx + x;
					if (dx < 0 || dx >= W) continue;
					tmp[dy * W + dx] = v;
				}
			}
		}
	// ячейка 16x32 с началом в (64,48) = коробка тайла (0,0)
	const strip = new Uint8Array(16 * width);
	let ok = true;
	for (let r = 0; r < 16; r++)
		for (let c = 0; c < width; c++) {
			const v = tmp[(48 + r) * W + (64 + (c % 32))];
			strip[r * width + c] = v;
			if (c < 32 && tmp[(48 + r) * W + (64 + 32 + c)] !== v) ok = false;   // проверка периода
		}
	const v = { strip, width, periodic: ok };
	stripCache.set(key, v);
	return v;
}

// ---------------------------------------------------------------- план подложки

// В подложку выносится ТОЛЬКО ромб пола. Условия:
//  - спрайт пола закрывает свой ромб целиком (diaFull) и не залезает в «ближние» углы коробки;
//  - пол не сдвинут по вертикали (P_Level == 0);
//  - ни одна часть двух дальних соседей (x-1,y) и (x,y-1) не пишет в ближние углы своей коробки,
//    то есть никто из нарисованных раньше не залезает в этот ромб (иначе порядок художника
//    нарушится: подложка кладётся раньше всех стен уровня).
// «Лишнее» выше ромба (окантовка, бугры) рисуется отдельным маленьким блитом на своём месте
// в порядке художника — конвертер режет спрайт пола на «ромб» и «остаток» (SPRSET умеет обрезку).
function diaOk(sc, id) {
	if (!id) return false;
	const info = sc.part(id);
	return info.diaFull && info.yoff === 0 && info.lowEff === 0;
}
function blocksNear(sc, x, y, z) {                 // есть ли у клетки часть, лезущая в ближние ромбы
	if (x < 0 || y < 0 || x >= sc.map.sx || y >= sc.map.sy) return false;
	const p = sc.cell(x, y, z);
	for (let k = 0; k < 4; k++) {
		if (!p[k] || !sc.part(p[k]).lowEff) continue;
		// у пола такие точки всё равно не видны, если у ближних соседей пол цел — конвертер
		// режет их из спрайта пола (остаётся отдельным кусочком для редкого случая обрыва)
		if (k === 0 && lowHidden(sc, x, y, z)) continue;
		return true;
	}
	return false;
}
// Точки пола, залезшие в ближние углы коробки, всегда перекрыты ромбами соседей (x+1,y) и (x,y+1),
// если у тех пол цел — тогда их можно просто не рисовать, и клетка годится в подложку.
function lowHidden(sc, x, y, z) {
	for (const [dx, dy] of [[1, 0], [0, 1]]) {
		const nx = x + dx, ny = y + dy;
		if (nx >= sc.map.sx || ny >= sc.map.sy) return false;
		const nid = sc.cell(nx, ny, z)[0];
		if (!nid) return false;
		const ni = sc.part(nid);
		if (!ni.diaFull || ni.yoff) return false;
	}
	return true;
}
function qualifies(sc, x, y, z) {
	const id = sc.cell(x, y, z)[0];
	if (!id) return false;
	const info = sc.part(id);
	if (!info.diaFull || info.yoff) return false;
	if (info.lowEff && !lowHidden(sc, x, y, z)) return false;
	if (blocksNear(sc, x - 1, y, z) || blocksNear(sc, x, y - 1, z)) return false;
	return true;
}

// Связные группы клеток с одинаковым СПРАЙТОМ пола (полоса строится на спрайт, а не на запись MCD)
function components(sc, z) {
	const { sx, sy } = sc.map;
	const mark = new Uint8Array(sx * sy);
	const comps = [];
	const inView = (x, y) => {
		const px = sc.sx(x, y, z), py = sc.sy(x, y, z) + FLOOR_TOP;
		return !(px <= -32 || px >= VIEW_W || py <= -16 || py >= VIEW_H);
	};
	const spr = (x, y) => { const id = sc.cell(x, y, z)[0]; return id ? sc.part(id).sprite : -1; };
	for (let y = 0; y < sy; y++)
		for (let x = 0; x < sx; x++) {
			if (mark[y * sx + x]) continue;
			if (!qualifies(sc, x, y, z) || !inView(x, y)) continue;
			const s = spr(x, y), list = [], q = [[x, y]];
			mark[y * sx + x] = 1;
			while (q.length) {
				const [cx, cy] = q.pop();
				list.push([cx, cy]);
				for (const [dx, dy] of [[1, 0], [-1, 0], [0, 1], [0, -1]]) {
					const nx = cx + dx, ny = cy + dy;
					if (nx < 0 || ny < 0 || nx >= sx || ny >= sy) continue;
					if (mark[ny * sx + nx]) continue;
					if (spr(nx, ny) !== s || !qualifies(sc, nx, ny, z) || !inView(nx, ny)) continue;
					mark[ny * sx + nx] = 1;
					q.push([nx, ny]);
				}
			}
			comps.push({ id: sc.cell(x, y, z)[0], sprite: s, z, tiles: list });
		}
	return comps;
}

// Вариант «кэш плоскости»: вся плоскость пола уровня лежит готовой в отдельном буфере
// (строится один раз при входе/смене этажа), кадр берёт из неё прямоугольники — тип пола
// уже не важен, прогоны не рвутся на границах текстур.
function planeGroup(sc, z) {
	const { sx, sy } = sc.map;
	const mark = new Uint8Array(sx * sy);
	const inView = (x, y) => {
		const px = sc.sx(x, y, z), py = sc.sy(x, y, z) + FLOOR_TOP;
		return !(px <= -32 || px >= VIEW_W || py <= -16 || py >= VIEW_H);
	};
	const ok = (x, y) => qualifies(sc, x, y, z) && inView(x, y);
	const comps = [];
	for (let y = 0; y < sy; y++)
		for (let x = 0; x < sx; x++) {
			if (mark[y * sx + x] || !ok(x, y)) continue;
			const list = [], q = [[x, y]];
			mark[y * sx + x] = 1;
			while (q.length) {
				const [cx, cy] = q.pop();
				list.push([cx, cy]);
				for (const [dx, dy] of [[1, 0], [-1, 0], [0, 1], [0, -1]]) {
					const nx = cx + dx, ny = cy + dy;
					if (nx < 0 || ny < 0 || nx >= sx || ny >= sy || mark[ny * sx + nx] || !ok(nx, ny)) continue;
					mark[ny * sx + nx] = 1;
					q.push([nx, ny]);
				}
			}
			comps.push({ id: sc.cell(x, y, z)[0], sprite: -1, z, tiles: list, cache: true });
		}
	return comps;
}

// Прогоны по строкам экрана для набора тайлов одного пола
function runsOf(sc, comp) {
	const rows = new Map();                       // экранная строка -> список отрезков
	for (const [x, y] of comp.tiles) {
		const bx = sc.sx(x, y, comp.z), by = sc.sy(x, y, comp.z) + FLOOR_TOP;
		for (let r = 1; r < 16; r++) {
			const R = by + r;
			if (R < 0 || R >= VIEW_H) continue;
			let x0 = bx + DIA[r][0], x1 = bx + DIA[r][1] + 1;
			if (x0 < 0) x0 = 0;
			if (x1 > VIEW_W) x1 = VIEW_W;
			if (x0 >= x1) continue;
			if (!rows.has(R)) rows.set(R, []);
			rows.get(R).push([x0, x1]);
		}
	}
	const runs = [];
	for (const [R, segs] of rows) {
		segs.sort((a, b) => a[0] - b[0]);
		let cur = segs[0].slice();
		for (let i = 1; i < segs.length; i++) {
			if (segs[i][0] <= cur[1]) cur[1] = Math.max(cur[1], segs[i][1]);
			else { runs.push({ R, x0: cur[0], x1: cur[1] }); cur = segs[i].slice(); }
		}
		runs.push({ R, x0: cur[0], x1: cur[1] });
	}
	runs.sort((a, b) => a.R - b.R || a.x0 - b.x0);
	return runs;
}

// Разбор области на прямоугольники: DMA с S_ALGN+D_ALGN (шаг 512) берёт до 16 строк полосы
// за один запуск, пока строка полосы не перевалит через границу периода. Жадно: берём верхний
// левый отрезок и тянем вниз, пока пересечение остаётся широким.
function decompose(runs, phaseRow, maxH) {
	const rows = new Map();
	for (const r of runs) {
		if (!rows.has(r.R)) rows.set(r.R, []);
		rows.get(r.R).push([r.x0, r.x1]);
	}
	for (const list of rows.values()) list.sort((a, b) => a[0] - b[0]);
	const order = [...rows.keys()].sort((a, b) => a - b);
	const rects = [];
	const cut = (R, x0, x1) => {            // вырезать [x0,x1) из отрезков строки R
		const list = rows.get(R); if (!list) return;
		for (let i = 0; i < list.length; i++) {
			const s = list[i];
			if (s[1] <= x0 || s[0] >= x1) continue;
			const left = [s[0], x0], right = [x1, s[1]];
			const rep = [];
			if (left[1] > left[0]) rep.push(left);
			if (right[1] > right[0]) rep.push(right);
			list.splice(i, 1, ...rep);
			i += rep.length - 1;
		}
		if (!list.length) rows.delete(R);
	};
	for (const R of order) {
		while (rows.has(R) && rows.get(R).length) {
			const [x0, x1] = rows.get(R)[0];
			let cx0 = x0, cx1 = x1, h = 1;
			while (h < maxH) {
				const R2 = R + h;
				if (phaseRow(R2) !== (phaseRow(R) + h)) break;       // перевал через период полосы
				const list = rows.get(R2);
				if (!list) break;
				let best = null;
				for (const s of list) {
					const a = Math.max(cx0, s[0]), b = Math.min(cx1, s[1]);
					if (b - a > 0 && (!best || b - a > best[1] - best[0])) best = [a, b];
				}
				if (!best || (best[1] - best[0]) * 4 < (cx1 - cx0) * 3) break;
				cx0 = best[0]; cx1 = best[1]; h++;
			}
			rects.push({ R, h, x0: cx0, x1: cx1 });
			for (let k = 0; k < h; k++) cut(R + k, cx0, cx1);
		}
	}
	return rects;
}

// ---------------------------------------------------------------- отрисовка подложкой

function renderUnder(sc, st, opt) {
	sc.buf.fill(0);
	const plan = { compUnder: 0, compBlit: 0, tilesUnder: 0, tilesBlit: 0, exBlits: 0, runs: 0, rects: 0, monoRuns: 0, monoBytes: 0, planeBytes: 0, runLen: [], rectH: [] };
	for (let z = 0; z <= sc.level; z++) {
		const comps = opt.order ? [] : (opt.cache ? planeGroup(sc, z) : components(sc, z));
		const under = new Set();
		for (const c of comps) { plan.compAll = (plan.compAll || 0) + 1; plan.tilesAll = (plan.tilesAll || 0) + c.tiles.length; }
		for (const c of comps) {
			const runs = runsOf(sc, c);
			const oy0 = sc.sy(0, 0, z) + FLOOR_TOP;                    // фаза плоскости по Y
			const phaseRow = (R) => (((R - oy0) % 16) + 16) % 16;
			const rects = decompose(runs.map(r => ({ ...r })), opt.cache ? (R => R) : phaseRow, opt.cache ? 256 : 16);
			// стоимость: подложка (RAM->RAM, при одноцветности FILL) против блитов BLT1
			let uBytes = 0;
			for (const r of runs) uBytes += (r.x1 - r.x0);
			const useRects = (opt.rects || opt.cache) && rects.length <= runs.length;
			const uSetups = useRects ? rects.length : runs.length;
			let eBytes = 0, eSetups = 0, bBytes = 0, bSetups = 0;
			const inC = new Set(c.tiles.map(([a, b]) => b * sc.map.sx + a));
			for (const [x, y] of c.tiles) {
				const info = sc.part(sc.cell(x, y, z)[0]);
				// в режиме кэша «остаток» уже лежит в плоскости, если оба дальних соседа в области
				const exFree = opt.cache && inC.has(y * sc.map.sx + x - 1) && inC.has((y - 1) * sc.map.sx + x);
				if (info.ex && !exFree) {
					const re = clipRect(sc.sx(x, y, z) + info.ex.x, sc.sy(x, y, z) + info.ex.y, info.ex.w, info.ex.h);
					if (re) { eBytes += re.w * re.h; eSetups++; }
				}
				const rr = clipRect(sc.sx(x, y, z), sc.sy(x, y, z) + info.top, 32, info.h);
				if (rr) { bBytes += rr.w * rr.h; bSetups++; }
			}
			const costUnder = (uSetups + eSetups) * SETUP_T +
				(Math.ceil(uBytes / 2) * CYC.copy + Math.ceil(eBytes / 2) * CYC.blt1) * T_PER_DRAM;
			const costBlit = bSetups * SETUP_T + Math.ceil(bBytes / 2) * CYC.blt1 * T_PER_DRAM;
			if (costUnder < costBlit) {
				under.add(c);
				c.runs = runs; c.rects = rects; c.phaseRow = phaseRow; c.useRects = useRects;
				plan.compUnder++; plan.tilesUnder += c.tiles.length;
			} else { plan.compBlit++; plan.tilesBlit += c.tiles.length; }
		}
		// 0) кэш плоскости уровня: все полы, нарисованные порядком художника в отдельный буфер
		//    (строится один раз при входе в бой / смене этажа), и маска закрытой им области
		let plane = null;
		const cov = new Uint8Array(VIEW_W * VIEW_H);
		if (opt.cache) {
			plane = new Uint8Array(B.BUF_W * B.BUF_H);
			for (let x = 0; x < sc.map.sx; x++)
				for (let y = 0; y < sc.map.sy; y++) {
					const id = sc.cell(x, y, z)[0];
					if (!id) continue;
					const px = sc.sx(x, y, z), py = sc.sy(x, y, z);
					if (px <= -32 || px >= VIEW_W || py <= -40 || py >= VIEW_H) continue;
					const info = sc.part(id);
					blitPart(sc, plane, info, px, py - info.yoff, null, 0,
						null, info.lowEff && lowHidden(sc, x, y, z));
				}
			for (const c of comps) {
				if (!under.has(c)) continue;
				for (const [x, y] of c.tiles) {
					const bx = sc.sx(x, y, z), by = sc.sy(x, y, z) + FLOOR_TOP;
					for (let r = 1; r < 16; r++) {
						const dy = by + r;
						if (dy < 0 || dy >= VIEW_H) continue;
						for (let px = Math.max(0, bx + DIA[r][0]); px <= Math.min(VIEW_W - 1, bx + DIA[r][1]); px++)
							cov[dy * VIEW_W + px] = 1;
					}
				}
			}
		}
		// какие «остатки» придётся дорисовать: те, что вылезли за закрытую область, и все
		// более ближние с ними в пересечении (иначе блит дальнего затрёт ближний «остаток»)
		const needEx = new Set();
		if (opt.cache) {
			const list = [];
			for (const c of comps) if (under.has(c)) for (const t of c.tiles) list.push(t);
			list.sort((a, b) => (b[0] + b[1]) - (a[0] + a[1]));            // от ближних к дальним
			for (const [x, y] of list) {
				const info = sc.part(sc.cell(x, y, z)[0]);
				if (!info.ex) continue;
				const px = sc.sx(x, y, z), py = sc.sy(x, y, z);
				let need = false;
				for (let sy = 0; sy < FLOOR_TOP + 16 && !need; sy++)
					for (let sx2 = 0; sx2 < 32; sx2++) {
						if (!info.img[sy * 32 + sx2] || DIA_MASK[sy * 32 + sx2] || DIA_LOWCORNER[sy * 32 + sx2]) continue;
						const dx = px + sx2, dy = py + sy;
						if (dx < 0 || dx >= VIEW_W || dy < 0 || dy >= VIEW_H) continue;
						if (!cov[dy * VIEW_W + dx]) { need = true; break; }
					}
				// «остаток» вынесен вперёд вместе с плоскостью, поэтому его затрёт любая часть
				// клетки, которую порядок художника рисует раньше этой (стены, объекты рядом)
				if (!need) for (let dx = -3; dx <= 3 && !need; dx++)
					for (let dy = -3; dy <= 3; dy++) {
						const nx = x + dx, ny = y + dy;
						if (nx < 0 || ny < 0 || nx >= sc.map.sx || ny >= sc.map.sy) continue;
						if (nx > x || (nx === x && ny >= y)) continue;          // рисуется позже — не мешает
						const q = sc.cell(nx, ny, z);
						if (q[1] || q[2] || q[3]) { need = true; break; }
					}
				if (!need) for (let dx = 0; dx <= 3 && !need; dx++)
					for (let dy = 0; dy <= 3 - dx; dy++) {
						if (!dx && !dy) continue;
						if (needEx.has((y + dy) * sc.map.sx + (x + dx))) { need = true; break; }
					}
				if (need) needEx.add(y * sc.map.sx + x);
			}
		}
		// 1) подложка
		for (const c of comps) {
			if (!under.has(c)) continue;
			const ox0 = sc.sx(0, 0, z);
			let src;
			if (c.cache) {
				src = (x, R) => plane[R * B.BUF_W + x];
			} else {
				const strip = buildStrip(sc, c.id, 512);
				src = (x, R) => strip.strip[c.phaseRow(R) * strip.width + ((((x - ox0) % 32) + 32) % 32)];
			}
			const list = c.useRects ? c.rects : c.runs.map(r => ({ R: r.R, h: 1, x0: r.x0, x1: r.x1 }));
			for (const r of list) {
				const w = r.x1 - r.x0, bytes = w * r.h;
				// одноцветность: можно FILL вместо переноса
				let mono = true, first = -1;
				for (let dy = 0; dy < r.h && mono; dy++) {
					for (let x = r.x0; x < r.x1; x++) {
						const v = src(x, r.R + dy);
						if (first < 0) first = v;
						if (v !== first) { mono = false; break; }
					}
				}
				account(st, bytes, mono ? 'fill' : 'copy');
				if (mono) { st.fills++; st.fillBytes += bytes; plan.monoRuns++; plan.monoBytes += bytes; }
				st.bytesByClass[0] += bytes; st.byClass[0]++;
				if (c.useRects) plan.rects++; else plan.runs++;
				plan.planeBytes += bytes;
				plan.runLen.push(w); plan.rectH.push(r.h);
				for (let dy = 0; dy < r.h; dy++)
					for (let x = r.x0; x < r.x1; x++)
						sc.buf[(r.R + dy) * B.BUF_W + x] = src(x, r.R + dy);
			}
		}
		// 2) всё остальное — строго порядком художника, как в оригинале; у вынесенных в подложку
		//    полов рисуется только «остаток» выше ромба (отдельный обрезанный спрайт)
		const inUnder = new Set();
		for (const c of comps) if (under.has(c)) for (const [x, y] of c.tiles) inUnder.add(y * sc.map.sx + x);
		for (let x = 0; x < sc.map.sx; x++)
			for (let y = 0; y < sc.map.sy; y++) {
				const px = sc.sx(x, y, z), py = sc.sy(x, y, z);
				if (px <= -32 || px >= VIEW_W || py <= -40 || py >= VIEW_H) continue;
				const p = sc.cell(x, y, z);
				for (let k = 0; k < 4; k++) {
					if (!p[k]) continue;
					const info = sc.part(p[k]);
					if (k === 0 && inUnder.has(y * sc.map.sx + x)) {
						// «остаток» рисуем, только если он не попал целиком в закрытую подложкой область
						const need = info.ex && (!opt.cache || needEx.has(y * sc.map.sx + x));
						if (need) { blitPart(sc, sc.buf, info, px, py, st, 0, info.ex); plan.exBlits++; }
						continue;
					}
					blitPart(sc, sc.buf, info, px, py - info.yoff, st, k,
						null, k === 0 && info.lowEff && lowHidden(sc, x, y, z));
				}
			}
	}
	return plan;
}

// ---------------------------------------------------------------- потенциал для стен/объектов

// Годится ли часть в непрозрачную подложку вообще: однородное поле такой части даёт
// полностью непрозрачную ячейку 32x16 (тогда прогон можно лить RAM->RAM).
function wallpaperOpaque(sc, id) {
	const s = buildStrip(sc, id, 32);
	for (let i = 0; i < 16 * 32; i++) if (!s.strip[i]) return false;
	return true;
}

// ---------------------------------------------------------------- вывод PNG

let palCache = new Map();
function palette(game) {
	if (!palCache.has(game)) palCache.set(game, B.readPalette(game, 6));
	return palCache.get(game);
}
function savePng(file, game, buf, w, h) {
	const pal = palette(game), rgb = Buffer.alloc(w * h * 3);
	for (let y = 0; y < h; y++)
		for (let x = 0; x < w; x++) {
			const c = pal[buf[y * B.BUF_W + x]], o = (y * w + x) * 3;
			rgb[o] = c[0]; rgb[o + 1] = c[1]; rgb[o + 2] = c[2];
		}
	B.writePng(file, w, h, rgb);
}
function saveDiff(file, a, b, w, h) {
	const rgb = Buffer.alloc(w * h * 3);
	for (let y = 0; y < h; y++)
		for (let x = 0; x < w; x++) {
			const i = y * B.BUF_W + x, o = (y * w + x) * 3;
			const d = a[i] !== b[i];
			rgb[o] = d ? 255 : (a[i] ? 40 : 0); rgb[o + 1] = d ? 0 : (a[i] ? 40 : 0); rgb[o + 2] = d ? 0 : (a[i] ? 40 : 0);
		}
	B.writePng(file, w, h, rgb);
}

// ---------------------------------------------------------------- прогон одной сцены

function runScene(sc0, nblocks, outDir, png) {
	const sc = new Scene(sc0, nblocks);
	const stRef = newStat();
	const ref = renderRef(sc, stRef);

	// статистика по полам кадра
	const floors = new Map();
	let tilesVis = 0, floorTiles = 0;
	for (let z = 0; z <= sc.level; z++)
		for (let x = 0; x < sc.map.sx; x++)
			for (let y = 0; y < sc.map.sy; y++) {
				const px = sc.sx(x, y, z), py = sc.sy(x, y, z);
				if (px <= -32 || px >= VIEW_W || py <= -40 || py >= VIEW_H) continue;
				const p = sc.cell(x, y, z);
				if (p[0] || p[1] || p[2] || p[3]) tilesVis++;
				if (!p[0]) continue;
				floorTiles++;
				floors.set(p[0], (floors.get(p[0]) || 0) + 1);
			}
	let exactTiles = 0, fullTiles = 0, diaSum = 0, outSum = 0, lowSum = 0, shifted = 0;
	for (const [id, n] of floors) {
		const info = sc.part(id);
		if (info.diaExact) exactTiles += n;
		if (info.diaFull) fullTiles += n;
		if (info.yoff) shifted += n;
		diaSum += info.inDia * n; outSum += info.outDia * n; lowSum += info.lowCorner * n;
	}

	const stRuns = newStat(), stRects = newStat(), stOrder = newStat();
	const planOrder = renderUnder(sc, stOrder, { order: true });     // только перестановка порядка
	const bufOrder = sc.buf.slice();
	const planRuns = renderUnder(sc, stRuns, { rects: false });
	const bufRuns = sc.buf.slice();
	const planRects = renderUnder(sc, stRects, { rects: true });
	const bufRects = sc.buf.slice();
	const stCache = newStat();
	const planCache = renderUnder(sc, stCache, { cache: true });
	const bufCache = sc.buf.slice();

	let diff = 0, diffRuns = 0, diffOrder = 0, diffCache = 0;
	for (let y = 0; y < VIEW_H; y++)
		for (let x = 0; x < VIEW_W; x++) {
			const i = y * B.BUF_W + x;
			if (ref[i] !== bufRects[i]) diff++;
			if (ref[i] !== bufRuns[i]) diffRuns++;
			if (ref[i] !== bufOrder[i]) diffOrder++;
			if (ref[i] !== bufCache[i]) diffCache++;
		}

	// почему клетки не попали в подложку
	const why = { total: 0, notFull: 0, yoff: 0, low: 0, blocked: 0, ok: 0 };
	for (let z = 0; z <= sc.level; z++)
		for (let x = 0; x < sc.map.sx; x++)
			for (let y = 0; y < sc.map.sy; y++) {
				const id = sc.cell(x, y, z)[0];
				if (!id) continue;
				const px = sc.sx(x, y, z), py = sc.sy(x, y, z);
				if (px <= -32 || px >= VIEW_W || py <= -40 || py >= VIEW_H) continue;
				const info = sc.part(id);
				why.total++;
				if (!info.diaFull) why.notFull++;
				else if (info.yoff) why.yoff++;
				else if (info.lowEff && !lowHidden(sc, x, y, z)) why.low++;
				else if (blocksNear(sc, x - 1, y, z) || blocksNear(sc, x, y - 1, z)) why.blocked++;
				else why.ok++;
			}

	// «идеал»: все полы — подложкой (только площадь плоскости), стены и объекты как есть
	let idealFloor = 0;
	for (let z = 0; z <= sc.level; z++)
		for (let x = 0; x < sc.map.sx; x++)
			for (let y = 0; y < sc.map.sy; y++) {
				const id = sc.cell(x, y, z)[0];
				if (!id) continue;
				const bx = sc.sx(x, y, z), by = sc.sy(x, y, z) + FLOOR_TOP;
				for (let r = 1; r < 16; r++) {
					const R = by + r;
					if (R < 0 || R >= VIEW_H) continue;
					const a = Math.max(0, bx + DIA[r][0]), b = Math.min(VIEW_W, bx + DIA[r][1] + 1);
					if (b > a) idealFloor += b - a;
				}
			}
	const idealDram = Math.ceil(idealFloor / 2) * CYC.copy +
		Math.ceil((stRef.bytes - stRef.bytesByClass[0]) / 2) * CYC.blt1;

	// сплошные спрайты стен/объектов: их плотный габарит без дырок, значит блит можно делать
	// простым переносом RAM->RAM (2 цикла на слово вместо 3) без всяких прогонов
	const sol = { blits: 0, bytes: 0, allBlits: 0, allBytes: 0 };
	for (let z = 0; z <= sc.level; z++)
		for (let x = 0; x < sc.map.sx; x++)
			for (let y = 0; y < sc.map.sy; y++) {
				const px = sc.sx(x, y, z), py = sc.sy(x, y, z);
				if (px <= -32 || px >= VIEW_W || py <= -40 || py >= VIEW_H) continue;
				const p = sc.cell(x, y, z);
				for (let k = 1; k < 4; k++) {
					if (!p[k]) continue;
					const info = sc.part(p[k]);
					if (!info.box) continue;
					const rr = clipRect(px + info.box.x, py - info.yoff + info.box.y, info.box.w, info.box.h);
					if (!rr) continue;
					sol.allBlits++; sol.allBytes += rr.w * rr.h;
					if (info.solid) { sol.blits++; sol.bytes += rr.w * rr.h; }
				}
			}

	// потенциал для стен/объектов: сколько блитов и байт приходится на части,
	// однородное поле которых непрозрачно (значит, их тоже можно лить прогонами)
	const wp = { blits: 0, bytes: 0 };
	for (let z = 0; z <= sc.level; z++)
		for (let x = 0; x < sc.map.sx; x++)
			for (let y = 0; y < sc.map.sy; y++) {
				const px = sc.sx(x, y, z), py = sc.sy(x, y, z);
				if (px <= -32 || px >= VIEW_W || py <= -40 || py >= VIEW_H) continue;
				const p = sc.cell(x, y, z);
				for (let k = 1; k < 4; k++) {
					if (!p[k]) continue;
					const info = sc.part(p[k]);
					if (!info.img || !info.h || !wallpaperOpaque(sc, p[k])) continue;
					// сосед того же вида по 4 сторонам — тогда прогон длиннее одного тайла
					let same = false;
					for (const [dx, dy] of [[1, 0], [-1, 0], [0, 1], [0, -1]]) {
						const nx = x + dx, ny = y + dy;
						if (nx < 0 || ny < 0 || nx >= sc.map.sx || ny >= sc.map.sy) continue;
						if (sc.cell(nx, ny, z)[k] === p[k]) { same = true; break; }
					}
					if (!same) continue;
					const rr = clipRect(px, py - info.yoff + info.top, 32, info.h);
					if (rr) { wp.blits++; wp.bytes += rr.w * rr.h; }
				}
			}

	if (has('debug')) {
		let n = 0;
		for (let y = 0; y < VIEW_H && n < 12; y++)
			for (let x = 0; x < VIEW_W && n < 12; x++) {
				const i = y * B.BUF_W + x;
				if (ref[i] === bufCache[i]) continue;
				n++;
				console.log(`diff ${x},${y}: эталон ${ref[i]} кэш ${bufCache[i]}`);
			}
	}
	if (png) {
		savePng(path.join(outDir, sc0.id + '_cache.png'), sc0.game, bufCache, VIEW_W, VIEW_H);
		saveDiff(path.join(outDir, sc0.id + '_cachediff.png'), ref, bufCache, VIEW_W, VIEW_H);
		savePng(path.join(outDir, sc0.id + '_ref.png'), sc0.game, ref, VIEW_W, VIEW_H);
		savePng(path.join(outDir, sc0.id + '_under.png'), sc0.game, bufRects, VIEW_W, VIEW_H);
		saveDiff(path.join(outDir, sc0.id + '_diff.png'), ref, bufRects, VIEW_W, VIEW_H);
	}

	return {
		id: sc0.id, game: sc0.game, sc, tilesVis, floorTiles, uniqFloors: floors.size,
		exactShare: floorTiles ? exactTiles / floorTiles : 0,
		fullShare: floorTiles ? fullTiles / floorTiles : 0,
		shifted, avgDia: floorTiles ? diaSum / floorTiles : 0,
		avgOut: floorTiles ? outSum / floorTiles : 0, avgLow: floorTiles ? lowSum / floorTiles : 0,
		stRef, stRuns, stRects, stOrder, stCache, planRuns, planRects, planCache,
		diff, diffRuns, diffOrder, diffCache, wp, sol, why, idealFloor, idealDram,
	};
}

// ---------------------------------------------------------------- главное

const outDir = arg('out', 'tmp/proto/opt3');
const nblocks = parseInt(arg('blocks', '5'), 10);
const only = arg('scene', null);
const png = has('png') || !!only;
fs.mkdirSync(outDir, { recursive: true });

const list = only ? SCENES.filter(s => s.id === only) : SCENES;
const res = [];
for (const s of list) {
	try { res.push(runScene(s, nblocks, outDir, png)); }
	catch (e) { console.log(s.id + ': ошибка — ' + e.message); }
}

const f2 = (v) => v.toFixed(2);
const kb = (v) => (v / 1024).toFixed(0);
const frames = (st) => Math.max(st.dram / FRAME_DRAM, st.setups * SETUP_T / FRAME_T);

console.log('\n=== 1. Доля пола в кадре (окно 320x144, поле ' + nblocks + 'x' + nblocks + ' блоков)\n');
console.log('сцена      клеток  полов  уник  блитов  из них пол   КБ всего  КБ пол  доля пол   КБ пол подложкой  циклов пол: было/стало');
for (const r of res) {
	const b = r.stRef;
	console.log(
		r.id.padEnd(10) + String(r.tilesVis).padStart(6) + String(r.floorTiles).padStart(7) +
		String(r.uniqFloors).padStart(6) + String(b.blits).padStart(8) + String(b.byClass[0]).padStart(12) +
		kb(b.bytes).padStart(10) + kb(b.bytesByClass[0]).padStart(8) +
		(100 * b.bytesByClass[0] / b.bytes).toFixed(0).padStart(8) + '%' +
		kb(r.stCache.bytesByClass[0]).padStart(17) +
		(Math.ceil(b.bytesByClass[0] / 2) * CYC.blt1 / 1000).toFixed(0).padStart(15) + 'k/' +
		(r.planCache.planeBytes / 2 * CYC.copy / 1000 +
		 (r.stCache.bytesByClass[0] - r.planCache.planeBytes) / 2 * CYC.blt1 / 1000).toFixed(0) + 'k');
}

console.log('\n=== 2. Насколько полы «плиточны» (точный ромб 256 точек, без лишнего, без сдвига)\n');
console.log('сцена      точный ромб  ромб цел  сдвинут  ср. точек в ромбе  ср. лишних  из них в ближних углах');
for (const r of res)
	console.log(r.id.padEnd(10) + (100 * r.exactShare).toFixed(0).padStart(11) + '%' +
		(100 * r.fullShare).toFixed(0).padStart(9) + '%' + String(r.shifted).padStart(9) +
		r.avgDia.toFixed(0).padStart(19) + r.avgOut.toFixed(0).padStart(12) + r.avgLow.toFixed(1).padStart(23));

console.log('\n=== 3. Подложка: что попало в неё и какими кусками\n');
console.log('сцена      групп одного пола/ср.размер | сплошных областей/ср.размер | в подложке  прогонов  прямоуг.  ср.прогон  ср.выс.');
for (const r of res) {
	const p = r.planRuns, q = r.planRects;
	const avg = (a) => a.length ? a.reduce((x, y) => x + y, 0) / a.length : 0;
	const k = r.planCache;
	console.log(r.id.padEnd(10) + String(p.compAll || 0).padStart(11) + '/' +
		((p.tilesAll || 0) / (p.compAll || 1)).toFixed(1).padStart(4) + ' |' +
		String(k.compAll || 0).padStart(19) + '/' + ((k.tilesAll || 0) / (k.compAll || 1)).toFixed(1).padStart(5) + ' |' +
		String(k.tilesUnder).padStart(10) + String(p.runs).padStart(10) + String(k.rects).padStart(10) +
		avg(k.runLen).toFixed(0).padStart(11) + avg(k.rectH).toFixed(1).padStart(9));
}

console.log('\n=== 3a. Почему клетки не попали в подложку (видимых полов)\n');
console.log('сцена      всего  ромб неполон  сдвиг P_Level  сам лезет вниз  сосед лезет вниз  годных');
for (const r of res) {
	const w = r.why;
	console.log(r.id.padEnd(10) + String(w.total).padStart(6) + String(w.notFull).padStart(14) +
		String(w.yoff).padStart(15) + String(w.low).padStart(16) + String(w.blocked).padStart(18) +
		String(w.ok).padStart(8) + ' (' + (100 * w.ok / (w.total || 1)).toFixed(0) + '%)');
}

console.log('\n=== 4. Итог по кадру целиком: запусков DMA / КБ / кадров\n');
console.log('сцена      |  эталон           | прогоны           | прямоуг.          | кэш плоскости     | выигрыш | постройка кэша');
for (const r of res) {
	const a = r.stRef, b = r.stRuns, c = r.stRects, d = r.stCache;
	console.log(r.id.padEnd(10) + ' |' +
		String(a.setups).padStart(6) + kb(a.bytes).padStart(5) + f2(frames(a)).padStart(7) + ' |' +
		String(b.setups).padStart(6) + kb(b.bytes).padStart(5) + f2(frames(b)).padStart(7) + ' |' +
		String(c.setups).padStart(6) + kb(c.bytes).padStart(5) + f2(frames(c)).padStart(7) + ' |' +
		String(d.setups).padStart(6) + kb(d.bytes).padStart(5) + f2(frames(d)).padStart(7) + ' |' +
		((1 - frames(c) / frames(a)) * 100).toFixed(0).padStart(5) + '% / ' +
		((1 - frames(d) / frames(a)) * 100).toFixed(0).padStart(3) + '% |' +
		f2(Math.max(Math.ceil(a.bytesByClass[0] / 2) * CYC.blt1 / FRAME_DRAM, a.byClass[0] * SETUP_T / FRAME_T)).padStart(8));
}

console.log('\n=== 5. Заливка FILL по плоскости пола\n');
console.log('сцена      FILL-кусков  КБ FILL  % байт пола');
for (const r of res) {
	const c = r.stCache, p = r.planCache;
	console.log(r.id.padEnd(10) + String(c.fills).padStart(11) + kb(c.fillBytes).padStart(9) +
		(p.planeBytes ? (100 * p.monoBytes / p.planeBytes).toFixed(1) : '0').padStart(13));
}

console.log('\n=== 5a. Стены и объекты: что можно лить без прозрачности\n');
console.log('сцена      блитов  КБ(плотн.габарит) | сплошных: блитов  КБ  % байт | в однородных полях: блитов  КБ');
for (const r of res)
	console.log(r.id.padEnd(10) + String(r.sol.allBlits).padStart(6) + kb(r.sol.allBytes).padStart(18) + '  |' +
		String(r.sol.blits).padStart(17) + kb(r.sol.bytes).padStart(5) +
		(r.sol.allBytes ? (100 * r.sol.bytes / r.sol.allBytes).toFixed(0) : '0').padStart(7) + '% |' +
		String(r.wp.blits).padStart(26) + kb(r.wp.bytes).padStart(5));

console.log('\n=== 6. Проверка попиксельно (эталон против подложки), окно 320x144 = 46080 точек\n');
console.log('сцена      прогоны  прямоуг.  кэш плоскости');
for (const r of res)
	console.log(r.id.padEnd(10) + String(r.diffRuns).padStart(8) + String(r.diff).padStart(10) +
		String(r.diffCache).padStart(15));

console.log('\n=== 7. Чувствительность к цене запуска DMA (кадров на кадр вида)\n');
console.log('сцена      |  эталон: 300/200/120 T  |  прямоуг.: 300/200/120  |  кэш: 300/200/120');
const fr = (st, t) => Math.max(st.dram / FRAME_DRAM, st.setups * t / FRAME_T);
for (const r of res)
	console.log(r.id.padEnd(10) + ' |' + [300, 200, 120].map(t => f2(fr(r.stRef, t)).padStart(7)).join('') +
		'  |' + [300, 200, 120].map(t => f2(fr(r.stRects, t)).padStart(7)).join('') +
		'  |' + [300, 200, 120].map(t => f2(fr(r.stCache, t)).padStart(7)).join(''));

// сводка
const sum = (f) => res.reduce((a, r) => a + f(r), 0);
const frRef = sum(r => Math.max(r.stRef.dram / FRAME_DRAM, r.stRef.setups * SETUP_T / FRAME_T));
const frUnd = sum(r => Math.max(r.stRects.dram / FRAME_DRAM, r.stRects.setups * SETUP_T / FRAME_T));
console.log('\nВсего по ' + res.length + ' сценам: эталон ' + kb(sum(r => r.stRef.bytes)) + ' КБ / ' +
	sum(r => r.stRef.setups) + ' запусков / ' + frRef.toFixed(1) + ' кадров; подложка ' +
	kb(sum(r => r.stRects.bytes)) + ' КБ / ' + sum(r => r.stRects.setups) + ' запусков / ' +
	frUnd.toFixed(1) + ' кадров (' + (100 * (1 - frUnd / frRef)).toFixed(0) + ' %); расхождение ' +
	sum(r => r.diff) + ' точек из ' + (res.length * VIEW_W * VIEW_H));
