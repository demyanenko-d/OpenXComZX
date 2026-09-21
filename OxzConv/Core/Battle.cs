// Ресурсы наземного боя: тайлсеты (TERRAIN/*.MCD + *.PCK) и готовые карты (16_battlescape_plan.md §2.5).
// Первый этап — только отрисовка: генератора карт (mapScript) в порту ещё нет, поэтому поле
// замащивает блоками сам конвертер и кладёт готовую карту в BATTLE.PAK (читается с SD).
//
// BATMAP<i> (Blob, a = sx, b = sy, c = sz): u8 sx, sy, sz, ntiles; u16 тайлсет (SPRSET);
// u8 level (этаж камеры), u8 0; затем ntiles записей по 4 байта (u8 yofs = MCD.P_Level,
// u8 flags, u8 bigwall, u8 tile_type); затем sx*sy*sz клеток по 4 байта — номера тайлов+1
// (пол, западная стена, северная стена, объект), 0 — пусто. Порядок клеток: X быстрее всего,
// затем Y, затем Z снизу вверх (в оригинале Z идёт сверху вниз — переворачивает конвертер).
// Номер тайла — он же номер кадра в тайлсете карты.
using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;

namespace OxzConv
{
	class Battle
	{
		public const int TileW = 32, TileH = 40, ViewW = 320, ViewH = 144;
		public const int MapCount = 3;          // сколько карт готовим (движок берёт их по очереди)

		// Какими террейнами замащивать карты: первые доступные из списка (имена — terrains.rul).
		static readonly string[] TftdTerrains = { "SEABED", "CORAL", "VOLC", "PIPES", "ISLAND" };
		static readonly string[] UfoTerrains = { "CULTA", "DESERT", "FOREST", "MOUNT", "JUNGLE" };

		public class Mcd
		{
			public byte[] Frame = new byte[8];
			public int PLevel, TLevel, BigWall, TileType, ScanG, FrameBase;
			public int TuWalk, Alt;            // цена прохода (255 — не пройти) и часть-замена для двери
			public bool NoFloor, StopLOS, UfoDoor, Door, GravLift;
		}

		// MCD: 62 байта на запись (Mod/MapDataSet.cpp:107-153)
		public static List<Mcd> ReadMcd(byte[] buf)
		{
			var l = new List<Mcd>();
			for (int p = 0; p + 62 <= buf.Length; p += 62)
			{
				var r = new Mcd();
				Array.Copy(buf, p, r.Frame, 0, 8);
				r.ScanG = buf[p + 20] | (buf[p + 21] << 8);
				r.UfoDoor = buf[p + 30] != 0; r.StopLOS = buf[p + 31] != 0; r.NoFloor = buf[p + 32] != 0;
				r.BigWall = buf[p + 33]; r.GravLift = buf[p + 34] != 0; r.Door = buf[p + 35] != 0;
				r.TuWalk = buf[p + 39]; r.Alt = buf[p + 46];
				r.TLevel = (sbyte)buf[p + 48]; r.PLevel = buf[p + 49];
				r.TileType = buf[p + 53];
				l.Add(r);
			}
			return l;
		}

		public class MapBlock
		{
			public int Sx, Sy, Sz;
			public byte[] Cells;            // 4 байта на клетку, Z снизу вверх
			public int Cell(int x, int y, int z, int k) => Cells[(((z * Sy + y) * Sx + x) * 4) + k];
		}

		// MAP: u8 sizeY, sizeX, sizeZ, затем 4 байта на клетку; Z сверху вниз (BattlescapeGenerator.cpp:1577)
		public static MapBlock ReadMap(byte[] buf)
		{
			int sy = buf[0], sx = buf[1], sz = buf[2];
			var m = new MapBlock { Sx = sx, Sy = sy, Sz = sz, Cells = new byte[sx * sy * sz * 4] };
			int p = 3;
			for (int z = sz - 1; z >= 0; z--)
				for (int y = 0; y < sy; y++)
					for (int x = 0; x < sx; x++)
					{
						int o = ((z * sy + y) * sx + x) * 4;
						for (int k = 0; k < 4; k++) m.Cells[o + k] = p < buf.Length ? buf[p++] : (byte)0;
					}
			return m;
		}

		// Поле n x n блоков (как замащивает mapScript: блоки идут по порядку, с повтором)
		static MapBlock Tile(List<MapBlock> blocks, int n)
		{
			int bw = blocks[0].Sx, bh = blocks[0].Sy, sz = blocks.Max(b => b.Sz);
			var m = new MapBlock { Sx = bw * n, Sy = bh * n, Sz = sz, Cells = new byte[bw * n * bh * n * sz * 4] };
			for (int by = 0; by < n; by++)
				for (int bx = 0; bx < n; bx++)
				{
					var b = blocks[(by * n + bx) % blocks.Count];
					for (int z = 0; z < b.Sz; z++)
						for (int y = 0; y < b.Sy && by * bh + y < m.Sy; y++)
							for (int x = 0; x < b.Sx && bx * bw + x < m.Sx; x++)
							{
								int from = ((z * b.Sy + y) * b.Sx + x) * 4;
								int to = ((z * m.Sy + by * bh + y) * m.Sx + bx * bw + x) * 4;
								for (int k = 0; k < 4; k++) m.Cells[to + k] = b.Cells[from + k];
							}
				}
			return m;
		}

		// --------------------------------------------------------------- сборка пакета

		readonly GameFs gfs;
		readonly OxcomData ox;
		readonly string ruleFolder, game;
		readonly ResIds ids;
		readonly List<string> report;

		public Battle(GameFs gfs, OxcomData ox, string ruleFolder, string game, ResIds ids, List<string> report)
		{
			this.gfs = gfs; this.ox = ox; this.ruleFolder = ruleFolder; this.game = game; this.ids = ids; this.report = report;
		}

		class Terrain
		{
			public string Name;
			public List<string> Sets = new List<string>();
			public List<(string name, int w, int l)> Blocks = new List<(string, int, int)>();
		}

		Dictionary<string, Terrain> LoadTerrains()
		{
			var d = new Dictionary<string, Terrain>(StringComparer.Ordinal);
			var list = Y.List(Y.Get(Y.Load(ox.ReadText($"standard/{ruleFolder}/terrains.rul")), "terrains")) ?? new List<object>();
			foreach (var o in list)
			{
				var t = new Terrain { Name = Y.Str(Y.Get(o, "name")) };
				if (t.Name == null) continue;
				foreach (var s in Y.List(Y.Get(o, "mapDataSets")) ?? new List<object>()) t.Sets.Add(Y.Str(s));
				foreach (var b in Y.List(Y.Get(o, "mapBlocks")) ?? new List<object>())
					t.Blocks.Add((Y.Str(Y.Get(b, "name")), Y.Int(Y.Get(b, "width"), 10), Y.Int(Y.Get(b, "length"), 10)));
				d[t.Name] = t;
			}
			return d;
		}

		// Записи MCD и кадры всех наборов террейна подряд — так же их нумерует MAP
		(List<Mcd> parts, List<Img> frames) LoadSets(Terrain t)
		{
			var parts = new List<Mcd>();
			var frames = new List<Img>();
			foreach (var s in t.Sets)
			{
				if (!gfs.Has($"TERRAIN/{s}.MCD")) { report.Add($"  battle: no TERRAIN/{s}.MCD"); continue; }
				var mcd = ReadMcd(gfs.Read($"TERRAIN/{s}.MCD"));
				var pck = Formats.DecodePck(gfs.Read($"TERRAIN/{s}.PCK"), gfs.Read($"TERRAIN/{s}.TAB"), TileW, TileH);
				int b = frames.Count;
				foreach (var r in mcd) { r.FrameBase = b; parts.Add(r); }
				frames.AddRange(pck);
			}
			return (parts, frames);
		}

		public (int maps, string info) Build(string outDir, string prevDir, byte[] pal)
		{
			var terrains = LoadTerrains();
			var want = (game == "TFTD" ? TftdTerrains : UfoTerrains).Where(terrains.ContainsKey).Take(MapCount).ToList();
			var pak = new PakWriter(game);
			var info = new List<string>();
			int n = 0;
			foreach (var name in want)
			{
				var t = terrains[name];
				var (parts, frames) = LoadSets(t);
				// блоки 10x10 этого террейна (большие пропускаем — поле замащивается однородно)
				var small = t.Blocks.Where(b => b.w == 10 && b.l == 10 && gfs.Has($"MAPS/{b.name}.MAP")).Take(16).ToList();
				if (small.Count == 0 || parts.Count == 0) { report.Add($"  battle: terrain {name} skipped"); continue; }
				var blocks = small.Select(b => ReadMap(gfs.Read($"MAPS/{b.name}.MAP"))).ToList();
				var map = Tile(blocks, 4);                    // 4x4 блока = 40x40 клеток
				var (data, tiles, tileFrames) = Encode(map, parts, frames, name);
				if (data == null) continue;
				int mapId = ids.Id($"BATMAP{n}"), setId = ids.Id($"BATTILE{n}");
				data[4] = (byte)setId; data[5] = (byte)(setId >> 8);
				var sprset = Sprites.EncodeSprset(tileFrames);
				pak.Add(mapId, ResType.Blob, data, map.Sx, map.Sy, map.Sz);
				pak.Add(setId, ResType.Sprset, sprset, tileFrames.Count, TileW, TileH);
				info.Add($"{name} {map.Sx}x{map.Sy}x{map.Sz}, {tiles} tiles, map {data.Length / 1024}K + tiles {sprset.Length / 1024}K");
				if (prevDir != null)
				{
					var img = RenderPreview(map, parts, frames, Math.Min(map.Sz - 1, 1));
					File.WriteAllBytes(Path.Combine(prevDir, $"BATMAP{n}_{name}.png"), Png.EncodeIndexed(img, Cram.Pal6To8(pal)));
				}
				n++;
			}
			var r = pak.Write(Path.Combine(outDir, "BATTLE.PAK"));
			return (n, $"{n} maps, {(r.bytes + 512) / 1024} KB; " + string.Join("; ", info));
		}

		// Карта в наш формат: номера MCD переиндексируются в номера кадров тайлсета (только
		// использованные), кадры кладутся в том же порядке.
		(byte[] data, int tiles, List<Img> frames) Encode(MapBlock map, List<Mcd> parts, List<Img> frames, string name)
		{
			var remap = new Dictionary<int, int>();
			var order = new List<int>();
			for (int i = 0; i < map.Cells.Length; i++)
			{
				int v = map.Cells[i];
				if (v == 0 || remap.ContainsKey(v)) continue;
				if (v - 1 >= parts.Count) { map.Cells[i] = 0; continue; }
				remap[v] = order.Count + 1;
				order.Add(v);
			}
			if (order.Count > 255)
			{
				report.Add($"  battle: terrain {name} has {order.Count} tiles (>255) — skipped");
				return (null, 0, null);
			}
			var tiles = new List<Img>();
			var hdr = new List<byte> { (byte)map.Sx, (byte)map.Sy, (byte)map.Sz, (byte)order.Count, 0, 0, 0, 0 };
			foreach (var v in order)
			{
				var rec = parts[v - 1];
				int f = rec.FrameBase + rec.Frame[0];
				tiles.Add(f < frames.Count ? frames[f] : new Img(TileW, TileH));
				hdr.Add((byte)rec.PLevel);
				hdr.Add((byte)((rec.NoFloor ? 1 : 0) | (rec.StopLOS ? 2 : 0) | (rec.UfoDoor ? 4 : 0) | (rec.Door ? 8 : 0) | (rec.GravLift ? 16 : 0)));
				hdr.Add((byte)rec.BigWall);
				hdr.Add((byte)rec.TileType);
			}
			var cells = new byte[map.Cells.Length];
			for (int i = 0; i < cells.Length; i++)
			{
				int v = map.Cells[i];
				cells[i] = v != 0 && remap.TryGetValue(v, out var nv) ? (byte)nv : (byte)0;
			}
			var data = new byte[hdr.Count + cells.Length];
			hdr.CopyTo(data);
			cells.CopyTo(data, hdr.Count);
			return (data, order.Count, tiles);
		}

		// Эталон вида для сверки с эмулятором: алгоритм художника, камера в центре карты
		public static Img RenderPreview(MapBlock map, List<Mcd> parts, List<Img> frames, int level)
		{
			var img = new Img(ViewW, ViewH);
			double cx = map.Sx / 2.0, cy = map.Sy / 2.0;
			int ox = (int)Math.Round(ViewW / 2.0 - (cx - cy) * 16);
			int oy = (int)Math.Round(ViewH / 2.0 - ((cx + cy) * 8 - level * 24));
			for (int z = 0; z <= level && z < map.Sz; z++)
				for (int x = 0; x < map.Sx; x++)
					for (int y = 0; y < map.Sy; y++)
					{
						int sx = (x - y) * 16 + ox, sy = (x + y) * 8 - z * 24 + oy;
						if (sx <= -TileW || sx >= ViewW || sy <= -TileH || sy >= ViewH) continue;
						for (int k = 0; k < 4; k++)
						{
							int v = map.Cell(x, y, z, k);
							if (v == 0 || v - 1 >= parts.Count) continue;
							var rec = parts[v - 1];
							int f = rec.FrameBase + rec.Frame[0];
							if (f >= frames.Count) continue;
							Draw(img, frames[f], sx, sy - rec.PLevel);
						}
					}
			return img;
		}

		static void Draw(Img dst, Img src, int x0, int y0)
		{
			for (int y = 0; y < src.H; y++)
			{
				int py = y0 + y;
				if (py < 0 || py >= dst.H) continue;
				for (int x = 0; x < src.W; x++)
				{
					byte v = src.Px[y * src.W + x];
					int px = x0 + x;
					if (v == 0 || px < 0 || px >= dst.W) continue;
					dst.Px[py * dst.W + px] = v;
				}
			}
		}
	}
}
