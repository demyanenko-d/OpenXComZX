// Прототип отрисовки наземного боя (16_battlescape_plan.md §8): читает данные оригинала из
// Steam/, собирает изометрический вид и считает, сколько работы досталось бы процессору.
// Это модель, а не порт: важны числа (клеток, блитов, байт DMA), а картинка — проверка геометрии.
//
//   node tools/proto/battle.js [--game TFTD|UFO] [--map U_BASE00] [--terrain U_BASE]
//                              [--level 3] [--cam x,y] [--out tmp/proto]
//
// Экран боя: окно карты 320x144 (нижние 56 строк — панель), буфер прокрутки 512x456 (в бою вся
// видеопамять наша). Тайл — 32x40, ромб пола 32x16; экранные координаты (Camera::convertMapToScreen):
//   X = (x - y) * 16, Y = (x + y) * 8 - z * 24.
'use strict';
const fs = require('fs');
const path = require('path');
const zlib = require('zlib');

const GAMES = {
	TFTD: 'Steam/X-COM Terror from the Deep/TFD',
	UFO: 'Steam/X-COM UFO Defense/XCOM',
};

const TILE_W = 32, TILE_H = 40, HALF_W = 16, QUART = 8, LEVEL_H = 24;
const VIEW_W = 320, VIEW_H = 144;          // окно карты (панель — отдельные 56 строк)
const BUF_W = 512, BUF_H = 456;            // буфер прокрутки

// ---------------------------------------------------------------- разбор файлов

// PCK + TAB: кадры w x h, 254 — пропуск n точек, 255 — конец кадра (Formats.cs DecodePck)
function readPck(pck, tab, w = TILE_W, h = TILE_H) {
	const wide = tab.length >= 4 && tab.readUInt16LE(2) === 0 && tab.length % 4 === 0;
	const n = wide ? tab.length / 4 : tab.length / 2;
	const frames = [];
	let p = 0;
	for (let f = 0; f < n; f++) {
		const img = new Uint8Array(w * h);
		let o = 0;
		if (p >= pck.length) { frames.push(img); continue; }
		o = pck[p++] * w;
		while (p < pck.length) {
			const v = pck[p++];
			if (v === 255) break;
			if (v === 254) o += pck[p++];
			else if (o < img.length) img[o++] = v;
		}
		frames.push(img);
	}
	return frames;
}

// MCD: 62 байта на запись (MapDataSet.cpp:107)
function readMcd(buf) {
	const recs = [];
	for (let p = 0; p + 62 <= buf.length; p += 62) {
		recs.push({
			frame: Array.from(buf.subarray(p, p + 8)),
			scang: buf.readUInt16LE(p + 20),
			ufoDoor: buf[p + 30], stopLOS: buf[p + 31], noFloor: buf[p + 32],
			bigWall: buf[p + 33], gravLift: buf[p + 34], door: buf[p + 35],
			blockFire: buf[p + 36], blockSmoke: buf[p + 37],
			tuWalk: buf[p + 39], tuSlide: buf[p + 40], tuFly: buf[p + 41],
			armor: buf[p + 42], heBlock: buf[p + 43], dieMcd: buf[p + 44],
			tLevel: buf.readInt8(p + 48), pLevel: buf[p + 49],
			lightBlock: buf[p + 51], tileType: buf[p + 53],
			lightSource: buf[p + 58],
		});
	}
	return recs;
}

// MAP: 3 байта (sizeY, sizeX, sizeZ), затем по 4 байта на клетку; порядок — z сверху вниз,
// внутри уровня y, внутри ряда x (BattlescapeGenerator.cpp:1577, :1671)
function readMap(buf) {
	const sy = buf[0], sx = buf[1], sz = buf[2];
	const cells = new Uint8Array(sx * sy * sz * 4);
	let p = 3;
	for (let z = sz - 1; z >= 0; z--)
		for (let y = 0; y < sy; y++)
			for (let x = 0; x < sx; x++) {
				const o = ((z * sy + y) * sx + x) * 4;
				for (let k = 0; k < 4; k++) cells[o + k] = buf[p++];
			}
	return { sx, sy, sz, cells };
}

// Палитра из нашего PAL.PAK (конвертер уже собрал боевые палитры TFTD из D0..D3.LBM):
// ресурс — 256 слов RGB555 с битом 15 (VDAC, 02 §4)
function readPalette(game, id) {
	const buf = fs.readFileSync(path.join('tmp/sd/OXZ', game, 'PAL.PAK'));
	const n = buf.readUInt16LE(6);
	for (let i = 0; i < n; i++) {
		const e = 16 + i * 16;
		if (buf.readUInt16LE(e) !== id) continue;
		const off = buf.readUInt16LE(e + 4) * 512;
		const pal = [];
		for (let k = 0; k < 256; k++) {
			const v = buf.readUInt16LE(off + k * 2);
			pal.push([((v >> 10) & 31) << 3, ((v >> 5) & 31) << 3, (v & 31) << 3]);
		}
		pal[0] = [0, 0, 0];
		return pal;
	}
	throw new Error('palette ' + id + ' not found in PAL.PAK');
}

// ---------------------------------------------------------------- вывод PNG

function writePng(file, w, h, rgb) {
	const raw = Buffer.alloc((w * 3 + 1) * h);
	for (let y = 0; y < h; y++) {
		raw[y * (w * 3 + 1)] = 0;
		rgb.copy(raw, y * (w * 3 + 1) + 1, y * w * 3, (y + 1) * w * 3);
	}
	const chunks = [];
	const chunk = (type, data) => {
		const len = Buffer.alloc(4);
		len.writeUInt32BE(data.length);
		const body = Buffer.concat([Buffer.from(type, 'ascii'), data]);
		const crc = Buffer.alloc(4);
		crc.writeUInt32BE(crc32(body) >>> 0);
		chunks.push(len, body, crc);
	};
	const ihdr = Buffer.alloc(13);
	ihdr.writeUInt32BE(w, 0); ihdr.writeUInt32BE(h, 4);
	ihdr[8] = 8; ihdr[9] = 2; ihdr[10] = 0; ihdr[11] = 0; ihdr[12] = 0;
	chunks.push(Buffer.from([0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A]));
	chunk('IHDR', ihdr);
	chunk('IDAT', zlib.deflateSync(raw));
	chunk('IEND', Buffer.alloc(0));
	fs.mkdirSync(path.dirname(file), { recursive: true });
	fs.writeFileSync(file, Buffer.concat(chunks));
}

let crcTable = null;
function crc32(buf) {
	if (!crcTable) {
		crcTable = new Int32Array(256);
		for (let n = 0; n < 256; n++) {
			let c = n;
			for (let k = 0; k < 8; k++) c = c & 1 ? 0xEDB88320 ^ (c >>> 1) : c >>> 1;
			crcTable[n] = c;
		}
	}
	let c = -1;
	for (let i = 0; i < buf.length; i++) c = crcTable[(c ^ buf[i]) & 0xFF] ^ (c >>> 8);
	return c ^ -1;
}

module.exports = { readPck, readMcd, readMap, readPalette, writePng, GAMES, TILE_W, TILE_H, HALF_W, QUART, LEVEL_H, VIEW_W, VIEW_H, BUF_W, BUF_H };
