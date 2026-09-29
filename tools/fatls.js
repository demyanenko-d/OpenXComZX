// Список файлов в образе карты (FAT16/FAT32, только чтение) — проверка, что robimg
// сложил в образ то, что нужно: node tools\fatls.js tmp\oxz_card.img [путь]
const fs = require('fs');
const img = process.argv[2] || 'tmp/oxz_card.img';
const want = (process.argv[3] || '').toUpperCase();
const fd = fs.openSync(img, 'r');
const sec = (lba, n = 1) => { const b = Buffer.alloc(512 * n); fs.readSync(fd, b, 0, b.length, lba * 512); return b; };

let base = 0;
let b = sec(0);
if (b[0] !== 0xEB && b[0] !== 0xE9) { base = b.readUInt32LE(0x1C6); b = sec(base); }
const spc = b[13], rsvd = b.readUInt16LE(14), nfat = b[16], rootent = b.readUInt16LE(17);
let fatsz = b.readUInt16LE(22);
const fat32 = fatsz === 0;
if (fat32) fatsz = b.readUInt32LE(36);
const fatLba = base + rsvd;
const rootSecs = Math.ceil(rootent * 32 / 512);
const rootLba = fatLba + nfat * fatsz;
const dataLba = rootLba + rootSecs;
const rootClus = fat32 ? b.readUInt32LE(44) : 0;
console.log(`${fat32 ? 'FAT32' : 'FAT16'} spc=${spc} (${spc * 512 / 1024}K) fatsz=${fatsz} root=${rootent}`);

const clusLba = (c) => dataLba + (c - 2) * spc;
const next = (c) => {
	if (fat32) return sec(fatLba + (c >> 7)).readUInt32LE((c & 127) * 4) & 0x0FFFFFFF;
	const n = sec(fatLba + (c >> 8)).readUInt16LE((c & 255) * 2);
	return n >= 0xFFF8 ? 0x0FFFFFFF : n;
};

function list(clus, path) {
	const out = [];
	let lba, left;
	if (!clus && !fat32) { lba = rootLba; left = rootSecs; }
	else { if (!clus) clus = rootClus; lba = clusLba(clus); left = spc; }
	for (;;) {
		const s = sec(lba);
		for (let o = 0; o < 512; o += 32) {
			const c0 = s[o];
			if (!c0) return out;
			if (c0 === 0xE5 || s[o + 11] === 0x0F || (s[o + 11] & 8)) continue;
			const nm = s.toString('latin1', o, o + 8).trim();
			const ex = s.toString('latin1', o + 8, o + 11).trim();
			out.push({
				name: nm + (ex ? '.' + ex : ''), dir: !!(s[o + 11] & 0x10),
				clus: s.readUInt16LE(o + 26) | (s.readUInt16LE(o + 20) << 16),
				size: s.readUInt32LE(o + 28), path,
			});
		}
		lba++;
		if (--left) continue;
		if (!clus) return out;
		clus = next(clus);
		if (clus < 2 || clus >= 0x0FFFFFF8) return out;
		lba = clusLba(clus);
		left = spc;
	}
}

function walk(clus, path) {
	for (const e of list(clus, path)) {
		if (e.name === '.' || e.name === '..') continue;
		const p = path + e.name;
		if (want && !p.toUpperCase().startsWith(want)) continue;
		console.log(e.dir ? `  <DIR> ${p}` : `${String(e.size).padStart(9)} ${p}`);
		if (e.dir) walk(e.clus, p + '/');
	}
}
walk(0, '/');
