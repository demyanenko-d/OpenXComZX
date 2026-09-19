// Данные для генератора наземных миссий (16_battlescape_plan.md §3): наборы MCD, блоки карт
// и тайлсеты по наборам. В отличие от Battle.cs, который замащивал готовое поле сам, здесь
// всё кладётся порознь — карту собирает движок по mapScript, как это делает OpenXcom
// (BattlescapeGenerator::generateMap).
//
// Форматы (u16 — младший байт первым):
//
// MCDSET_<набор> (Blob, a = число частей) — таблица частей набора, по 8 байт:
//   u16 frame   номер первого кадра в тайлсете этого набора (MCD.Frame[0])
//   u8  yofs    P_Level: на сколько пикселей часть опущена при выводе
//   u8  flags   1 нет пола, 2 держит обзор, 4 дверь НЛО, 8 дверь, 16 гравилифт
//   u8  bigwall тип «большой стены» (0 — нет)
//   u8  type    tile_type (MCD.TileType): 0 пол, 1 западная стена, 2 северная, 3 объект
//   i8  tlevel  T_Level
//   u8  0       запас
//
// MAPBLK_<блок> (Blob, a = sx, b = sy, c = sz) — блок карты как в оригинале, но снизу вверх:
//   u8 sx, sy, sz, 0; затем sx*sy*sz клеток по 4 байта (пол, западная стена, северная, объект).
//   Байт клетки — сквозной номер части в наборах ТОГО террейна, которому принадлежит блок
//   (0 — пусто), ровно как в MAP оригинала: движок переводит его в номер части миссии,
//   вычитая размеры наборов по порядку (RuleTerrain::getMapData).
//
// TILESET_<набор> (Sprset) — кадры набора, 32x40.
using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;

namespace OxzConv
{
	class BattleData
	{
		public const int TileW = 32, TileH = 40;

		public class Block
		{
			public string Name;
			public int W, L, Sz, Groups;
			public int ResId;
		}

		public class Terrain
		{
			public string Name, Script;
			public List<string> Sets = new List<string>();
			public List<Block> Blocks = new List<Block>();
		}

		// Команды mapScript в порядке MapScript.h:45 — движок разбирает их по этим номерам
		public const int C_ADD_BLOCK = 0, C_ADD_LINE = 1, C_ADD_CRAFT = 2, C_ADD_UFO = 3,
			C_DIG_TUNNEL = 4, C_FILL_AREA = 5, C_CHECK_BLOCK = 6, C_REMOVE_BLOCK = 7, C_RESIZE = 8;

		public class Command
		{
			public int Type, Size = 1, Direction, Chances = 100, Executions = 1, Label, Ufo = -1, TunnelLevel = -1;
			public List<int> Rects = new List<int>();      // x, y, w, h в модулях
			public List<int> Groups = new List<int>(), Blocks = new List<int>(), Freqs = new List<int>(),
				MaxUses = new List<int>(), Conditionals = new List<int>(), Replacements = new List<int>();
		}

		public class Script
		{
			public string Name;
			public List<Command> Commands = new List<Command>();
		}

		readonly GameFs gfs;
		readonly OxcomData ox;
		readonly string ruleFolder, game;
		readonly ResIds ids;
		readonly List<string> report;

		public readonly List<Terrain> Terrains = new List<Terrain>();
		public readonly List<Script> Scripts = new List<Script>();
		public readonly Dictionary<string, int> SetSize = new Dictionary<string, int>(StringComparer.Ordinal);

		public BattleData(GameFs gfs, OxcomData ox, string ruleFolder, string game, ResIds ids, List<string> report)
		{
			this.gfs = gfs; this.ox = ox; this.ruleFolder = ruleFolder; this.game = game; this.ids = ids; this.report = report;
		}

		// ---------------------------------------------------------------- правила
		// Террейны берутся из terrains.rul, а также из battlescapeTerrainData кораблей и НЛО:
		// у них такая же структура (наборы + блоки), и генератор кладёт их поверх поля.
		public void LoadRules()
		{
			void Add(object o, string nameField)
			{
				var t = new Terrain { Name = Y.Str(Y.Get(o, nameField)), Script = Y.Str(Y.Get(o, "script")) };
				if (t.Name == null) return;
				foreach (var s in Y.List(Y.Get(o, "mapDataSets")) ?? new List<object>()) t.Sets.Add(Y.Str(s));
				foreach (var b in Y.List(Y.Get(o, "mapBlocks")) ?? new List<object>())
				{
					var blk = new Block
					{
						Name = Y.Str(Y.Get(b, "name")),
						W = Y.Int(Y.Get(b, "width"), 10),
						L = Y.Int(Y.Get(b, "length"), 10),
						Groups = GroupMask(Y.Get(b, "groups")),
					};
					if (blk.Name != null) t.Blocks.Add(blk);
				}
				if (t.Sets.Count != 0 && t.Blocks.Count != 0) Terrains.Add(t);
			}

			foreach (var o in Y.List(Y.Get(Y.Load(ox.ReadText($"standard/{ruleFolder}/terrains.rul")), "terrains")) ?? new List<object>())
				Add(o, "name");
			// корабли и НЛО: их карта лежит в battlescapeTerrainData, имя террейна — имя карты
			foreach (var file in new[] { "crafts.rul", "ufos.rul" })
			{
				var list = Y.List(Y.Get(Y.Load(ox.ReadText($"standard/{ruleFolder}/{file}")), file == "crafts.rul" ? "crafts" : "ufos"));
				foreach (var o in list ?? new List<object>())
				{
					var d = Y.Get(o, "battlescapeTerrainData");
					if (d != null) Add(d, "name");
				}
			}
		}

		// mapScripts.rul: список скриптов, у каждого — список команд (MapScript::load)
		public void LoadScripts()
		{
			var cmdName = new Dictionary<string, int>(StringComparer.Ordinal)
			{
				{ "addBlock", C_ADD_BLOCK }, { "addLine", C_ADD_LINE }, { "addCraft", C_ADD_CRAFT },
				{ "addUFO", C_ADD_UFO }, { "digTunnel", C_DIG_TUNNEL }, { "fillArea", C_FILL_AREA },
				{ "checkBlock", C_CHECK_BLOCK }, { "removeBlock", C_REMOVE_BLOCK }, { "resize", C_RESIZE },
			};
			var dirName = new Dictionary<string, int>(StringComparer.OrdinalIgnoreCase)
			{ { "vertical", 1 }, { "horizontal", 2 }, { "both", 3 } };
			// тип замены в тоннеле: 0 пол, 1 западная стена, 2 северная стена, 3 объект
			var replName = new Dictionary<string, int>(StringComparer.OrdinalIgnoreCase)
			{ { "floor", 0 }, { "westWall", 1 }, { "northWall", 2 }, { "object", 3 } };

			foreach (var o in Y.List(Y.Get(Y.Load(ox.ReadText($"standard/{ruleFolder}/mapScripts.rul")), "mapScripts")) ?? new List<object>())
			{
				var sc = new Script { Name = Y.Str(Y.Get(o, "type")) };
				if (sc.Name == null) continue;
				foreach (var c in Y.List(Y.Get(o, "commands")) ?? new List<object>())
				{
					string ct = Y.Str(Y.Get(c, "type"));
					if (ct == null || !cmdName.ContainsKey(ct)) { report.Add($"  battle: script {sc.Name}: unknown command {ct}"); continue; }
					var cm = new Command { Type = cmdName[ct] };
					cm.Size = Y.Int(Y.Get(c, "size"), 1);
					string dir = Y.Str(Y.Get(c, "direction"));
					if (dir != null && dirName.ContainsKey(dir)) cm.Direction = dirName[dir];
					cm.Chances = Y.Int(Y.Get(c, "executionChances"), 100);
					cm.Executions = Y.Int(Y.Get(c, "executions"), 1);
					cm.Label = Y.Int(Y.Get(c, "label"), 0);
					foreach (var r in Y.List(Y.Get(c, "rects")) ?? new List<object>())
					{
						var v = Y.List(r);
						for (int i = 0; i < 4; i++) cm.Rects.Add(v != null && i < v.Count ? Y.Int(v[i]) : 0);
					}
					void Nums(string field, List<int> to)
					{
						var v = Y.Get(c, field);
						if (v == null) return;
						var l = Y.List(v);
						if (l != null) foreach (var x in l) to.Add(Y.Int(x));
						else to.Add(Y.Int(v));
					}
					Nums("groups", cm.Groups); Nums("blocks", cm.Blocks); Nums("freqs", cm.Freqs);
					Nums("maxUses", cm.MaxUses); Nums("conditionals", cm.Conditionals);
					var td = Y.Get(c, "tunnelData");
					if (td != null)
					{
						cm.TunnelLevel = Y.Int(Y.Get(td, "level"), 0);
						foreach (var r in Y.List(Y.Get(td, "MCDReplacements")) ?? new List<object>())
						{
							string rt = Y.Str(Y.Get(r, "type"));
							cm.Replacements.Add(rt != null && replName.ContainsKey(rt) ? replName[rt] : 3);
							cm.Replacements.Add(Y.Int(Y.Get(r, "set"), -1));
							cm.Replacements.Add(Y.Int(Y.Get(r, "entry"), -1));
						}
					}
					// UFOName ссылается на террейн из ufos.rul — номер разрешаем при записи
					cm.Ufo = TerrainIndex(Y.Str(Y.Get(c, "UFOName")));
					sc.Commands.Add(cm);
				}
				Scripts.Add(sc);
			}
		}

		public int TerrainIndex(string name)
		{
			if (name == null) return -1;
			for (int i = 0; i < Terrains.Count; i++) if (Terrains[i].Name == name) return i;
			return -1;
		}

		public int ScriptIndex(string name)
		{
			if (name == null) return -1;
			for (int i = 0; i < Scripts.Count; i++) if (Scripts[i].Name == name) return i;
			return -1;
		}

		// groups: число или список чисел. Группы OpenXcom: 0 обычный, 1 посадочная площадка,
		// 2 перекрёсток дорог, 3 дорога V, 4 дорога H, 5 НЛО, 6 корабль (MapBlockType).
		static int GroupMask(object v)
		{
			if (v == null) return 1;                       // по умолчанию группа 0
			int m = 0;
			var l = Y.List(v);
			if (l != null) foreach (var x in l) m |= 1 << (Y.Int(x) & 15);
			else m |= 1 << (Y.Int(v) & 15);
			return m;
		}

		// ---------------------------------------------------------------- таблицы для движка
		// TERRAINS (Blob, a = число террейнов): u16 n, n x u16 смещение записи; запись:
		//   i16 script (-1 нет), u8 nSets, u8 nBlocks, nSets x u16 ресурс MCDSET,
		//   nBlocks x { u16 ресурс MAPBLK, u8 w, u8 l, u8 sz, u8 groups } — размеры в клетках,
		//   groups — маска групп блока (бит 0 — обычный, 1 — посадочная площадка, …).
		byte[] EncodeTerrains()
		{
			var head = new List<byte>();
			var body = new List<byte>();
			void U16(List<byte> to, int v) { to.Add((byte)v); to.Add((byte)(v >> 8)); }
			U16(head, Terrains.Count);
			int tableEnd = 2 + Terrains.Count * 2;
			foreach (var t in Terrains)
			{
				U16(head, tableEnd + body.Count);
				U16(body, t.Script == null ? 0xFFFF : (ScriptIndex(t.Script) < 0 ? 0xFFFF : ScriptIndex(t.Script)));
				body.Add((byte)t.Sets.Count); body.Add((byte)t.Blocks.Count);
				foreach (var s in t.Sets) U16(body, ids.Id($"MCDSET_{s}"));
				foreach (var b in t.Blocks)
				{
					U16(body, ids.Id($"MAPBLK_{b.Name}"));
					body.Add((byte)b.W); body.Add((byte)b.L); body.Add((byte)b.Sz); body.Add((byte)b.Groups);
				}
			}
			return head.Concat(body).ToArray();
		}

		// MAPSCRIPTS (Blob, a = число скриптов): u16 n, n x u16 смещение; скрипт: u16 команд,
		// затем команды. Команда (все поля i16, длина в словах — первым):
		//   len, type, size, direction, chances, executions, label, ufo (номер террейна), tunnelLevel,
		//   nRects, nGroups, nBlocks, nFreqs, nMaxUses, nCond, nRepl, затем сами списки в этом порядке.
		byte[] EncodeScripts()
		{
			var head = new List<byte>();
			var body = new List<byte>();
			void U16(List<byte> to, int v) { to.Add((byte)v); to.Add((byte)(v >> 8)); }
			U16(head, Scripts.Count);
			int tableEnd = 2 + Scripts.Count * 2;
			foreach (var sc in Scripts)
			{
				U16(head, tableEnd + body.Count);
				U16(body, sc.Commands.Count);
				foreach (var c in sc.Commands)
				{
					var w = new List<int> { 0, c.Type, c.Size, c.Direction, c.Chances, c.Executions, c.Label, c.Ufo, c.TunnelLevel,
						c.Rects.Count / 4, c.Groups.Count, c.Blocks.Count, c.Freqs.Count, c.MaxUses.Count, c.Conditionals.Count, c.Replacements.Count / 3 };
					w.AddRange(c.Rects); w.AddRange(c.Groups); w.AddRange(c.Blocks);
					w.AddRange(c.Freqs); w.AddRange(c.MaxUses); w.AddRange(c.Conditionals); w.AddRange(c.Replacements);
					w[0] = w.Count;
					foreach (var v in w) U16(body, v & 0xFFFF);
				}
			}
			return head.Concat(body).ToArray();
		}

		// ---------------------------------------------------------------- данные
		public (int sets, int blocks, long mcdBytes, long mapBytes, long tileBytes) Build(string outDir, string prevDir, byte[] pal)
		{
			var mcdPak = new PakWriter(game);
			var mapPak = new PakWriter(game);
			var tilePak = new PakWriter(game);
			var doneSets = new HashSet<string>(StringComparer.Ordinal);
			var doneBlocks = new HashSet<string>(StringComparer.Ordinal);
			int nSets = 0, nBlocks = 0;
			long mcdBytes = 0, tileBytes = 0, mapBytes = 0;

			foreach (var t in Terrains)
			{
				foreach (var s in t.Sets)
				{
					if (!doneSets.Add(s)) continue;
					if (!gfs.Has($"TERRAIN/{s}.MCD")) { report.Add($"  battle: no TERRAIN/{s}.MCD"); continue; }
					var parts = Battle.ReadMcd(gfs.Read($"TERRAIN/{s}.MCD"));
					SetSize[s] = parts.Count;
					var data = new byte[parts.Count * 8];
					for (int i = 0; i < parts.Count; i++)
					{
						var r = parts[i];
						int o = i * 8;
						data[o] = r.Frame[0]; data[o + 1] = 0;          // кадры набора идут подряд
						data[o + 2] = (byte)r.PLevel;
						data[o + 3] = (byte)((r.NoFloor ? 1 : 0) | (r.StopLOS ? 2 : 0) | (r.UfoDoor ? 4 : 0) | (r.Door ? 8 : 0) | (r.GravLift ? 16 : 0));
						data[o + 4] = (byte)r.BigWall;
						data[o + 5] = (byte)r.TileType;
						data[o + 6] = (byte)r.TLevel;
					}
					mcdPak.Add(ids.Id($"MCDSET_{s}"), ResType.Blob, data, parts.Count);
					mcdBytes += data.Length;
					nSets++;

					if (!gfs.Has($"TERRAIN/{s}.PCK") || !gfs.Has($"TERRAIN/{s}.TAB")) continue;
					var frames = Formats.DecodePck(gfs.Read($"TERRAIN/{s}.PCK"), gfs.Read($"TERRAIN/{s}.TAB"), TileW, TileH);
					var sprset = Sprites.EncodeSprset(frames);
					tilePak.Add(ids.Id($"TILESET_{s}"), ResType.Sprset, sprset, frames.Count, TileW, TileH);
					tileBytes += sprset.Length;
				}

				foreach (var b in t.Blocks)
				{
					if (!doneBlocks.Add(b.Name)) continue;
					if (!gfs.Has($"MAPS/{b.Name}.MAP")) { report.Add($"  battle: no MAPS/{b.Name}.MAP"); continue; }
					var m = Battle.ReadMap(gfs.Read($"MAPS/{b.Name}.MAP"));
					b.Sz = m.Sz;
					var data = new byte[4 + m.Cells.Length];
					data[0] = (byte)m.Sx; data[1] = (byte)m.Sy; data[2] = (byte)m.Sz;
					Array.Copy(m.Cells, 0, data, 4, m.Cells.Length);
					mapPak.Add(ids.Id($"MAPBLK_{b.Name}"), ResType.Blob, data, m.Sx, m.Sy, m.Sz);
					mapBytes += data.Length;
					nBlocks++;
				}
			}
			// размеры блоков из правил могли разойтись с файлом — движку нужны настоящие
			foreach (var t in Terrains)
				foreach (var b in t.Blocks)
					if (b.Sz == 0 && gfs.Has($"MAPS/{b.Name}.MAP"))
						b.Sz = gfs.Read($"MAPS/{b.Name}.MAP")[2];

			mcdPak.Add(ids.Id("TERRAINS"), ResType.Blob, EncodeTerrains(), Terrains.Count);
			mcdPak.Add(ids.Id("MAPSCRIPTS"), ResType.Blob, EncodeScripts(), Scripts.Count);
			var rm = mcdPak.Write(Path.Combine(outDir, "MCD.PAK"));
			var rb = mapPak.Write(Path.Combine(outDir, "MAPS.PAK"));
			var rt = tilePak.Write(Path.Combine(outDir, "TILES.PAK"));
			report.Add($"MCD.PAK: {nSets} sets, {(rm.bytes + 512) / 1024} KB");
			report.Add($"MAPS.PAK: {nBlocks} blocks, {(rb.bytes + 512) / 1024} KB");
			report.Add($"TILES.PAK: {(rt.bytes + 512) / 1024} KB");
			return (nSets, nBlocks, mcdBytes, mapBytes, tileBytes);
		}
	}
}
