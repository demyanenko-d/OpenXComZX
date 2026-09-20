// Реестр номеров ресурсов, общий для UFO и TFTD.
// Статические номера — ресурсы, которые код движка запрашивает по имени (как
// getSurface/getSurfaceSet/getPalette в OpenXcom); из них генерируется
// res_ids.h. Номера не менять — только добавлять. Динамические (от DynBase) —
// остальные ресурсы; движок получает их только через таблицы правил.
using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;

namespace OxzConv
{
	class ResIds
	{
		public static readonly List<KeyValuePair<string, int>> Static = new List<KeyValuePair<string, int>>();
		static void S(string name, int id) => Static.Add(new KeyValuePair<string, int>(name, id));

		static ResIds()
		{
			// палитры
			S("PAL_GEOSCAPE", 0x0001); S("PAL_BASESCAPE", 0x0002); S("PAL_GRAPHS", 0x0003);
			S("PAL_UFOPAEDIA", 0x0004); S("PAL_BATTLEPEDIA", 0x0005);
			S("PAL_BATTLESCAPE", 0x0006); S("PAL_BATTLESCAPE_1", 0x0007); S("PAL_BATTLESCAPE_2", 0x0008); S("PAL_BATTLESCAPE_3", 0x0009);
			S("BACKPALS", 0x000A);
			// шрифты и строки (LANG.PAK)
			S("FONT_BIG", 0x0010); S("FONT_SMALL", 0x0011); S("FONT_GEO_BIG", 0x0012); S("FONT_GEO_SMALL", 0x0013);
			S("STRINGS", 0x0020); S("CHARSET", 0x0021); S("NAMES", 0x0022);
			// таблицы правил (RULES.PAK)
			S("UI", 0x0030);
			S("CUTSCENES", 0x0031);   // заставки: слайды (CUTS.PAK на SD), палитры и подписи
			S("GLOBEDET", 0x0032);    // детали глобуса: линии, подписи стран, города (GlobeDetail.cs)
			S("MUSGRP", 0x0033);      // музыка: состав групп (GMGEO, GMINTER) и роли экранов (22 §1.3)
			// полноэкранные картинки геоскейпа и баз
			for (int i = 1; i <= 17; i++) S($"BACK{i:00}.SCR", 0x0100 + i);
			S("GEOBORD.SCR", 0x0120); S("UP_BORD2.SCR", 0x0121); S("GRAPHS.SPK", 0x0122); S("GRAPH.BDY", 0x0123);
			S("INTERWIN.DAT", 0x0124);
			// наборы спрайтов геоскейпа и баз
			S("BASEBITS.PCK", 0x0140); S("INTICON.PCK", 0x0141); S("TEXTURE.DAT", 0x0142); S("SCANG.DAT", 0x0143);
			S("WORLDMAP", 0x0144);   // маска полигонов WORLD.DAT (insideLand, текстура места высадки)
			S("GLOBE", 0x0145);      // геометрия глобуса: многоугольники WORLD.DAT по ячейкам 30° (Globe.cs)
			// интерфейс боя
			S("TAC00.SCR", 0x0180); S("TAC01.SCR", 0x0181); S("ICONS.PCK", 0x0182); S("DETBORD.PCK", 0x0183);
			S("DETBORD2.PCK", 0x0184); S("MEDIBORD.PCK", 0x0185); S("SCANBORD.PCK", 0x0186); S("UNIBORD.PCK", 0x0187);
			S("CURSOR.PCK", 0x01A0); S("SMOKE.PCK", 0x01A1); S("HIT.PCK", 0x01A2); S("X1.PCK", 0x01A3);
			S("MEDIBITS.DAT", 0x01A4); S("DETBLOB.DAT", 0x01A5); S("SPICONS.DAT", 0x01A6);
			S("BIGOBS.PCK", 0x01A7); S("FLOOROB.PCK", 0x01A8); S("HANDOB.PCK", 0x01A9); S("HANDOB2.PCK", 0x01AA);
			S("BREATH-1.PCK", 0x01AB);
			// карты боя (BATTLE.PAK): готовые поля и их тайлсеты — пока нет генератора карт (Core/Battle.cs)
			S("BATMAP0", 0x01C0); S("BATMAP1", 0x01C1); S("BATMAP2", 0x01C2);
			S("BATTILE0", 0x01C8); S("BATTILE1", 0x01C9); S("BATTILE2", 0x01CA);
			// генератор миссий (16 §3): таблицы террейнов и скриптов; наборы, блоки и тайлсеты
			// получают динамические номера по именам MCDSET_/MAPBLK_/TILESET_
			S("TERRAINS", 0x01D0); S("MAPSCRIPTS", 0x01D1); S("DEPLOYS", 0x01D2);
			// листы спрайтов брони X-COM: их движок грузит по имени сам (остальные — по правилам)
			S("UNIT_XCOM_0", 0x01D8); S("UNIT_TDXCOM_0", 0x01D9);
			// строить после заполнения: инициализаторы полей выполняются раньше тела конструктора
			staticMap = Static.ToDictionary(kv => kv.Key.ToUpperInvariant(), kv => kv.Value);
		}

		public const int DynBase = 0x1000;
		static readonly Dictionary<string, int> staticMap;

		int next = DynBase;
		public readonly List<KeyValuePair<string, int>> Dyn = new List<KeyValuePair<string, int>>();
		readonly Dictionary<string, int> dynMap = new Dictionary<string, int>(StringComparer.Ordinal);

		// Номер уже известного ресурса или -1 (без выделения нового)
		public int Find(string name)
		{
			string key = name.ToUpperInvariant();
			if (staticMap.TryGetValue(key, out var s)) return s;
			return dynMap.TryGetValue(key, out var d) ? d : -1;
		}

		public int Id(string name)
		{
			string key = name.ToUpperInvariant();
			if (staticMap.TryGetValue(key, out var s)) return s;
			if (!dynMap.TryGetValue(key, out var d))
			{
				d = next++;
				dynMap[key] = d;
				Dyn.Add(new KeyValuePair<string, int>(key, d));
			}
			return d;
		}

		public static string StaticHeader()
		{
			var sb = new StringBuilder();
			sb.Append("// Сгенерировано OxzConv (Core/ResIds.cs) — не править вручную.\n");
			sb.Append("// Номера ресурсов, которые движок запрашивает по имени (общие для UFO и TFTD).\n");
			sb.Append("#ifndef RES_IDS_H\n#define RES_IDS_H\n\n");
			foreach (var kv in Static)
			{
				string c = "RES_" + new string(kv.Key.Select(ch => char.IsLetterOrDigit(ch) && ch < 128 ? char.ToUpperInvariant(ch) : '_').ToArray());
				sb.Append($"#define {c.PadRight(28)} 0x{kv.Value:X4}  // {kv.Key}\n");
			}
			sb.Append($"\n#define RES_DYN_BASE                 0x{DynBase:X}\n\n#endif\n");
			return sb.ToString();
		}
	}

	// Реестры имён с фиксированными номерами (Data/*.txt): номер = строка без комментариев.
	static class Registry
	{
		public static List<string> Load(string file) =>
			OxcomData.ReadDataText(file).Split('\n').Select(s => s.Trim()).Where(s => s.Length > 0 && !s.StartsWith("#", StringComparison.Ordinal)).ToList();

		public static Dictionary<string, int> Index(List<string> list)
		{
			var d = new Dictionary<string, int>(StringComparer.Ordinal);
			for (int i = 0; i < list.Count; i++) d[list[i]] = i;
			return d;
		}
	}
}
