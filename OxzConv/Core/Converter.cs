// Конвертация каталога игры (UFO или TFTD) в данные OpenXComZX (каталог OXZ/<игра>).
// Шаги и форматы — project_docs/09_converter.md.
using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text;

namespace OxzConv
{
	class Converter
	{
		readonly GameFs gfs;
		readonly OxcomData ox;
		readonly Action<string> log;
		public readonly string Game, RuleFolder, OutDir;
		readonly string prevDir, lang;
		readonly ResIds ids = new ResIds();
		readonly List<string> report = new List<string>();
		readonly Dictionary<string, byte[]> pal6 = new Dictionary<string, byte[]>();
		readonly List<string> palOrder = new List<string>();
		Dictionary<string, int> strIndex;
		Func<string, (string pal, int backpal)> screenPal;
		Dictionary<string, string> bgScreen;

		public Converter(string gameDir, string outRoot, string previewRoot, OxcomData ox, string lang, Action<string> log)
		{
			gfs = new GameFs(gameDir);
			Game = gfs.DetectGame();
			RuleFolder = Game == "TFTD" ? "xcom2" : "xcom1";
			OutDir = Path.Combine(outRoot, "OXZ", Game);
			prevDir = previewRoot != null ? Path.Combine(previewRoot, Game) : null;
			this.ox = ox; this.lang = lang; this.log = log;
		}

		public void Run()
		{
			log($"oxzconv: game {Game} in {gfs.Root} (OpenXcom data: {ox.Source})");
			Directory.CreateDirectory(OutDir);
			if (prevDir != null) Directory.CreateDirectory(prevDir);
			Palettes();
			screenPal = Interfaces.ScreenPalettes(ox, RuleFolder);
			bgScreen = BackgroundScreens();
			GeoImages();
			BattleUi();
			BattleMaps();
			MusicStep();
			Lang();
			Cutscenes();
			Rules();
			File.WriteAllText(Path.Combine(OutDir, "RESIDS.TXT"), string.Concat(ids.Dyn.Select(kv => $"{kv.Value:X4} {kv.Key}\r\n")), Encoding.ASCII);
			foreach (var l in report) log("  " + l);
			log($"oxzconv: done -> {OutDir}");
		}

		string Kb(long bytes) => ((bytes + 512) / 1024).ToString();

		// ------------------------------------------------------------ палитры
		// Логические имена как в OpenXcom (Mod::loadVanillaResources).
		void AddPal(string name, byte[] p) { if (!pal6.ContainsKey(name)) palOrder.Add(name); pal6[name] = p; }

		void Palettes()
		{
			string[] names = { "PAL_GEOSCAPE", "PAL_BASESCAPE", "PAL_GRAPHS", "PAL_UFOPAEDIA", "PAL_BATTLEPEDIA" };
			var pals = Formats.DecodePalettes(gfs.Read("GEODATA/PALETTES.DAT"));
			for (int i = 0; i < pals.Count && i < names.Length; i++) AddPal(names[i], pals[i]);
			AddPal("BACKPALS", Formats.DecodeBackpals(gfs.Read("GEODATA/BACKPALS.DAT")));

			// палитры боя: UFO — палитра 4 + серая рампа 240..255; TFTD — D0..D3.LBM
			string[] lbms = { "D0.LBM", "D1.LBM", "D2.LBM", "D3.LBM" };
			string[] battle = { "PAL_BATTLESCAPE", "PAL_BATTLESCAPE_1", "PAL_BATTLESCAPE_2", "PAL_BATTLESCAPE_3" };
			int[][] backPal = { new[] { 0, 5, 4 }, new[] { 0, 10, 34 }, new[] { 2, 9, 24 }, new[] { 2, 0, 24 } };
			if (gfs.Has("UFOGRAPH/D0.LBM"))
			{
				for (int i = 0; i < lbms.Length; i++)
				{
					if (!gfs.Has("UFOGRAPH/" + lbms[i])) continue;
					var pal = Formats.DecodeLbm(gfs.Read("UFOGRAPH/" + lbms[i])).pal;
					for (int c = 0; c < 3; c++) pal[255 * 3 + c] = (byte)backPal[i][c];
					AddPal(battle[i], Cram.Pal8To6(pal));
				}
			}
			else if (pal6.ContainsKey("PAL_BATTLEPEDIA"))
			{
				var p = (byte[])pal6["PAL_BATTLEPEDIA"].Clone();
				int[,] grad = { {140,152,148},{132,136,140},{116,124,132},{108,116,124},{92,104,108},{84,92,100},{76,80,92},{56,68,84},
					{48,56,68},{40,48,56},{32,36,48},{24,28,32},{16,20,24},{8,12,16},{3,4,8},{3,3,6} };
				for (int i = 0; i < 16; i++) for (int c = 0; c < 3; c++) p[(240 + i) * 3 + c] = (byte)(grad[i, c] >> 2);
				AddPal("PAL_BATTLESCAPE", p);
			}
			var pak = new PakWriter(Game);
			foreach (var name in palOrder) pak.Add(ids.Id(name), ResType.Pal, Cram.Pal6ToCram(pal6[name]), pal6[name].Length / 3);
			var r = pak.Write(Path.Combine(OutDir, "PAL.PAK"));
			report.Add($"PAL.PAK: {r.count} palettes ({string.Join(", ", palOrder)})");
		}

		// ------------------------------------------------------------ предпросмотр
		Dictionary<string, string> BackgroundScreens()
		{
			var list = Y.List(Y.Load(OxcomData.ReadDataText("screen_backgrounds.json")));
			var count = new Dictionary<string, int>(StringComparer.Ordinal);
			var order = new List<string>();
			foreach (var x in list)
			{
				string k = Y.Str(Y.Get(x, "background")) + "|" + Y.Str(Y.Get(x, "interface"));
				if (!count.ContainsKey(k)) { count[k] = 0; order.Add(k); }
				count[k]++;
			}
			var best = new Dictionary<string, (string ui, int n)>(StringComparer.Ordinal);
			foreach (var k in order)
			{
				var parts = k.Split('|');
				if (!best.TryGetValue(parts[0], out var b) || count[k] > b.n) best[parts[0]] = (parts[1], count[k]);
			}
			return best.ToDictionary(kv => kv.Key, kv => kv.Value.ui, StringComparer.Ordinal);
		}

		byte[] PalOfScreen(string type)
		{
			var (pal, backpal) = screenPal(type);
			var p = (byte[])(pal6.TryGetValue(pal, out var pp) ? pp : pal6["PAL_GEOSCAPE"]).Clone();
			if (backpal >= 0) Array.Copy(pal6["BACKPALS"], backpal * 48, p, 224 * 3, 48);
			return p;
		}

		void Preview(string name, Img img, string palName = null, string screen = null)
		{
			if (prevDir == null) return;
			var p = screen != null ? PalOfScreen(screen) : pal6.TryGetValue(palName ?? "", out var pp) ? pp : pal6["PAL_GEOSCAPE"];
			File.WriteAllBytes(Path.Combine(prevDir, name.Replace('/', '_').Replace('\\', '_') + ".png"), Png.EncodeIndexed(img, Cram.Pal6To8(p)));
		}

		string PalFor(string name)
		{
			if (name.StartsWith("UP", StringComparison.Ordinal) && name.Length > 2 && char.IsDigit(name[2])) return pal6.ContainsKey("PAL_UFOPAEDIA") ? "PAL_UFOPAEDIA" : "PAL_BASESCAPE";
			if (System.Text.RegularExpressions.Regex.IsMatch(name, "^BACK0[1-3]|^GRAPH")) return "PAL_GRAPHS";
			if (System.Text.RegularExpressions.Regex.IsMatch(name, "^TAC|BORD|ICONS|^MAN_")) return "PAL_BATTLESCAPE";
			return "PAL_BASESCAPE";
		}

		// ------------------------------------------------------------ геоскейп, базы, Уфопедия
		Img LoadGeographImage(string file)
		{
			string ext = file.Substring(file.Length - 3).ToUpperInvariant();
			var buf = gfs.Read("GEOGRAPH/" + file);
			if (ext == "SCR") return Formats.DecodeScr(buf);
			if (ext == "SPK") return Formats.DecodeSpk(buf);
			if (ext == "BDY") return Formats.DecodeBdy(buf);
			return null;
		}

		void GeoImages()
		{
			// GEO.PAK вшивается в SPG; фоны окон BACKnn.SCR (по 64 КБ) — в BACK.PAK на SD,
			// картинки Уфопедии — в UFOP.PAK на SD (src/kernel/sdres.c)
			var geo = new PakWriter(Game); var ufop = new PakWriter(Game); var back = new PakWriter(Game);
			int nGeo = 0, nUfop = 0, nBack = 0;
			var upRe = new System.Text.RegularExpressions.Regex(@"^UP\d{3}\.");
			var backRe = new System.Text.RegularExpressions.Regex(@"^BACK\d\d\.SCR$");
			foreach (var file in gfs.List("GEOGRAPH"))
			{
				var img = LoadGeographImage(file);
				if (img == null) continue;
				bool isUfop = upRe.IsMatch(file) || file.StartsWith("UP_BORD", StringComparison.Ordinal);
				bool isBack = backRe.IsMatch(file.ToUpperInvariant());
				(isUfop ? ufop : isBack ? back : geo).Add(ids.Id(file), ResType.Img8, Sprites.EncodeImg8(img), img.W, img.H);
				if (isUfop) nUfop++; else if (isBack) nBack++; else nGeo++;
				if (bgScreen.TryGetValue(file.ToUpperInvariant(), out var scr)) Preview(file, img, screen: scr);
				else Preview(file, img, PalFor(file));
			}
			{
				var img = Formats.DecodeScr(gfs.Read("GEODATA/INTERWIN.DAT"), 160, 600);
				geo.Add(ids.Id("INTERWIN.DAT"), ResType.Img8, Sprites.EncodeImg8(img), img.W, img.H);
				Preview("INTERWIN.DAT", img, "PAL_GEOSCAPE");
				nGeo++;
			}
			var sets = new (string name, Func<List<Img>> load, string pal)[]
			{
				("BASEBITS.PCK", () => Formats.DecodePck(gfs.Read("GEOGRAPH/BASEBITS.PCK"), gfs.Read("GEOGRAPH/BASEBITS.TAB"), 32, 40), "PAL_BASESCAPE"),
				("INTICON.PCK", () => Formats.DecodePck(gfs.Read("GEOGRAPH/INTICON.PCK"), gfs.Read("GEOGRAPH/INTICON.TAB"), 32, 40), "PAL_BASESCAPE"),
				("SCANG.DAT", () => Formats.DecodeDatSet(gfs.Read("GEODATA/SCANG.DAT"), 4, 4), "PAL_GEOSCAPE"),
			};
			foreach (var (name, load, pal) in sets)
			{
				var frames = load();
				geo.Add(ids.Id(name), ResType.Sprset, Sprites.EncodeSprset(frames), frames.Count, frames[0].W, frames[0].H);
				Preview(name, Sprites.Sheet(frames), pal);
				nGeo++;
			}
			{
				var (map, mixed) = WorldMap.Build(gfs.Read("GEODATA/WORLD.DAT"));
				geo.Add(ids.Id("WORLDMAP"), ResType.Blob, map, mixed);
				report.Add($"WORLDMAP: {mixed} coastal cells, {Kb(map.Length)} KB");
				nGeo++;
				GlobeArcs arcs;
				var (globe, info) = GlobeData.Build(gfs.Read("GEODATA/WORLD.DAT"), out arcs);
				geo.Add(ids.Id("GLOBE"), ResType.Blob, globe);
				report.Add($"GLOBE: {info}, {Kb(globe.Length)} KB");
				nGeo++;
				// предрасчитанные виды глобуса (зумы 0–2) на карту рёбрами — globe.md §12.11
				var gv = GlobeEdges.Build(arcs, Path.Combine(OutDir, "GVIEW.PAK"));
				report.Add($"GVIEW.PAK: {gv.info}; {gv.bytes / 1048576.0:0.0} MB");
				// узоры глобуса: 39 кадров 32x32 как есть (3 набора по 13, зумы 4–5, 2–3, 0–1)
				var tex = gfs.Read("GEOGRAPH/TEXTURE.DAT");
				geo.Add(ids.Id("TEXTURE.DAT"), ResType.Blob, tex, tex.Length / 1024);
				Preview("TEXTURE.DAT", Sprites.Sheet(Formats.DecodeDatSet(tex, 32, 32)), "PAL_GEOSCAPE");
				nGeo++;
			}
			var r1 = geo.Write(Path.Combine(OutDir, "GEO.PAK"));
			var r2 = ufop.Write(Path.Combine(OutDir, "UFOP.PAK"));
			var r3 = back.Write(Path.Combine(OutDir, "BACK.PAK"));
			report.Add($"GEO.PAK: {nGeo} resources, {Kb(r1.bytes)} KB");
			report.Add($"UFOP.PAK: {nUfop} images, {Kb(r2.bytes)} KB");
			report.Add($"BACK.PAK: {nBack} backgrounds, {Kb(r3.bytes)} KB");
		}

		// ------------------------------------------------------------ интерфейс боя и предметы
		// Имена как в OpenXcom (Mod::loadBattlescapeResources): у UFO панели и TAC01 — SPK
		// под расширениями .PCK/.SCR, у TFTD — BDY.
		void BattleUi()
		{
			var bat = new PakWriter(Game); var items = new PakWriter(Game);
			int nBat = 0, nItems = 0;
			void AddImg(PakWriter pak, string name, Img img, string pal) { pak.Add(ids.Id(name), ResType.Img8, Sprites.EncodeImg8(img), img.W, img.H); Preview(name, img, pal); }
			if (gfs.Has("UFOGRAPH/TAC00.SCR")) { AddImg(bat, "TAC00.SCR", Formats.DecodeScr(gfs.Read("UFOGRAPH/TAC00.SCR")), "PAL_BATTLESCAPE"); nBat++; }
			foreach (var name in new[] { "TAC01.SCR", "DETBORD.PCK", "DETBORD2.PCK", "ICONS.PCK", "MEDIBORD.PCK", "SCANBORD.PCK", "UNIBORD.PCK" })
			{
				if (!gfs.Has("UFOGRAPH/" + name)) continue;
				AddImg(bat, name, Formats.DecodeSpk(gfs.Read("UFOGRAPH/" + name)), "PAL_BATTLESCAPE"); nBat++;
			}
			foreach (var file in gfs.List("UFOGRAPH", "BDY"))
			{
				string bas = file.Substring(0, file.Length - 3);
				string name = bas.StartsWith("MAN", StringComparison.Ordinal) ? bas + "SPK" : bas == "TAC01." ? bas + "SCR" : bas + "PCK";
				var img = Formats.DecodeBdy(gfs.Read("UFOGRAPH/" + file));
				if (name.StartsWith("MAN", StringComparison.Ordinal)) { AddImg(items, name, img, "PAL_BATTLESCAPE"); nItems++; }
				else { AddImg(bat, name, img, "PAL_BATTLESCAPE"); nBat++; }
			}
			foreach (var file in gfs.List("UFOGRAPH", "SPK"))
			{
				AddImg(items, file, Formats.DecodeSpk(gfs.Read("UFOGRAPH/" + file)), "PAL_BATTLESCAPE"); nItems++;
			}
			var setDefs = new (PakWriter pak, string name, string dir, int w, int h)[]
			{
				(bat, "CURSOR.PCK", "UFOGRAPH", 32, 40), (bat, "SMOKE.PCK", "UFOGRAPH", 32, 40),
				(bat, "HIT.PCK", "UFOGRAPH", 32, 40), (bat, "X1.PCK", "UFOGRAPH", 128, 64),
				(bat, "MEDIBITS.DAT", "UFOGRAPH", 52, 58), (bat, "DETBLOB.DAT", "UFOGRAPH", 16, 16),
				(bat, "SPICONS.DAT", "UFOGRAPH", 32, 24),
				(items, "BIGOBS.PCK", "UNITS", 32, 48), (items, "FLOOROB.PCK", "UNITS", 32, 40), (items, "HANDOB.PCK", "UNITS", 32, 40),
			};
			foreach (var (pak, name, dir, w, h) in setDefs)
			{
				if (!gfs.Has($"{dir}/{name}")) continue;
				var frames = name.EndsWith(".DAT", StringComparison.Ordinal)
					? Formats.DecodeDatSet(gfs.Read($"{dir}/{name}"), w, h)
					: Formats.DecodePck(gfs.Read($"{dir}/{name}"), gfs.Read($"{dir}/{name.Replace(".PCK", ".TAB")}"), w, h);
				pak.Add(ids.Id(name), ResType.Sprset, Sprites.EncodeSprset(frames), frames.Count, w, h);
				Preview(name, Sprites.Sheet(frames, w > 64 ? 4 : 16), "PAL_BATTLESCAPE");
				if (pak == bat) nBat++; else nItems++;
			}
			var rb = bat.Write(Path.Combine(OutDir, "BATUI.PAK"));
			var ri = items.Write(Path.Combine(OutDir, "ITEMS.PAK"));
			report.Add($"BATUI.PAK: {nBat} resources, {Kb(rb.bytes)} KB");
			report.Add($"ITEMS.PAK: {nItems} resources, {Kb(ri.bytes)} KB");
		}

		// ------------------------------------------------------------ карты боя (BATTLE.PAK)
		// Готовые поля из блоков террейна и их тайлсеты: генератора карт ещё нет, движку нужны
		// данные для отрисовки (Core/Battle.cs, 16_battlescape_plan.md §2.5).
		void BattleMaps()
		{
			var pal = pal6.TryGetValue("PAL_BATTLESCAPE", out var p) ? p : pal6["PAL_GEOSCAPE"];
			var (n, info) = new Battle(gfs, ox, RuleFolder, Game, ids, report).Build(OutDir, prevDir, pal);
			report.Add("BATTLE.PAK: " + info);
		}

		// ------------------------------------------------------------ музыка (MUSIC.PAK)
		// ADLIB.CAT (+ AINTRO.CAT у UFO за его концом) -> покадровый поток OPL3.
		// Номер ресурса = #0300 + номер типа в Data/music_types.txt.
		void MusicStep()
		{
			if (!gfs.Has("SOUND/ADLIB.CAT")) return;
			var types = Registry.Index(Registry.Load("music_types.txt"));
			var rules = Y.List(Y.Get(Y.Load(ox.ReadText($"standard/{RuleFolder}/music.rul")), "musics")) ?? new List<object>();
			var adlib = gfs.Read("SOUND/ADLIB.CAT"); var aEnt = Music.CatEntries(adlib);
			var intro = gfs.Has("SOUND/AINTRO.CAT") ? gfs.Read("SOUND/AINTRO.CAT") : null;
			var iEnt = intro != null ? Music.CatEntries(intro) : new List<(int, int)>();
			var pak = new PakWriter(Game);
			var payk = new PakWriter(Game);   // тот же трек на трёх каналах AY
			var pfm = new PakWriter(Game);    // он же на FM-частях двух YM2203
			var cache = new Dictionary<int, (Music.Render r, (byte[] data, int maxWrites) s)>();
			int n = 0, maxw = 0, nloop = 0, nonce = 0, ayMax = 0, fmMax = 0; long bytes = 0, ayBytes = 0, fmBytes = 0; double secs = 0;
			if (prevDir != null) Directory.CreateDirectory(Path.Combine(prevDir, "music"));
			foreach (var m in rules)
			{
				string type = Y.Str(Y.Get(m, "type"));
				if (!Y.Has(m, "catPos") || type == null || !types.ContainsKey(type)) continue;
				int catPos = Y.Int(Y.Get(m, "catPos"));
				byte[] data;
				if (catPos < aEnt.Count) data = Music.CatTrack(adlib, aEnt[catPos]);
				else if (intro != null && catPos - aEnt.Count < iEnt.Count) data = Music.CatTrack(intro, iEnt[catPos - aEnt.Count]);
				else continue;
				if (!cache.ContainsKey(catPos))
				{
					var r = Music.RenderTrack(data);
					cache[catPos] = (r, Music.EncodeStream(r));
					if (prevDir != null) File.WriteAllBytes(Path.Combine(prevDir, "music", $"{catPos:00}_{type}.vgm"), Music.ToVgm(r));
					secs += r.Seconds;
				}
				var (rr, ss) = cache[catPos];
				// normalization из music.rul (умолчание 0.76) — громкость трека относительно других;
				// поток остаётся на полной шкале, коэффициент применяет плеер вместе с регулятором (22 §3.3)
				int vol = (int)(127 * Y.Num(Y.Get(m, "normalization"), 0.76));
				if (vol > 255) vol = 255;
				pak.Add(0x0300 + types[type], ResType.Music, ss.data, rr.Frames.Count, ss.maxWrites, (vol << 8) | (rr.Loop ? 1 : 0));
				var ay = MusicAy.Encode(rr.Voices, rr.Patches, rr.Loop);
				payk.Add(0x0300 + types[type], ResType.Music, ay.data, rr.Voices.Count, ay.maxWrites, (vol << 8) | (rr.Loop ? 1 : 0));
				ayBytes += ay.data.Length; ayMax = Math.Max(ayMax, ay.maxWrites);
				var fm = MusicFm.Encode(rr.Voices, rr.Patches, rr.Loop);
				pfm.Add(0x0300 + types[type], ResType.Music, fm.data, rr.Voices.Count, fm.maxWrites, (vol << 8) | (rr.Loop ? 1 : 0));
				fmBytes += fm.data.Length; fmMax = Math.Max(fmMax, fm.maxWrites);
				if (rr.Loop) nloop++; else nonce++;
				n++; bytes += ss.data.Length; maxw = Math.Max(maxw, ss.maxWrites);
			}
			var have = new HashSet<int>();
			foreach (var m in rules) { string t = Y.Str(Y.Get(m, "type")); if (Y.Has(m, "catPos") && t != null && types.ContainsKey(t)) have.Add(types[t]); }
			BuildMusGroups(types, have);
			var res = pak.Write(Path.Combine(OutDir, "MUSIC.PAK"));
			var resAy = payk.Write(Path.Combine(OutDir, "MUSICAY.PAK"));
			var resFm = pfm.Write(Path.Combine(OutDir, "MUSICFM.PAK"));
			report.Add($"MUSICAY.PAK: сведено на 3 канала AY, {Kb(ayBytes)} KB потоков, max {ayMax} writes/frame ({Kb(resAy.bytes)} KB)");
			report.Add($"MUSICFM.PAK: сведено на 6 FM-каналов 2 x YM2203, {Kb(fmBytes)} KB потоков, max {fmMax} writes/frame ({Kb(resFm.bytes)} KB)");
			report.Add($"MUSIC.PAK: {n} music types ({nloop} looped, {nonce} one-shot), {cache.Count} tracks, {secs / 60:0.0} min, streams {Kb(bytes)} KB, max {maxw} writes/frame ({Kb(res.bytes)} KB)");
		}

		// Состав музыкальных групп и роли экранов (22 §1.3). Группы — по правилу оригинала
		// (имя типа содержит имя группы как подстроку, Mod.cpp:504), роли — из правил, а не
		// зашиты в движок. Формат: u8 n_grp, n_grp x {first, count}, u8 n_kind, n_kind x тип,
		// затем пул номеров типов; #FF — «темы нет, оставить играть текущее».
		static readonly string[] MusGroups = { "GMGEO", "GMINTER" };
		byte[] musGrpTable;

		void BuildMusGroups(Dictionary<string, int> types, HashSet<int> have)
		{
			string Iface(string screen)
			{
				var list = Y.List(Y.Get(Y.Load(ox.ReadText($"standard/{RuleFolder}/interfaces.rul")), "interfaces")) ?? new List<object>();
				foreach (var o in list)
					if (Y.Str(Y.Get(o, "type")) == screen) return Y.Str(Y.Get(o, "music"));
				return null;
			}
			string Vars(string key)
			{
				if (!ox.Exists($"standard/{RuleFolder}/vars.rul")) return null;
				return Y.Str(Y.Get(Y.Get(Y.Load(ox.ReadText($"standard/{RuleFolder}/vars.rul")), "constants"), key));   // ключи лежат под constants
			}
			string Cutscene(string type)
			{
				var list = Y.List(Y.Get(Y.Load(ox.ReadText($"standard/{RuleFolder}/cutscenes.rul")), "cutscenes")) ?? new List<object>();
				foreach (var o in list)
					if (Y.Str(Y.Get(o, "type")) == type) return Y.Str(Y.Get(Y.Get(o, "slideshow"), "musicId"));
				return null;
			}
			byte Type(string name)
			{
				if (name == null || !types.TryGetValue(name, out var t) || !have.Contains(t)) return 0xFF;
				return (byte)t;
			}
			var pool = new List<byte>();
			var wins = new List<byte[]>();
			foreach (var grp in MusGroups)
			{
				int first = pool.Count;
				foreach (var kv in types)
					if (kv.Key.Contains(grp) && have.Contains(kv.Value)) pool.Add((byte)kv.Value);
				wins.Add(new[] { (byte)first, (byte)(pool.Count - first) });
			}
			var kinds = new[]
			{
				Type(Iface("mainMenu")), Type(Iface("soldierMemorial")),
				Type(Vars("goodDebriefingMusic") ?? "GMMARS"), Type(Vars("badDebriefingMusic") ?? "GMMARS"),
				Type("GMTACTIC"), Type("GMDEFEND"),
				Type(Cutscene("loseGame")), Type(Cutscene("winGame")),
			};
			var o2 = new List<byte> { (byte)wins.Count };
			foreach (var w in wins) o2.AddRange(w);
			o2.Add((byte)kinds.Length);
			o2.AddRange(kinds);
			o2.AddRange(pool);
			musGrpTable = o2.ToArray();
			var names = new List<string>();
			for (int i = 0; i < MusGroups.Length; i++) names.Add($"{MusGroups[i]} {wins[i][1]}");
			report.Add($"  music groups: {string.Join(", ", names)}; roles " + string.Join(",", kinds.Select(k => k == 0xFF ? "-" : k.ToString())));
		}

		// ------------------------------------------------------------ заставки (CUTS.PAK)
		// cutscenes.rul (CutsceneState / SlideshowState): слайд-шоу intro, winGame, loseGame,
		// tleth. Картинки LBM (IMG8 320x200) — CUTS.PAK на SD; палитры слайдов и таблица
		// CUTSCENES — в RULES.PAK (Rules()). Таблица: 4 x u16 смещение записи (0 — нет) в
		// порядке CutTypes; запись: u8 слайдов, u8 музыка (номер music_types или FF), u8 секунд
		// по умолчанию, u8 0, затем слайды по 20 байт: u16 картинка, u16 палитра, u16 строка
		// подписи (FFFF — нет), i16 x, y, w, h, u8 цвет, u8 выравнивание (0 влево, 1 центр,
		// 2 вправо), u8 секунд (0 — по умолчанию), u8 0. Видео (FLI/VID) не переносятся.
		static readonly string[] CutTypes = { "intro", "winGame", "loseGame", "tleth" };
		byte[] cutTable;
		readonly List<(int id, byte[] cram)> cutPals = new List<(int, byte[])>();

		void Cutscenes()
		{
			var doc = Y.List(Y.Get(Y.Load(ox.ReadText($"standard/{RuleFolder}/cutscenes.rul")), "cutscenes")) ?? new List<object>();
			var mus = Registry.Index(Registry.Load("music_types.txt"));
			var pak = new PakWriter(Game);
			var tab = new List<byte>(new byte[CutTypes.Length * 2]);
			int nImg = 0;
			foreach (var o in doc)
			{
				var m = Y.Map(o);
				int ti = Array.IndexOf(CutTypes, Y.Str(Y.Get(m, "type")));
				var show = Y.Map(Y.Get(m, "slideshow"));
				if (ti < 0 || show == null) continue;
				var slides = Y.List(Y.Get(show, "slides")) ?? new List<object>();
				var rec = new List<byte>();
				string music = Y.Str(Y.Get(show, "musicId"));
				rec.Add(0);
				rec.Add((byte)(music != null && mus.TryGetValue(music, out var mi) ? mi : 0xFF));
				rec.Add((byte)Y.Int(Y.Get(show, "transitionSeconds"), 30));
				rec.Add(0);
				int n = 0;
				foreach (var so in slides)
				{
					var s = Y.Map(so);
					string path = Y.Str(Y.Get(s, "imagePath"));
					if (path == null || !gfs.Has(path)) continue;
					var (img, pal) = Formats.DecodeLbm(gfs.Read(path));
					if (img.W != 320 || img.H != 200) continue;
					string key = "CUT:" + path.ToUpperInvariant();
					int imgId = ids.Id(key), palId = ids.Id(key + ":PAL");
					if (!cutPals.Exists(p => p.id == palId))
					{
						pak.Add(imgId, ResType.Img8, Sprites.EncodeImg8(img), img.W, img.H);
						cutPals.Add((palId, Cram.Pal6ToCram(Cram.Pal8To6(pal))));
						nImg++;
						if (prevDir != null) File.WriteAllBytes(Path.Combine(prevDir, "CUT_" + Path.GetFileNameWithoutExtension(path) + ".png"), Png.EncodeIndexed(img, pal));
					}
					string cap = Y.Str(Y.Get(s, "caption"));
					int str = cap != null && strIndex.TryGetValue(cap, out var si) ? si : 0xFFFF;
					var pos = Y.List(Y.Get(s, "captionPos")); var size = Y.List(Y.Get(s, "captionSize"));
					int x = pos != null ? Y.Int(pos[0], 0) : 0, y = pos != null ? Y.Int(pos[1], 0) : 0;
					int w = size != null ? Y.Int(size[0], 320) : 320, h = size != null ? Y.Int(size[1], 200) : 200;
					void W16(int v) { rec.Add((byte)v); rec.Add((byte)(v >> 8)); }
					W16(imgId); W16(palId); W16(str); W16(x); W16(y); W16(w); W16(h);
					rec.Add((byte)Y.Int(Y.Get(s, "captionColor"), 0));
					rec.Add((byte)Y.Int(Y.Get(s, "captionAlign"), 0));
					rec.Add((byte)Y.Int(Y.Get(s, "transitionSeconds"), 0));
					rec.Add(0);
					n++;
				}
				if (n == 0) continue;
				rec[0] = (byte)n;
				int off = tab.Count;
				tab[ti * 2] = (byte)off; tab[ti * 2 + 1] = (byte)(off >> 8);
				tab.AddRange(rec);
			}
			cutTable = tab.ToArray();
			var r = pak.Write(Path.Combine(OutDir, "CUTS.PAK"));
			report.Add($"CUTS.PAK: {nImg} slides, {Kb(r.bytes)} KB; palettes and CUTSCENES table ({cutTable.Length} B) -> RULES.PAK");
		}

		// ------------------------------------------------------------ строки и шрифты (LANG.PAK)
		void Lang()
		{
			var strings = Strings.Load(ox, RuleFolder, lang);
			var cs = new Charset();
			var table = Strings.Build(strings, cs);
			var nameWarn = new List<string>();
			var names = Names.Build(ox, cs, nameWarn);   // до cs.Glyphs(): буквы имён — в шрифты
			var glyphs = cs.Glyphs();
			var fonts = Fonts.Load(ox);
			var pak = new PakWriter(Game);
			pak.Add(ids.Id("STRINGS"), ResType.Str, table.Data, table.Count, table.EngineCount);
			var csBuf = new byte[cs.Extra.Count * 2];
			for (int i = 0; i < cs.Extra.Count; i++) { csBuf[i * 2] = (byte)cs.Extra[i]; csBuf[i * 2 + 1] = (byte)(cs.Extra[i] >> 8); }
			pak.Add(ids.Id("CHARSET"), ResType.Blob, csBuf, cs.Extra.Count);
			pak.Add(ids.Id("NAMES"), ResType.Blob, names.data, names.pools, names.names);
			var fr = new List<string>();
			foreach (var id in new[] { "FONT_BIG", "FONT_SMALL", "FONT_GEO_BIG", "FONT_GEO_SMALL" })
			{
				var f = fonts[id];
				var (data, missing) = Fonts.Encode(f, glyphs);
				pak.Add(ids.Id(id), ResType.Font, data, glyphs.Count, f.W, f.H);
				fr.Add($"{id} {f.W}x{f.H} {data.Length / 1024.0:0.0} KB" + (missing > 0 ? $", no glyph: {missing}" : ""));
				if (prevDir != null) PreviewText(f, $"{id}: X-COM TERROR FROM THE DEEP 0123456789 abc xyz !?.,:");
			}
			var r = pak.Write(Path.Combine(OutDir, "LANG.PAK"));
			report.Add($"LANG.PAK ({lang}): {table.Count} strings ({table.EngineCount} engine, {table.Missing.Count} of them absent), {cs.Extra.Count} non-ASCII chars, {Kb(r.bytes)} KB");
			foreach (var s in fr) report.Add("  " + s);
			report.Add($"  NAMES: {names.pools} pools, {names.names} names, {Kb(names.data.Length)} KB");
			foreach (var w in nameWarn) report.Add("  " + w);
			strIndex = table.Index;
		}

		void PreviewText(Font font, string text)
		{
			int w = 0;
			foreach (var ch in text) { font.Glyphs.TryGetValue(ch, out var g); w += (ch == ' ' ? font.W >> 1 : g != null ? g.W : font.W) + font.Spacing + 1; }
			var img = new Img(w + 4, font.H + 4);
			int x = 2;
			foreach (var ch in text)
			{
				font.Glyphs.TryGetValue(ch, out var g);
				if (ch == ' ' || g == null) { x += (font.W >> 1) + font.Spacing + 1; continue; }
				for (int y = 0; y < g.H; y++)
					for (int xx = 0; xx < g.W; xx++)
					{
						byte v = g.Img.Px[(g.Y + y) * g.Img.W + g.X + xx];
						if (v != 0) img.Px[(y + 2) * img.W + x + xx] = v;
					}
				x += g.W + font.Spacing + 1;
			}
			var pal = new byte[768];
			int[][] cols = { new[] { 0, 0, 32 }, new[] { 255, 255, 255 }, new[] { 200, 200, 200 }, new[] { 150, 150, 150 }, new[] { 100, 100, 255 }, new[] { 60, 60, 180 } };
			for (int i = 0; i < cols.Length; i++) for (int c = 0; c < 3; c++) pal[i * 3 + c] = (byte)cols[i][c];
			File.WriteAllBytes(Path.Combine(prevDir, $"text_{font.Id}.png"), Png.EncodeIndexed(img, pal));
		}

		// ------------------------------------------------------------ правила (RULES.PAK)
		void Rules()
		{
			var pak = new PakWriter(Game);
			var ui = Interfaces.Build(ox, RuleFolder, ids);
			pak.Add(ids.Id("UI"), ResType.Table, ui.data, ui.screens);
			var sections = RuleSet.LoadSections(ox, RuleFolder);
			var schema = RulesSchema.Tables();
			var rs = new RuleSet(sections, schema, strIndex, n => ids.Id(n));
			rs.ResFind = n => ids.Find(n);
			var parts = new List<string>();
			for (int i = 0; i < schema.Count; i++)
			{
				var t = rs.Build(schema[i]);
				pak.Add(RulesSchema.ResBase + i, ResType.Table, t.data, t.count, t.recSize);
				parts.Add($"{schema[i].Name} {t.count}x{t.recSize}");
			}
			if (musGrpTable != null) pak.Add(ids.Id("MUSGRP"), ResType.Table, musGrpTable, MusGroups.Length);
			var det = GlobeDetail.Build(rs, sections, ox, RuleFolder);   // детали глобуса: линии, подписи, города
			pak.Add(ids.Id("GLOBEDET"), ResType.Blob, det.data);
			parts.Add("GLOBEDET: " + det.info);
			var vars = RulesSchema.Vars(sections, rs);
			pak.Add(RulesSchema.ResBase - 1, ResType.Table, vars, vars.Length);
			if (cutTable != null)                         // заставки: таблица и палитры слайдов (Cutscenes)
			{
				pak.Add(ids.Id("CUTSCENES"), ResType.Blob, cutTable, CutTypes.Length);
				foreach (var (id, cram) in cutPals) pak.Add(id, ResType.Pal, cram, 256);
			}
			var r = pak.Write(Path.Combine(OutDir, "RULES.PAK"));
			report.Add($"RULES.PAK: UI {ui.screens} screens" + (ui.unknown.Count > 0 ? ", not in registry: " + string.Join(", ", ui.unknown) : "") + $"; {Kb(r.bytes)} KB");
			report.Add("  " + string.Join(", ", parts));
			if (rs.Warnings.Count > 0)
			{
				File.WriteAllText(Path.Combine(OutDir, "RULEWARN.TXT"), string.Join("\r\n", rs.Warnings) + "\r\n");
				report.Add($"  {rs.Warnings.Count} warnings -> RULEWARN.TXT");
			}
		}
	}
}
