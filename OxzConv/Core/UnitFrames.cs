// Готовые кадры юнитов по броне: порт сборки UnitSprite::draw OpenXcom
// (REF/OpenXcom/src/Battlescape/UnitSprite.cpp, процедуры drawRoutine0..22). Оригинал
// собирает бойца из частей листа UNITS/*.PCK (ноги, торс, руки) на каждом кадре; движку на
// Z80 это не по силам, поэтому кадры собираются здесь, а лист брони становится набором
// готовых картинок 32x40.
//
// Что учтено (поля BattleUnit, которые читают процедуры): направление, фаза ходьбы
// (getWalkingPhase = _walkPhase % 8), статус STATUS_STANDING / STATUS_WALKING, kneeled,
// movementType (ховертанк, дисплейсер), forcedTorso, _helmet (глубина боя у TFTD), часть
// большого юнита (_part) и общий такт анимации карты _animationFrame (Map::animate, 0..7).
// Чего нет: предметы в руках (HANDOB, руки «с оружием» — состояние «без предмета»), башни
// танков (тип башни задаёт оружие юнита, не броня: turretType предмета; кадры 64+turret*8 у
// drawRoutine2 и 256+turret*8 у drawRoutine11), женский торс по полу бойца (только
// forcedTorso брони), смерть, падение, прицеливание, парение стоя (legsFloat), перекраска
// волос (battleHairBleach).
//
// Холст как у UnitSprite: 64x40 (кадр «двойной ширины», Map::cacheUnit), части кладутся с
// X = 16 (offXSprite) и своим Y; непрозрачный пиксель (индекс != 0) — поверх. Результат —
// окно x 16..47 холста, то есть клетка 32x40. Что выходит за окно, теряется (UnitFrames.Lost).
//
// Раскладка кадров (ресурс ASHEET_*, BattleData.cs):
//   size 1 (80 кадров): 0..7 — стоя по направлению d (0 — север, по часовой);
//     8 + d*8 + фаза — ходьба (фазы 0..7); 72 + d — на колене (нет колена — как стоя).
//   size 2 (288 кадров, без ходьбы — 32): d*4 + part — стоя; 32 + (d*8 + фаза)*4 + part —
//     ходьба. part = ox + oy*2, где ox, oy — смещение клетки от клетки юнита (Map::drawUnit):
//     0 — сама клетка юнита (верх ромба), 1 — x+1 (правее и ниже на 16x8), 2 — y+1 (левее и
//     ниже на 16x8), 3 — x+1,y+1 (ниже на 16).
//   У процедур с постоянной анимацией (силакоид, целатид, галлюциноид, ксаркид, пропеллер
//   кибердиска и ховертанка, дисплейсер) такт карты _animationFrame берётся равным фазе:
//   кадры «ходьбы» — это и есть цикл анимации по направлению, кадры «стоя» — такт 0.
using System;
using System.Collections.Generic;

namespace OxzConv
{
	static class UnitFrames
	{
		public const int W = 32, H = 40, CanvasW = 64, OffX = 16;

		public class Armor
		{
			public string Type, Sheet;          // Sheet — имя листа без .PCK
			public int Routine, Size = 1, Movement, ForcedTorso;
		}

		// Процедуры семейства drawRoutine0 (солдаты, сектоиды, мутоны, акванавты, наземные TFTD)
		static bool HumanoidRoutine(int r) => r == 0 || r == 10 || r == 13 || r == 14 || r == 15;

		// MT_FLY (1) — у TFTD и MT_FLOAT/MT_SINK под водой (BattleUnit::BattleUnit: depth > 0)
		static bool Flying(Armor a, bool tftd) => a.Movement == 1 || (tftd && (a.Movement == 3 || a.Movement == 4));

		// Имя ресурса: лист + процедура и те поля брони, от которых зависит картинка:
		// _T2 — forcedTorso: 2 (TORSO_ALWAYS_FEMALE: летающий костюм, магнитно-ионная броня),
		// _F — летающий танк (ховертанк, дисплейсер). Одинаковые ключи не дублируются.
		public static string ResName(Armor a, bool tftd)
		{
			string n = $"ASHEET_{a.Sheet}_{a.Routine}";
			if (HumanoidRoutine(a.Routine) && (a.Routine == 0 || tftd) && a.ForcedTorso == 2) n += "_T2";
			if ((a.Routine == 2 || a.Routine == 11) && Flying(a, tftd)) n += "_F";
			return n;
		}

		// Счётчики потерь: Lost — непрозрачные точки, которые OpenXcom рисует на холсте 64x40,
		// а в окно 32x40 они не попали (наш срез); Clipped — точки за верхом/низом холста (их нет
		// и в OpenXcom). Missing — обращения к кадрам, которых нет в листе.
		public static long Lost, Clipped, Missing;

		class Canvas
		{
			public readonly byte[] Px = new byte[CanvasW * H];
			readonly List<Img> sheet;
			public Canvas(List<Img> sheet) { this.sheet = sheet; }

			// Surface::blit части листа с позицией (x, y) на холст
			public void Put(int frame, int x, int y)
			{
				if (frame < 0 || frame >= sheet.Count) { Missing++; return; }
				var f = sheet[frame];
				for (int sy = 0; sy < f.H; sy++)
					for (int sx = 0; sx < f.W; sx++)
					{
						byte v = f.Px[sy * f.W + sx];
						if (v == 0) continue;
						int dx = x + sx, dy = y + sy;
						if (dx < 0 || dx >= CanvasW || dy < 0 || dy >= H) { Clipped++; continue; }
						Px[dy * CanvasW + dx] = v;
					}
			}

			public Img Window()
			{
				var img = new Img(W, H);
				for (int y = 0; y < H; y++)
					for (int x = 0; x < CanvasW; x++)
					{
						byte v = Px[y * CanvasW + x];
						if (x >= OffX && x < OffX + W) img.Px[y * W + x - OffX] = v;
						else if (v != 0) Lost++;
					}
				return img;
			}
		}

		// Состояние юнита для одного кадра
		struct State
		{
			public int Dir, Phase, Part, Anim;
			public bool Walking, Kneel;
		}

		// Кадры брони по раскладке выше. walk = false — только стоя (size 2 без ходьбы).
		public static List<Img> Build(List<Img> sheet, Armor a, bool tftd, bool walk = true)
		{
			var o = new List<Img>();
			if (a.Size <= 1)
			{
				for (int d = 0; d < 8; d++) o.Add(Draw(sheet, a, tftd, new State { Dir = d }));
				for (int d = 0; d < 8; d++)
					for (int p = 0; p < 8; p++) o.Add(Draw(sheet, a, tftd, new State { Dir = d, Phase = p, Anim = p, Walking = true }));
				for (int d = 0; d < 8; d++) o.Add(Draw(sheet, a, tftd, new State { Dir = d, Kneel = HumanoidRoutine(a.Routine) }));
				return o;
			}
			for (int d = 0; d < 8; d++)
				for (int part = 0; part < 4; part++) o.Add(Draw(sheet, a, tftd, new State { Dir = d, Part = part }));
			if (!walk) return o;
			for (int d = 0; d < 8; d++)
				for (int p = 0; p < 8; p++)
					for (int part = 0; part < 4; part++)
						o.Add(Draw(sheet, a, tftd, new State { Dir = d, Phase = p, Anim = p, Part = part, Walking = true }));
			return o;
		}

		// UnitSprite::draw: выбор процедуры по drawingRoutine (таблица routines[])
		static Img Draw(List<Img> sheet, Armor a, bool tftd, State s)
		{
			var c = new Canvas(sheet);
			switch (a.Routine)
			{
			case 0: case 10: case 13: case 14: case 15: Routine0(c, a, tftd, s); break;
			case 1: Routine1(c, s); break;
			case 2: Routine2(c, a, tftd, s); break;
			case 3: case 22: Routine3(c, a.Routine, s); break;
			case 4: case 17: case 18: Routine4(c, a.Routine, s); break;
			case 5: Routine5(c, s); break;
			case 6: Routine6(c, s); break;
			case 7: Routine7(c, s); break;
			case 8: c.Put(new[] { 0, 1, 2, 3, 4, 3, 2, 1 }[s.Anim], OffX, 0); break;     // drawRoutine8: Pulsate
			case 9: c.Put(s.Anim, OffX, 0); break;                                        // drawRoutine9: Body + _animationFrame
			case 11: Routine11(c, a, tftd, s); break;
			case 12: case 16: c.Put(s.Part * 8 + s.Anim, OffX, 0); break;               // drawRoutine12
			case 19: c.Put((s.Walking ? 8 : 0) + s.Dir, OffX, 0); break;                  // drawRoutine19: stand 0, move 8
			case 20:                                                                        // drawRoutine20
				c.Put((s.Walking ? s.Phase / 2 % 4 : 0) + 5 * (s.Part + 4 * s.Dir), OffX, 0); break;
			case 21: c.Put(s.Part * 4 + s.Dir * 16 + s.Anim % 4, OffX, 0); break;        // drawRoutine21
			default: Missing++; break;
			}
			return c.Window();
		}

		// Часть с позицией: кадр листа и сдвиг Y (X у всех частей — offXSprite, кроме оговорённых)
		struct Piece { public int F, X, Y; public Piece(int f, int y = 0, int x = OffX) { F = f; Y = y; X = x; } }

		// drawRoutine0 (+10 мутоны, 13 акванавты, 14 наземные TFTD, 15 акватоиды), UnitSprite.cpp:209
		static void Routine0(Canvas c, Armor a, bool tftd, State s)
		{
			int r = a.Routine, d = s.Dir, ph = s.Phase;
			bool helmet = tftd;                       // _save->getDepth() != 0: берём подводный бой
			int maleTorso, femaleTorso;
			if (r <= 10) { maleTorso = 32; femaleTorso = 267; }
			else if (r == 13)
			{
				if (helmet) { maleTorso = 32; femaleTorso = a.ForcedTorso == 0 ? 32 : 286; }
				else { maleTorso = 270; femaleTorso = 262; }
			}
			else { maleTorso = 32; femaleTorso = 262; }
			int[] yoffWalk = { 1, 0, -1, 0, 1, 0, -1, 0 };
			int[] mutonYoffWalk = { 1, 1, 0, 0, 1, 1, 0, 0 };
			int[] aquatoidYoffWalk = { 1, 0, 0, 1, 2, 1, 0, 0 };

			// пол бойца — мужской; женский торс только по forcedTorso брони (UnitSprite.cpp:310)
			bool female = (r == 0 || helmet) && a.ForcedTorso == 2;
			var torso = new Piece((female ? femaleTorso : maleTorso) + d);
			Piece legs, larm, rarm;
			if (s.Walking)
			{
				if (r == 10) torso.Y = mutonYoffWalk[ph];
				else if (r == 13 || r == 14) torso.Y = yoffWalk[ph] + 1;
				else if (r == 15) torso.Y = aquatoidYoffWalk[ph];
				else torso.Y = yoffWalk[ph];
				legs = new Piece(56 + 24 * d + ph);
				larm = new Piece(40 + 24 * d + ph, r == 10 && d == 3 ? -1 : 0);
				rarm = new Piece(48 + 24 * d + ph);
				if (r == 10 && d == 2) rarm.X = 10;  // «fix the errant muton arm» (без предмета)
			}
			else
			{
				legs = new Piece((s.Kneel ? 24 : 16) + d);
				larm = new Piece(0 + d);
				rarm = new Piece(8 + d);
				if (s.Kneel)
				{
					int k = r == 13 ? 5 : 4;              // offYKneel (+1 — «tftd torsos are stubby»)
					larm.Y = rarm.Y = torso.Y = k;
				}
			}
			// порядок вывода по направлению — ветки без оружия (UnitSprite.cpp:544)
			Piece[] order;
			switch (d)
			{
			case 0: case 1: case 2: order = new[] { larm, legs, torso, rarm }; break;
			case 3: order = new[] { legs, torso, larm, rarm }; break;
			case 4: order = new[] { legs, rarm, torso, larm }; break;
			case 5: case 6: order = new[] { rarm, legs, torso, larm }; break;
			default: order = new[] { larm, rarm, legs, torso }; break;
			}
			foreach (var p in order) c.Put(p.F, p.X, p.Y);
		}

		// drawRoutine1 — флоатеры (руки в листе переставлены: левая 8, правая 0), UnitSprite.cpp:597
		static void Routine1(Canvas c, State s)
		{
			int d = s.Dir;
			var larm = new Piece(8 + d);
			var rarm = new Piece(0 + d);
			// у флоатера 5 кадров полёта на 8 фаз: walk + 5*dir + walkPhase/1.6 (усечение double)
			var torso = new Piece(s.Walking ? (int)(24 + 5 * d + s.Phase / 1.6) : 16 + d);
			Piece[] order;
			switch (d)
			{
			case 0: case 1: case 2: order = new[] { larm, torso, rarm }; break;
			case 3: case 4: order = new[] { torso, larm, rarm }; break;
			case 5: case 6: order = new[] { rarm, torso, larm }; break;
			default: order = new[] { rarm, larm, torso }; break;
			}
			foreach (var p in order) c.Put(p.F, p.X, p.Y);
		}

		// drawRoutine2 — танки X-COM (UFO), UnitSprite.cpp:756. Башня не рисуется (см. шапку).
		static void Routine2(Canvas c, Armor a, bool tftd, State s)
		{
			int hover = Flying(a, tftd) ? 32 : 0;
			if (s.Part > 0 && hover != 0) c.Put(104 + (s.Part - 1) * 8 + s.Anim, OffX, 0);   // пропеллер под корпусом
			c.Put(hover + s.Part * 8 + s.Dir, OffX, 0);
		}

		// drawRoutine3 — кибердиск (3) и вертолёт (22), UnitSprite.cpp:808
		static void Routine3(Canvas c, int r, State s)
		{
			if (r == 3 && s.Part > 0) c.Put(32 + (s.Part - 1) * 8 + s.Anim, OffX, 0);
			c.Put(s.Part * 8 + s.Dir, OffX, 0);
			if (r == 22 && s.Part > 0) c.Put(32 + (s.Part - 1) * 8 + s.Anim, OffX, 0);
		}

		// drawRoutine4 — гражданские, эфириалы, зомби (4), гражданские TFTD (17, 18), UnitSprite.cpp:854
		static void Routine4(Canvas c, int r, State s)
		{
			int stand = 0, walk = 8;
			int[] standConvert = { 3, 2, 1, 0, 7, 6, 5, 4 };
			if (r == 17) { stand = 64; walk = 0; }
			else if (r == 18) { stand = 140; walk = 76; }
			int f;
			if (s.Walking) f = walk + 8 * s.Dir + s.Phase;
			else if (r != 17) f = stand + s.Dir;
			else f = stand + standConvert[s.Dir];
			c.Put(f, OffX, 0);
		}

		// drawRoutine5 — сектопод, рипер, UnitSprite.cpp:995
		static void Routine5(Canvas c, State s)
		{
			if (s.Walking) c.Put(32 + s.Dir * 16 + s.Part * 4 + s.Phase / 2 % 4, OffX, 0);
			else c.Put(s.Part * 8 + s.Dir, OffX, 0);
		}

		// drawRoutine6 — змеелюди, UnitSprite.cpp:1024. xoffWalk оригинал задаёт, но тут же
		// перекрывает setX(offXSprite) — качания вбок нет и здесь.
		static void Routine6(Canvas c, State s)
		{
			int d = s.Dir, ph = s.Phase;
			int[] yoffWalk = { 3, 3, 2, 1, 0, 0, 1, 2 };
			int y = s.Walking ? yoffWalk[ph] : 0;
			var torso = new Piece(24 + d, y);
			var larm = new Piece(0 + d, y);
			var rarm = new Piece(8 + d, y);
			var legs = new Piece(s.Walking ? 32 + 8 * d + ph : 16 + d);
			Piece[] order;
			switch (d)
			{
			case 0: case 1: case 2: order = new[] { larm, legs, torso, rarm }; break;
			case 3: order = new[] { legs, torso, larm, rarm }; break;
			case 4: case 5: case 6: order = new[] { rarm, legs, torso, larm }; break;
			default: order = new[] { larm, rarm, legs, torso }; break;
			}
			foreach (var p in order) c.Put(p.F, p.X, p.Y);
		}

		// drawRoutine7 — хриссалид, UnitSprite.cpp:1220
		static void Routine7(Canvas c, State s)
		{
			int d = s.Dir, ph = s.Phase;
			int[] yoffWalk = { 1, 0, -1, 0, 1, 0, -1, 0 };
			Piece torso, legs, larm, rarm;
			if (s.Walking)
			{
				torso = new Piece(24 + d, yoffWalk[ph]);
				legs = new Piece(48 + 24 * d + ph);
				larm = new Piece(32 + 24 * d + ph);
				rarm = new Piece(40 + 24 * d + ph);
			}
			else
			{
				torso = new Piece(24 + d);
				legs = new Piece(16 + d);
				larm = new Piece(0 + d);
				rarm = new Piece(8 + d);
			}
			Piece[] order;
			switch (d)
			{
			case 0: case 1: case 2: order = new[] { larm, legs, torso, rarm }; break;
			case 3: order = new[] { legs, torso, larm, rarm }; break;
			case 4: case 5: case 6: order = new[] { rarm, legs, torso, larm }; break;
			default: order = new[] { larm, rarm, legs, torso }; break;
			}
			foreach (var p in order) c.Put(p.F, p.X, p.Y);
		}

		// drawRoutine11 — танки TFTD (целакант, дисплейсер), UnitSprite.cpp:1356. Корпус всегда
		// опущен на 4 точки; башня не рисуется (см. шапку).
		static void Routine11(Canvas c, Armor a, bool tftd, State s)
		{
			int body = 0, anim = s.Phase % 4;
			if (Flying(a, tftd)) { body = 128; anim = s.Anim % 4; }
			c.Put(body + s.Part * 4 + 16 * s.Dir + anim, OffX, 4);
		}

		// Предпросмотр size 2: четыре части в ромб 2x2 клеток (Map::drawUnit: клетка (x, y) на
		// экране в ((x - y) * 16, (x + y) * 8)), сзади вперёд: 0, 1, 2, 3. Ячейка 64x56.
		public static Img Compose2x2(List<Img> frames, int first)
		{
			var img = new Img(64, 56);
			int[] px = { 16, 32, 0, 16 }, py = { 0, 8, 8, 16 };
			for (int part = 0; part < 4; part++)
			{
				var f = frames[first + part];
				for (int y = 0; y < H; y++)
					for (int x = 0; x < W; x++)
					{
						byte v = f.Px[y * W + x];
						if (v != 0) img.Px[(py[part] + y) * 64 + px[part] + x] = v;
					}
			}
			return img;
		}
	}
}
