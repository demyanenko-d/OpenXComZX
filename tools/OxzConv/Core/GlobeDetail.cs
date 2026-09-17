// Детали глобуса (ресурс GLOBEDET, RULES.PAK) — Globe::drawDetail OpenXcom (REF/OpenXcom/src/Geoscape/
// Globe.cpp): линии рек и границ (globe.polylines, зум >= 1), подписи стран (countries.labelLon/Lat,
// зум >= 2), города — значок GlobeMarkers 8 и подпись (точечные области missionZones с именем,
// RuleRegion::getCities, зум >= 3). Движок — src/geoscape/earth/globe.s detail (отсев групп и проекция) и
// globe_det.c (вывод); project_docs/globe.md §12.12.
//
// Группы: движок проецирует только центры групп, отсевает их как ячейки карты (_gl_cull: сзади —
// z + sinρ/4 < −64, вне окна — центр ± m) и проецирует вершины видимых. Линия режется на куски до
// GroupPts точек (соседние куски делят точку стыка); подписи стран и городы собираются в группы до
// GroupPts точек радиусом до GroupDeg. Порядок групп — линии, страны, города (порядок вывода
// OpenXcom); внутри — порядок правил.
//
// Формат (little-endian; смещения чётные — вершины копирует DMA):
//   +0  u8 lineColor, countryColor, cityColor, baseColor (globe.rul; без них — Mod.cpp: 162, 239, 138, 133)
//   +4  u8 marker[9] — значок города 3x3 (0 — прозрачно), u8 0
//   +14 u16 nGroup, nVert, смещение кадров меток, 0, 0
//   +24 группы nGroup x 16 байт (как запись ячейки ресурса GLOBE для _gl_cull): u16 первая вершина,
//       u8 вершин, u8 вид (0 — линия, 1 — подписи стран, 2 — города), u16 1, u16 0, центр (6 байт как
//       у вершин), i16 sinρ (Q14)
//   m зумов 0..5: 6 x nGroup x u16 — предел окна центра группы в точках: v + (v >> 3) + 2 + поле подписи,
//       v = (R · sinρ) >> 14 (формула cm_fill движка; поле подписи 52 — рамка 100 x 9 со сдвигом 2)
//   строки: nVert x u16 (номер строки подписи; у точек линий — #FFFF)
//   вершины: nVert x 6 байт — X, Y, Z (Q14, X = cosφ·cosλ, Y = cosφ·sinλ, Z = sinφ, широта минус —
//     север), по 2 байта v & 127, v >> 7 (как ресурс GLOBE)
//   кадры меток: 9 x 9 байт — набор GlobeMarkers 3x3 по строкам (0 — прозрачно; globe_det.c globe_marks)
using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;

namespace OxzConv
{
	static class GlobeDetail
	{
		public const int MaxVert = 340, MaxGroup = 96;      // globe.s detail: рабочая страница; globe_det.c — хвост банка
		const int GroupPts = 12;
		const double GroupDeg = 15;
		static readonly int[] ZR = { 90, 120, 180, 280, 450, 720 };

		struct P { public double X, Y, Z; public int Str; }

		static P Vec(double lonDeg, double latDeg, int str)
		{
			double lon = lonDeg * Math.PI / 180, lat = latDeg * Math.PI / 180;
			return new P { X = Math.Cos(lat) * Math.Cos(lon), Y = Math.Cos(lat) * Math.Sin(lon), Z = Math.Sin(lat), Str = str };
		}

		static void V6(List<byte> o, double x, double y, double z)
		{
			foreach (double c in new[] { x, y, z })
			{
				int v = Math.Max(-16383, Math.Min(16383, (int)Math.Round(c * 16384)));
				o.Add((byte)(v & 127)); o.Add((byte)(v >> 7));
			}
		}

		static void U16(List<byte> o, int v) { o.Add((byte)v); o.Add((byte)(v >> 8)); }

		// Центр (нормированная сумма) и sinρ — синус наибольшего угла до точек
		static (double x, double y, double z, double sinRho) Cap(List<P> ps)
		{
			double x = ps.Sum(p => p.X), y = ps.Sum(p => p.Y), z = ps.Sum(p => p.Z), l = Math.Sqrt(x * x + y * y + z * z);
			x /= l; y /= l; z /= l;
			double minCos = ps.Min(p => p.X * x + p.Y * y + p.Z * z);
			double rho = Math.Acos(Math.Max(-1, Math.Min(1, minCos)));
			return (x, y, z, rho >= Math.PI / 2 ? 1.0 : Math.Sin(rho));
		}

		// Точки подписей -> группы по близости (жадно: первая свободная и ближайшие к ней в пределах GroupDeg)
		static List<List<P>> Cluster(List<P> pts)
		{
			var left = new List<P>(pts);
			var res = new List<List<P>>();
			double cmin = Math.Cos(GroupDeg * Math.PI / 180);
			while (left.Count > 0)
			{
				var a = left[0];
				var g = left.Where(p => p.X * a.X + p.Y * a.Y + p.Z * a.Z >= cmin)
					.OrderByDescending(p => p.X * a.X + p.Y * a.Y + p.Z * a.Z).Take(GroupPts).ToList();
				foreach (var p in g) left.Remove(p);
				// порядок правил внутри группы
				res.Add(pts.Where(g.Contains).ToList());
			}
			return res;
		}

		public static (byte[] data, string info) Build(RuleSet rs, Dictionary<string, object> sections, OxcomData ox, string folder)
		{
			sections.TryGetValue("globe", out var g);
			int Col(string k, int def) { var v = Y.Get(g, k); return v == null ? def : (int)Y.Num(v); }
			// TFTD: линии — реки, синие (92: 52, 124, 208) вместо чёрного lineColor 111 — просьба пользователя;
			// UFO — границы стран, цвет оригинала
			int line = folder == "xcom2" ? 92 : Col("lineColor", 162);
			var head = new List<byte> { (byte)line, (byte)Col("countryColor", 239), (byte)Col("cityColor", 138), (byte)Col("baseColor", 133) };

			// значок города — кадр 8 набора GlobeMarkers (extraSprites.rul: 3x3, файл UI/globe_*.png)
			string png = null;
			if (sections.TryGetValue("extraSprites", out var es))
				foreach (var e in Y.List(es) ?? new List<object>())
					if (Y.Str(Y.Get(e, "type")) == "GlobeMarkers")
						foreach (var kv in Y.Map(Y.Get(e, "files")) ?? new Dictionary<object, object>()) png = Y.Str(kv.Value);
			if (png == null) throw new InvalidDataException("GLOBEDET: GlobeMarkers not found in extraSprites");
			var img = Png.DecodeIndexed(ox.ReadBytes($"standard/{folder}/{png}"));
			for (int y = 0; y < 3; y++) for (int x = 0; x < 3; x++) head.Add(img.Px[y * img.W + 24 + x]);
			head.Add(0);
			var frames = new List<byte>();
			for (int fr = 0; fr < 9; fr++) for (int y = 0; y < 3; y++) for (int x = 0; x < 3; x++) frames.Add(img.Px[y * img.W + fr * 3 + x]);

			var groups = new List<(List<P> pts, int kind)>();
			int nLines = 0;
			foreach (var pl in Y.List(Y.Get(g, "polylines")) ?? new List<object>())
			{
				var v = Y.List(pl);
				int n = v.Count / 2;
				if (n < 2) continue;
				nLines++;
				var pts = Enumerable.Range(0, n).Select(i => Vec(Y.Num(v[2 * i]), Y.Num(v[2 * i + 1]), 0xFFFF)).ToList();
				for (int s = 0; s < n - 1; s += GroupPts - 1)
					groups.Add((pts.Skip(s).Take(GroupPts).ToList(), 0));
			}
			var countries = rs.Entries["countries"].Select(c => Vec(Y.Num(Y.Get(c.Value, "labelLon")), Y.Num(Y.Get(c.Value, "labelLat")),
				rs.Str(Y.Get(c.Value, "type") ?? c.Key))).ToList();
			foreach (var c in Cluster(countries)) groups.Add((c, 1));
			var cities = new List<P>();
			foreach (var reg in rs.Entries["regions"])
				foreach (var zone in Y.List(Y.Get(reg.Value, "missionZones")) ?? new List<object>())
					foreach (var a in Y.List(zone) ?? new List<object>())
					{
						var v = Y.List(a);
						if (v == null || v.Count < 6 || string.IsNullOrEmpty(Y.Str(v[5]))) continue;
						if (Y.Num(v[0]) != Y.Num(v[1]) || Y.Num(v[2]) != Y.Num(v[3])) continue;   // MissionArea::isPoint
						cities.Add(Vec(Y.Num(v[0]), Y.Num(v[2]), rs.Str(v[5])));
					}
			foreach (var c in Cluster(cities)) groups.Add((c, 2));

			int nVert = groups.Sum(x => x.pts.Count);
			if (nVert > MaxVert) throw new InvalidDataException($"GLOBEDET: {nVert} vertices > {MaxVert}");
			if (groups.Count > MaxGroup) throw new InvalidDataException($"GLOBEDET: {groups.Count} groups > {MaxGroup}");
			int fOff = 24 + groups.Count * 28 + nVert * 8;
			U16(head, groups.Count); U16(head, nVert); U16(head, fOff); U16(head, 0); U16(head, 0);
			var recs = new List<byte>(); var ms = new List<byte>(); var strs = new List<byte>(); var verts = new List<byte>();
			var caps = groups.Select(x => Cap(x.pts)).ToList();
			int first = 0;
			for (int i = 0; i < groups.Count; i++)
			{
				var (pts, kind) = groups[i];
				U16(recs, first); recs.Add((byte)pts.Count); recs.Add((byte)kind); U16(recs, 1); U16(recs, 0);
				V6(recs, caps[i].x, caps[i].y, caps[i].z);
				U16(recs, (int)Math.Round(caps[i].sinRho * 16384));
				foreach (var p in pts) { U16(strs, p.Str); V6(verts, p.X, p.Y, p.Z); }
				first += pts.Count;
			}
			for (int z = 0; z < 6; z++)
				for (int i = 0; i < groups.Count; i++)
				{
					int sr = (int)Math.Round(caps[i].sinRho * 16384);
					int v = (ZR[z] * sr) >> 14;
					U16(ms, v + (v >> 3) + 2 + (groups[i].kind == 0 ? 0 : 52));
				}
			var o = new List<byte>(head);
			o.AddRange(recs); o.AddRange(ms); o.AddRange(strs); o.AddRange(verts);
			if (o.Count != fOff) throw new InvalidDataException("GLOBEDET: layout");
			o.AddRange(frames);
			return (o.ToArray(), $"{nLines} lines, {countries.Count} country labels, {cities.Count} cities -> {groups.Count} groups, {nVert} points, {o.Count} B");
		}
	}
}
