// Маска полигонов глобуса (ресурс WORLDMAP, GEO.PAK) из GEODATA/WORLD.DAT — для
// insideLand и текстуры места высадки без тригонометрии на Z80 (14_todo §3.17, §4).
//
// Проверка точки — точный порт Globe::getPolygonFromLonLat OpenXcom (первый полигон,
// в котором лежит точка; отбрасываются полигоны с вершиной дальше ~41°). В UFO
// полигоны — суша, в TFTD — вода (фон диска — суша), поэтому «внутри полигона» =
// insideLand OpenXcom, что бы оно ни значило в игре.
//
// Формат (little-endian):
//   u16 nMixed
//   u8  cells[180 * 180]: растр 1° — 180 строк широты (строка 0 — от -90°, север,
//       как знак широты OpenXcom), 360 столбцов долготы от 0°; ниббл на клетку
//       (чётный столбец — младший): 0..13 — текстура полигона во всей клетке,
//       14 — клетка на границе, 15 — вне полигонов;
//   u16 rowStart[180]: граничных клеток до строки;
//   {u8 texture; u8 bits[8]} x nMixed: подрастр 1/8° граничной клетки по порядку
//       (строки 1/8° с севера, бит i — столбец i с запада; 1 — внутри полигона),
//       texture — самая частая текстура внутри.
using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;

namespace OxzConv
{
	static class WorldMap
	{
		class Poly { public double[] Lon, Lat; public int N, Texture; public double LonMin, LonMax, LatMin, LatMax; }

		static double Xcom2Rad(int v) => v * 0.125 * Math.PI / 180.0;

		static List<Poly> Load(byte[] dat)
		{
			var list = new List<Poly>();
			for (int o = 0; o + 20 <= dat.Length; o += 20)
			{
				var v = new short[10];
				for (int i = 0; i < 10; i++) v[i] = BitConverter.ToInt16(dat, o + i * 2);
				int n = v[6] != -1 ? 4 : 3;
				var p = new Poly { N = n, Lon = new double[n], Lat = new double[n], Texture = v[8] };
				for (int i = 0; i < n; i++) { p.Lon[i] = Xcom2Rad(v[i * 2]); p.Lat[i] = Xcom2Rad(v[i * 2 + 1]); }
				// рамка в градусах: долготы — относительно первой вершины (шов 0/360)
				double l0 = p.Lon[0] * 180 / Math.PI;
				double rmin = 0, rmax = 0;
				for (int i = 0; i < n; i++)
				{
					double r = ((p.Lon[i] * 180 / Math.PI - l0) % 360 + 540) % 360 - 180;
					rmin = Math.Min(rmin, r); rmax = Math.Max(rmax, r);
				}
				p.LonMin = l0 + rmin; p.LonMax = l0 + rmax;
				p.LatMin = p.Lat.Min() * 180 / Math.PI; p.LatMax = p.Lat.Max() * 180 / Math.PI;
				list.Add(p);
			}
			return list;
		}

		// Globe::getPolygonFromLonLat: текстура полигона или -1
		static int PolyAt(List<Poly> polys, double lon, double lat)
		{
			const double zDiscard = 0.75;
			double coslat = Math.Cos(lat), sinlat = Math.Sin(lat);
			foreach (var p in polys)
			{
				double z = 0;
				for (int j = 0; j < p.N; ++j)
				{
					z = coslat * Math.Cos(p.Lat[j]) * Math.Cos(p.Lon[j] - lon) + sinlat * Math.Sin(p.Lat[j]);
					if (z < zDiscard) break;
				}
				if (z < zDiscard) continue;
				bool odd = false;
				double clat = p.Lat[0], clon = p.Lon[0];
				double x = Math.Cos(clat) * Math.Sin(clon - lon);
				double y = coslat * Math.Sin(clat) - sinlat * Math.Cos(clat) * Math.Cos(clon - lon);
				for (int j = 0; j < p.N; ++j)
				{
					int k = (j + 1) % p.N;
					clat = p.Lat[k]; clon = p.Lon[k];
					double x2 = Math.Cos(clat) * Math.Sin(clon - lon);
					double y2 = coslat * Math.Sin(clat) - sinlat * Math.Cos(clat) * Math.Cos(clon - lon);
					if (((y > 0) != (y2 > 0)) && (0 < (x2 - x) * (0 - y) / (y2 - y) + x)) odd = !odd;
					x = x2; y = y2;
				}
				if (odd) return p.Texture;
			}
			return -1;
		}

		public static (byte[] data, int mixed) Build(byte[] dat)
		{
			var polys = Load(dat);
			// кандидаты клетки: полигоны, чья рамка (+1°) задевает клетку; порядок — как в файле
			var cand = new List<int>[180, 360];
			for (int pi = 0; pi < polys.Count; pi++)
			{
				var p = polys[pi];
				int r0 = Math.Max(0, (int)Math.Floor(p.LatMin + 90) - 1), r1 = Math.Min(179, (int)Math.Floor(p.LatMax + 90) + 1);
				int c0 = (int)Math.Floor(p.LonMin) - 1, c1 = (int)Math.Floor(p.LonMax) + 1;
				if (p.LatMin < -80 || p.LatMax > 80 || c1 - c0 > 180) { c0 = 0; c1 = 359; }   // у полюса — вся полоса
				for (int r = r0; r <= r1; r++)
					for (int c = c0; c <= c1; c++)
					{
						int cc = ((c % 360) + 360) % 360;
						(cand[r, cc] ?? (cand[r, cc] = new List<int>())).Add(pi);
					}
			}
			var cells = new byte[180 * 180];
			var rowStart = new int[180];
			var recs = new List<byte>();
			int mixed = 0;
			for (int r = 0; r < 180; r++)
			{
				rowStart[r] = mixed;
				for (int c = 0; c < 360; c++)
				{
					int val = 15;
					var cl = cand[r, c];
					if (cl != null)
					{
						var sub = cl.Distinct().OrderBy(i => i).Select(i => polys[i]).ToList();
						var tex = new int[64];
						for (int j = 0; j < 8; j++)
							for (int i = 0; i < 8; i++)
							{
								double lon = (c + (i + 0.5) / 8.0) * Math.PI / 180, lat = (r - 90 + (j + 0.5) / 8.0) * Math.PI / 180;
								tex[j * 8 + i] = PolyAt(sub, lon, lat);
							}
						if (tex.All(t => t == tex[0])) val = tex[0] < 0 ? 15 : Math.Min(tex[0], 13);
						else
						{
							val = 14;
							mixed++;
							var inside = tex.Where(t => t >= 0).GroupBy(t => t).OrderByDescending(g => g.Count()).ThenBy(g => g.Key).First().Key;
							recs.Add((byte)inside);
							for (int j = 0; j < 8; j++)
							{
								int b = 0;
								for (int i = 0; i < 8; i++) if (tex[j * 8 + i] >= 0) b |= 1 << i;
								recs.Add((byte)b);
							}
						}
					}
					int idx = r * 180 + c / 2;
					cells[idx] |= (byte)((c & 1) != 0 ? val << 4 : val);
				}
			}
			var o = new MemoryStream();
			o.WriteByte((byte)mixed); o.WriteByte((byte)(mixed >> 8));
			o.Write(cells, 0, cells.Length);
			foreach (var s in rowStart) { o.WriteByte((byte)s); o.WriteByte((byte)(s >> 8)); }
			o.Write(recs.ToArray(), 0, recs.Count);
			return (o.ToArray(), mixed);
		}
	}
}
