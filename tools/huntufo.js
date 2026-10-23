// «Охота за НЛО»: эмулятор без окна + канал управления, кампания идёт сама, пока не случится
// N воздушных боёв по живому пути (настоящее обнаружение -> Intercept -> перехватчик -> OK ->
// погоня -> бой в атакующем режиме). Проверка на зависания при перехвате (findings_log
// 2026-10-24): любая ловушка эмулятора (PC LOST, KERNEL CODE WRITE, IX SLOT, STACK OVERFLOW…)
// — снимок состояния и стоп.
//   node tools\huntufo.js [боёв=3] [предел кадров=400000] [кампаний=6]
// Севшие НЛО не перехватываются (состояние — по строкам «ufo:» журнала игры), после проигрыша
// кампании начинается новая (без сценария зерно новой игры от кадра — НЛО другие).
// Журнал — tmp\hunt\log.txt, снимки экрана — tmp\shots\hunt\, состояния — tmp\hunt\*.bin.
// Код выхода: 0 — набрали бои, 1 — авария, 2 — вышел предел кадров, 3 — эмулятор закрылся.
'use strict';
const fs = require('fs');
const path = require('path');
const { spawn } = require('child_process');

const root = path.join(__dirname, '..');
const dir = path.join(root, 'tmp', 'hunt');
fs.mkdirSync(dir, { recursive: true });
fs.mkdirSync(path.join(root, 'tmp', 'shots', 'hunt'), { recursive: true });
const CMD = path.join(dir, 'cmd.txt');
const LOG = path.join(dir, 'log.txt');
fs.writeFileSync(CMD, '');
fs.writeFileSync(LOG, '');
const want = +(process.argv[2] || 3);
const maxFrames = +(process.argv[3] || 400000);

const TRAP = /PC LOST|KERNEL CODE WRITE|IX SLOT|STACK OVERFLOW|ISR STACK|DMA INTO KERNEL|PANIC|CACHE STALE/;

// Экраны (src/common/screens.h)
const S = { MAIN: 1, NEWGAME: 2, GEO: 32, UFO_DET: 36, UFO_LOST: 37, BASE_NAME: 42, BUILD_BASE: 43,
	INTERCEPT: 200, CONFIRM: 203, LOW_FUEL: 205, DOGFIGHT: 216, FAILED: 51 };
const maxCampaigns = +(process.argv[4] || 6);
let campaigns = 0;
// Состояние НЛО по журналу игры: «ufo: reached slot/status*10+pt S V» (V/10 — US_*: 0 летит,
// 1 село, 2 сбито, 3 уничтожено), «ufo: detected slot/hyper S H» — какое обнаружено
const ufoStatus = {};
let detSlot = -1;

let frames = 0;          // по строкам «alive» (раз в 100 кадров)
let top = 0;             // верхний экран по последней «ui: screen N»
let lastScreenAt = 0;    // кадр последней смены/отметки экрана
let acted = -1;          // кадр последнего действия (ждём отклика)
let started = false;
let pursuing = false;    // перехватчик в воздухе — время медленнее
let pursuitAt = 0;
let nextRow = 1;         // чередовать BARRACUDA-1 / -2 (строки 1 и 2 списка)
let fights = 0, inFight = false, launches = 0, detected = 0;

let done = false;
let dismissTries = 0;

const log = (s) => { fs.appendFileSync(LOG, s + '\n'); };
const say = (s) => { const t = `[hunt f${frames}] ${s}`; console.log(t); log(t); };
function send(lines) {
	fs.appendFileSync(CMD, lines.join('\n') + '\n');
	acted = frames;
}
const click = (x, y) => [`pokew _cursor_x ${x}`, `pokew _cursor_y ${y}`, 'click L'];
const shot = (name) => `shot tmp/shots/hunt/${name}.png`;


function finish(code, why) {
	if (done) return;
	done = true;
	say(`STOP: ${why}; бои ${fights}, вылеты ${launches}, обнаружений ${detected}, кадров ~${frames}`);
	send([shot('zz_end'), 'wait 5', `exit ${code}`]);
	setTimeout(() => { try { emu.kill(); } catch (e) { /* уже вышел */ } process.exit(code); }, 8000);
}

// Реакция — только на смену окна: каждая клавиша и щелчок тоже печатают «ui: screen N», и на
// повторы того же окна (буквы имени базы!) действие повторялось бы. Повтор при тишине —
// из onAlive с force.
function onScreen(n, force) {
	const prev = top;
	top = n;
	lastScreenAt = frames;
	if (done) return;
	if (!force && n === prev && n !== S.GEO) return;
	switch (n) {
	case S.MAIN:
		// после проигрыша — новая кампания: без сценария зерно новой игры от кадра, НЛО другие
		if (++campaigns > maxCampaigns) return finish(2, `кампаний сыграно: ${maxCampaigns}`);
		pursuing = false; inFight = false; nextRow = 1; detSlot = -1;
		for (const k of Object.keys(ufoStatus)) delete ufoStatus[k];
		say(`новая игра (кампания ${campaigns})`);
		send([...(started ? [] : ['ixguard _globe_xy']), ...click(110, 100)]);
		started = true;
		return;
	case S.NEWGAME: send(click(119, 172)); return;
	case S.BUILD_BASE: send(click(120, 110)); return;
	case S.BASE_NAME: send(['type hunt', 'key ENTER']); return;
	case S.GEO:
		dismissTries = 0;
		if (inFight) {
			inFight = false;
			fights++;
			pursuing = false;
			say(`бой ${fights} окончен`);
			send([shot(`f${fights}_after`)]);
			if (fights >= want) return finish(0, `набрано боёв: ${fights}`);
		}
		// скорость — только при возврате с другого окна: нажатие на геоскейпе само печатает
		// «ui: screen 32», и иначе клавиша жалась бы без конца; окна событий сбрасывают скорость
		if (prev !== S.GEO) send([`key ${pursuing ? 2 : 5}`]);
		return;
	case S.UFO_DET:
		detected++;
		send([shot(`d${detected}_ufo`)]);
		if (ufoStatus[detSlot] === 1) { say(`НЛО ${detSlot} обнаружено, но село — отмена`); send(click(128, 156)); return; }
		if (pursuing && frames - pursuitAt < 15000) { say('НЛО обнаружено, перехватчик занят — отмена'); send(click(128, 156)); return; }
		say(`НЛО обнаружено (${detected}) — перехват`);
		send([`save tmp/hunt/before_${launches + 1}.bin`, ...click(128, 124)]);
		return;
	case S.INTERCEPT:
		say(`список кораблей — строка ${nextRow}`);
		send(click(60, 82 + nextRow * 8));
		nextRow = nextRow === 1 ? 2 : 1;
		return;
	case S.CONFIRM:
		launches++;
		pursuing = true;
		pursuitAt = frames;
		say(`вылет ${launches}`);
		send(['key ENTER']);
		return;
	case S.DOGFIGHT:
		if (!inFight) {
			inFight = true;
			say(`воздушный бой ${fights + 1} — атака`);
			send(['wait 3', shot(`f${fights + 1}_start`), ...click(218, 79)]);
		}
		return;
	case S.FAILED:
		say(`кампания ${campaigns} проиграна — заново`);
		send(['key ENTER']);
		return;
	case S.LOW_FUEL:                         // перехватчик идёт домой — свободен для следующей цели
		pursuing = false;
		send(['key ENTER']);
		return;
	default:
		// прочие всплывающие окна: ESC, потом ENTER
		dismissTries++;
		if (dismissTries > 6) { say(`окно ${n} не закрывается`); send([shot(`stuck_${n}`)]); return finish(2, `застряли в окне ${n}`); }
		send([dismissTries % 2 ? 'key SPACE' : 'key ENTER']);
	}
}

// Окно боя может остаться свёрнутым (значок на глобусе) — если долго тихо на 216/32, жмём атаку снова
function onAlive() {
	frames += 100;
	if (done) return;
	if (frames > maxFrames) return finish(2, 'предел кадров');
	if (pursuing && frames - pursuitAt > 15000) { say('погоня затянулась — время быстрее'); pursuing = false; if (top === S.GEO) send(['key 5']); }
	// нет отклика на действие — повторить реакцию на текущий экран
	if (acted >= 0 && frames - acted > 400 && frames - lastScreenAt > 400 && top !== S.GEO && top !== S.DOGFIGHT) {
		say(`нет отклика на экране ${top} — повтор`);
		onScreen(top, true);
	}
}

say(`охота: нужно боёв ${want}, предел ${maxFrames} кадров`);
const emu = spawn('powershell', ['-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', path.join(root, 'tools', 'run.ps1'),
	'-Cmd', 'tmp\\hunt\\cmd.txt', '-Headless'], { cwd: root });
let buf = '';
function onData(d) {
	buf += d.toString('latin1');
	let i;
	while ((i = buf.indexOf('\n')) >= 0) {
		const line = buf.slice(0, i).replace(/\r$/, '');
		buf = buf.slice(i + 1);
		log(line);
		if (TRAP.test(line)) { say(`ЛОВУШКА: ${line}`); send(['save tmp/hunt/crash.bin', shot('crash')]); finish(1, 'авария'); continue; }
		let m;
		if ((m = /^ufo: reached slot\/status\*10\+pt (\d+) (\d+)/.exec(line))) ufoStatus[+m[1]] = Math.floor(+m[2] / 10);
		else if ((m = /^ufo: detected slot\/hyper (\d+)/.exec(line))) detSlot = +m[1];
		if ((m = /^ui: screen (\d+),/.exec(line))) onScreen(+m[1]);
		else if (/^OXZ: alive/.test(line)) onAlive();
		else if (/^df: /.test(line) || /^craft: /.test(line)) say(line);
	}
}
emu.stdout.on('data', onData);
emu.stderr.on('data', onData);
emu.on('exit', (code) => { if (!done) { say(`эмулятор закрылся (${code})`); process.exit(3); } else process.exit(code === 0 ? 0 : code); });
