// Размер кода прошивки по разделам: что пришло из C, что написано на ассемблере,
// и как это делится на общий код, геоскейп и бой.
//
//   node tools/codesize.js
//
// Считается по объектным файлам сборки (tmp/build/*.rel): площадки кода — _CODE (Win0),
// _BANKn (банки), _GTAB, _HOME, _GSINIT, _INITIALIZER. Учитываются только модули, которые
// реально попали в сборку (список берётся из карты компоновщика: в tmp/build остаются и
// объектники давно удалённых файлов). Внутри площадок кода лежат и константы — сколько
// именно, оценивается по сгенерированному .asm (директивы .db/.dw/.ascii).
const fs = require("fs"), path = require("path");
process.chdir(path.join(__dirname, ".."));   // пути ниже — от корня проекта
const CODE = a => /^(_CODE|_BANK\d+|_HOME|_GSINIT|_GSFINAL|_GTAB|_INITIALIZER)$/.test(a);

const map = fs.readFileSync("tmp/build/oxz.map", "utf8");
const linked = new Set();
for (const m of map.matchAll(/^ +[0-9A-F]{8} +(\S+) +(\S+)$/gm)) linked.add(m[2]);
const areas = {};
for (const m of map.matchAll(/^(_\S+) +[0-9A-F]{8} +[0-9A-F]{8} = +(\d+)\. bytes/gm)) areas[m[1]] = +m[2];
let mapCode = 0;
for (const a in areas) if (CODE(a)) mapCode += areas[a];

const srcs = {};
(function walk(d) {
	for (const e of fs.readdirSync(d, { withFileTypes: true })) {
		const f = path.join(d, e.name);
		if (e.isDirectory()) walk(f);
		else if (/\.(c|s)$/.test(e.name)) srcs[e.name.replace(/\.(c|s)$/, "")] = f.split(path.sep).join("/");
	}
})("src");
srcs["banks"] = "tmp/build/banks.s";      // таблицы, которые генерирует build.ps1
srcs["packs"] = "tmp/build/packs.s";

// константы внутри площадок кода — по .asm тех модулей, что пришли из C
const RE_ASCII = new RegExp('^\\s*\\.ascii\\s+"((?:[^"\\\\]|\\\\.)*)"');
function dataBytes(line) {
	let m;
	if ((m = line.match(RE_ASCII))) {
		const s = m[1];
		let n = 0;
		for (let i = 0; i < s.length; i++) { if (s[i] === "\\") i++; n++; }
		return n;
	}
	if ((m = line.match(/^\s*\.(db|byte|fcb)\s+(.*)$/))) return m[2].split(",").length;
	if ((m = line.match(/^\s*\.(dw|word|fdb)\s+(.*)$/))) return 2 * m[2].split(",").length;
	if ((m = line.match(/^\s*\.blkb\s+(\d+)/))) return +m[1];
	return 0;
}
function constOf(mod) {
	const f = "tmp/build/" + mod + ".asm";
	if (!fs.existsSync(f)) return 0;
	let area = "", data = 0;
	for (const line of fs.readFileSync(f, "utf8").split(/\r?\n/)) {
		const a = line.match(/^\s*\.area\s+(\S+)/);
		if (a) { area = a[1]; continue; }
		if (CODE(area)) data += dataBytes(line);
	}
	return data;
}

const rows = [];
for (const mod of linked) {
	const f = "tmp/build/" + mod + ".rel";
	const src = srcs[mod] || null;
	if (!fs.existsSync(f)) { rows.push({ mod, src: null, code: 0 }); continue; }
	let code = 0;
	for (const m of fs.readFileSync(f, "utf8").matchAll(/^A (\S+) size ([0-9A-Fa-f]+)/gm))
		if (CODE(m[1])) code += parseInt(m[2], 16);
	const isC = src && src.endsWith(".c");
	rows.push({ mod, src, code, data: isC ? constOf(mod) : 0 });
}

function group(r) {
	if (!r.src) return null;
	const lang = r.src.endsWith(".c") ? "C" : "ассемблер";
	let part = "общий";
	if (r.src.startsWith("src/battlescape/")) part = "бой";
	else if (r.src.startsWith("src/geoscape/")) part = "геоскейп";
	return lang + "|" + part;
}
const acc = {};
let libCode = 0;
for (const r of rows) {
	const k = group(r);
	if (!k) { libCode += r.code; continue; }
	(acc[k] = acc[k] || { code: 0, data: 0, mods: [] });
	acc[k].code += r.code; acc[k].data += r.data || 0; acc[k].mods.push(r);
}
const order = ["C|общий", "C|геоскейп", "C|бой", "ассемблер|общий", "ассемблер|геоскейп", "ассемблер|бой"];
console.log("раздел                    всего     КБ   из них данные  модулей");
let tot = 0;
for (const k of order) {
	const a = acc[k];
	if (!a) { console.log(k.replace("|", " / ").padEnd(24), "     0    0.0              0       0"); continue; }
	tot += a.code;
	console.log(k.replace("|", " / ").padEnd(24), String(a.code).padStart(6), (a.code / 1024).toFixed(1).padStart(6),
		String(a.data).padStart(15), String(a.mods.filter(m => m.code).length).padStart(8));
}
console.log("библиотека SDCC".padEnd(24), String(mapCode - tot).padStart(6), ((mapCode - tot) / 1024).toFixed(1).padStart(6));
console.log("ИТОГО".padEnd(24), String(mapCode).padStart(6), (mapCode / 1024).toFixed(1).padStart(6));
console.log("\n--- по модулям (байт в площадках кода) ---");
for (const k of order) {
	const a = acc[k];
	if (!a) continue;
	console.log("\n== " + k.replace("|", " / "));
	a.mods.sort((x, y) => y.code - x.code);
	for (const m of a.mods) if (m.code) console.log(String(m.code).padStart(6), (m.data ? "(данные " + m.data + ")" : "").padEnd(16), m.src);
}

// данные в ассемблерных модулях — по самому исходнику .s (.db/.dw/.ascii вне _DATA)
console.log("\n--- данные внутри ассемблерного кода ---");
for (const k of ["ассемблер|общий", "ассемблер|геоскейп", "ассемблер|бой"]) {
	const a = acc[k];
	if (!a) continue;
	let sum = 0;
	const list = [];
	for (const m of a.mods) {
		if (!m.src || !fs.existsSync(m.src)) continue;
		let area = "_CODE", data = 0;
		for (const line of fs.readFileSync(m.src, "utf8").split(/\r?\n/)) {
			const ar = line.match(/^\s*\.area\s+(\S+)/);
			if (ar) { area = ar[1]; continue; }
			if (CODE(area)) data += dataBytes(line);
		}
		if (data) { sum += data; list.push([data, m.src]); }
	}
	list.sort((x, y) => y[0] - x[0]);
	console.log("\n== " + k.replace("|", " / ") + ": данные " + sum + " байт");
	for (const [d, s] of list.slice(0, 8)) console.log(String(d).padStart(6), " ", s);
}
