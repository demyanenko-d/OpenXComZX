// Варианты образа карты для тестов разбора тома (fat.c fat_mount, 23 §5):
//   node tools\cardvariants.js            — из oxz_card.img в tmp\sdtest\
// grub_mbr.img — как флешка из-под установщика Ubuntu с настоящего ZX Evolution (2026-10-24):
//   код MBR начинается с перехода «EB 63 90» (GRUB), а тип раздела — 07 (Windows при
//   форматировании в FAT32 тип не меняет). Старый fat_mount принимал такой MBR за том и
//   писал «не FAT»; теперь — как TS-BIOS: том ищется по BPB, тип раздела не важен.
// nofat.img — то же, но запись раздела указывает на сектор 1 (не том): ошибка монтирования,
//   окно диагностики показывает MBR и сырые байты секторов.
// Пересобираются, только если oxz_card.img новее.
'use strict';
const fs = require('fs');
const path = require('path');

const root = path.join(__dirname, '..');
const src = path.join(root, 'oxz_card.img');
const dir = path.join(root, 'tmp', 'sdtest');
fs.mkdirSync(dir, { recursive: true });

function variant(name, patch) {
	const dst = path.join(dir, name);
	if (fs.existsSync(dst) && fs.statSync(dst).mtimeMs >= fs.statSync(src).mtimeMs) return;
	fs.copyFileSync(src, dst);
	const fd = fs.openSync(dst, 'r+');
	const s0 = Buffer.alloc(512);
	fs.readSync(fd, s0, 0, 512, 0);
	if (s0.readUInt16LE(510) !== 0xAA55 || s0[0x1C2] === 0) throw new Error('oxz_card.img: no MBR partition');
	patch(s0);
	fs.writeSync(fd, s0, 0, 512, 0);
	fs.closeSync(fd);
	console.log(`cardvariants: ${dst}`);
}

const grub = (s0) => { s0[0] = 0xEB; s0[1] = 0x63; s0[2] = 0x90; s0[0x1C2] = 0x07; };
variant('grub_mbr.img', grub);
variant('nofat.img', (s0) => { grub(s0); s0.writeUInt32LE(1, 0x1C6); });
