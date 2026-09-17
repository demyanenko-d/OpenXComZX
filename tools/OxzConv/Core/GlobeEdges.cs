// Виды глобуса «рёбрами» (GVIEW.PAK, сигнатура 'GVE1') — project_docs/globe.md §12.11, модель —
// tools/globe_edges.js. Вместо готовых отрезков строк вид хранит куски границ карты: старт X в
// первой строке (пары, 8.8), наклон dX (пары на строку, 8.8), длина в строках, текстура справа.
// Пересечения дуги со строками — те же, что у точного растеризатора путь А (GlobeViews.View);
// кусок подгоняется так, что пара (X + 192) >> 8 в каждой его строке совпадает с парой границы
// путь А, поэтому картинка движка — та же. Куски связаны цепочками (продолжение — поправка X),
// у начала цепочки — место в упорядоченном списке активных кусков строки: движку не нужно
// сравнивать и сортировать. Текстура левого края строки — список «с какой строки какая».
//
// Контейнер — как GlobeViews (сектор 0 — заголовок, таблица секторов зума, вид с начала сектора),
// сигнатура 'GVE1'. Вид (little-endian):
//   u8 nSeed; nSeed x { u8 строка, u8 текстура } — текстура левого края с этой строки (до
//     следующей); строки без диска не описываются
//   группы начал: { u8 шаг строки (первая — от строки 0), u8 число начал; начала:
//     u16 X0 + 64·256, u8 место в списке активных, кусок }; { 0, 0 } — конец; шаг > 255 — { 255, 0 }
//   кусок: u8 флаги: бит 7 — есть продолжение, бит 6 — длинная запись, биты 3..0 — текстура справа;
//     короткая: u8 (длина − 1) << 4 | dX >> 8 & 15, u8 dX & 255 (dX — 12 бит со знаком, длина ≤ 16)
//     длинная: u8 длина − 1, i16 dX
//   продолжение (после куска с битом 7): i8 поправка X (−128 — дальше i16), кусок
// Движок на строку: вставить начала по местам, пройти список (пара границы — (X + 192) >> 8 − 64,
// прижать к диску строки, отрезок до неё текстурой прошлого куска, текстура — справа от куска),
// X += dX, конец куска — продолжение или удаление.
using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Threading.Tasks;

namespace OxzConv
{
	static class GlobeEdges
	{
		static readonly int[] ZR = { 90, 120, 180, 280, 450, 720 };
		const int H = 200, Sector = 512, Bias = 192, MaxView = 0x1C00, EvMax = 64;   // EvMax — EV_MAX движка

		class Piece
		{
			public int R0, Rows, X0, DX, Pos = -1;
			public byte Tex;
			public double[] Xs;                       // точный X в строках куска (8.8 пар)
			public int[] Pair;                        // пара границы путь А
			public string KTop, KBot;                 // вершина карты сверху / снизу (null — нет)
			public Piece Next, Prev;
		}

		struct Cross { public int Y; public double X, T; public bool Down, Vis; public byte Right, Left; }

		static string Key(GlobeArcs a, int i, bool b) =>
			b ? $"{Math.Round(a.Bx[i] * 1e9)},{Math.Round(a.By[i] * 1e9)},{Math.Round(a.Bz[i] * 1e9)}"
			  : $"{Math.Round(a.Ax[i] * 1e9)},{Math.Round(a.Ay[i] * 1e9)},{Math.Round(a.Az[i] * 1e9)}";

		// Один вид: куски, цепочки, места, упаковка. seedsOut — текстура левого края строк (−1 — нет диска)
		static byte[] View(GlobeArcs a, int zoom, double lonDeg, double tiltDeg, out int nPieces)
		{
			int R = ZR[zoom];
			double l0 = lonDeg * Math.PI / 180, C = tiltDeg * Math.PI / 180;
			double cl = Math.Cos(l0), sl = Math.Sin(l0), cc = Math.Cos(C), sc = Math.Sin(C);
			double exx = -sl, exy = cl, exz = 0, eyx = -sc * cl, eyy = -sc * sl, eyz = cc, ezx = cc * cl, ezy = cc * sl, ezz = sc;
			int n = a.N;
			var cross = new List<Cross>[n];
			var seed = new int[H];
			var leftmost = new double[H];
			for (int y = 0; y < H; y++) { seed[y] = -1; leftmost[y] = double.MaxValue; }
			// пересечения дуг со строками — как GlobeViews.View
			var vA = new double[n]; var dA = new double[n]; var m2 = new double[n];
			var cph = new double[n]; var sph = new double[n]; var ny = new double[n];
			var aex = new double[n]; var aez = new double[n]; var mex = new double[n]; var mez = new double[n];
			var rows = new List<int>[H];
			for (int i = 0; i < n; i++)
			{
				double va = eyx * a.Ax[i] + eyy * a.Ay[i] + eyz * a.Az[i];
				double vb = eyx * a.Bx[i] + eyy * a.By[i] + eyz * a.Bz[i];
				double da = eyx * a.Mx[i] + eyy * a.My[i] + eyz * a.Mz[i];
				double db = da * a.Ct[i] - va * a.St[i];
				double lo = Math.Min(va, vb), hi = Math.Max(va, vb);
				double m = Math.Sqrt(va * va + da * da);
				if (da > 0 && db < 0) hi = m; else if (da < 0 && db > 0) lo = -m;
				int y0 = (int)Math.Ceiling(99.5 + R * lo), y1 = (int)Math.Floor(99.5 + R * hi);
				if (y0 < 0) y0 = 0;
				if (y1 > H - 1) y1 = H - 1;
				if (y1 < y0) continue;
				double nxi = a.Nx[i] * exx + a.Ny[i] * exy + a.Nz[i] * exz;
				double nyi = a.Nx[i] * eyx + a.Ny[i] * eyy + a.Nz[i] * eyz;
				double nzi = a.Nx[i] * ezx + a.Ny[i] * ezy + a.Nz[i] * ezz;
				double mm = Math.Sqrt(nzi * nzi + nxi * nxi);
				if (mm < 1e-12) continue;
				vA[i] = va; dA[i] = da; ny[i] = nyi; m2[i] = mm; cph[i] = nzi / mm; sph[i] = nxi / mm;
				aex[i] = a.Ax[i] * exx + a.Ay[i] * exy + a.Az[i] * exz; aez[i] = a.Ax[i] * ezx + a.Ay[i] * ezy + a.Az[i] * ezz;
				mex[i] = a.Mx[i] * exx + a.My[i] * exy + a.Mz[i] * exz; mez[i] = a.Mx[i] * ezx + a.My[i] * ezy + a.Mz[i] * ezz;
				for (int y = y0; y <= y1; y++) { if (rows[y] == null) rows[y] = new List<int>(); rows[y].Add(i); }
			}
			for (int y = 0; y < H; y++)
			{
				if (!Disc(R, y, out int pl, out int pr)) continue;
				double v = (y + 0.5 - 100) / R, c = Math.Sqrt(Math.Max(0, 1 - v * v));
				var row = new List<(int i, Cross q)>();
				if (c > 1e-9 && rows[y] != null)
					foreach (int i in rows[y])
					{
						double d = -v * ny[i] / c;
						if (Math.Abs(d) > m2[i]) continue;
						double cw = d / m2[i], sw = Math.Sqrt(Math.Max(0, 1 - cw * cw));
						for (int s = 0; s < 2; s++)
						{
							double sgn = s == 0 ? 1 : -1;
							double cth = cph[i] * cw - sgn * sph[i] * sw, sth = sph[i] * cw + sgn * cph[i] * sw;
							if (cth <= 0) continue;
							double cp = v * vA[i] + c * (cth * aez[i] + sth * aex[i]);
							double sp = v * dA[i] + c * (cth * mez[i] + sth * mex[i]);
							if (sp <= 0 || sp * a.Ct[i] - cp * a.St[i] >= 0) continue;
							bool down = dA[i] * cp - vA[i] * sp > 0;
							double x = 128 + R * c * sth + 0.5;                  // как путь А
							int pair = (int)Math.Round(x / 2);
							row.Add((i, new Cross { Y = y, X = x, T = Math.Atan2(sp, cp), Down = down, Vis = pair > pl && pair <= pr,
								Right = down ? a.TL[i] : a.TR[i], Left = down ? a.TR[i] : a.TL[i] }));
						}
					}
				// текстура на левом краю окна (как путь А): левее самой левой границы, затем правее
				// каждой границы с парой <= pl — такие границы (и правее pr) отрезков не дают и в вид не идут
				row.Sort((p, q) => p.q.X.CompareTo(q.q.X));
				byte cur = row.Count > 0 ? row[0].q.Left : Probe(a, v * eyx + c * ezx, v * eyy + c * ezy, v * eyz + c * ezz);
				foreach (var (i, q) in row)
				{
					if ((int)Math.Round(q.X / 2) <= pl) cur = q.Right;
					if (cross[i] == null) cross[i] = new List<Cross>();
					cross[i].Add(q);
				}
				seed[y] = cur;
			}
			// куски: пересечения дуги по параметру t, разрыв — смена направления или пропуск строки
			var pieces = new List<Piece>();
			for (int i = 0; i < n; i++)
			{
				if (cross[i] == null) continue;
				var cs = cross[i].OrderBy(q => q.T).ToList();
				int k0 = 0;
				while (k0 < cs.Count)
				{
					if (!cs[k0].Vis) { k0++; continue; }
					int k1 = k0 + 1;
					while (k1 < cs.Count && cs[k1].Vis && cs[k1].Down == cs[k0].Down && Math.Abs(cs[k1].Y - cs[k1 - 1].Y) == 1) k1++;
					var seg = cs.GetRange(k0, k1 - k0);
					bool down = seg[0].Down;
					if (!down) seg.Reverse();                             // по строкам сверху вниз
					// вершины: верх — A при движении вниз (если кусок начинается с начала дуги), иначе B
					bool fromStart = k0 == 0, toEnd = k1 == cs.Count;
					string kTop = down ? (fromStart ? Key(a, i, false) : null) : (toEnd ? Key(a, i, true) : null);
					string kBot = down ? (toEnd ? Key(a, i, true) : null) : (fromStart ? Key(a, i, false) : null);
					Fit(pieces, seg, kTop, kBot);
					k0 = k1;
				}
			}
			// цепочки между дугами: в вершине и строке — по порядку слева направо
			var ends = new Dictionary<string, List<Piece>>(); var starts = new Dictionary<string, List<Piece>>();
			foreach (var p in pieces)
			{
				if (p.KBot != null && p.Next == null) { string k = p.KBot + ":" + (p.R0 + p.Rows); if (!ends.TryGetValue(k, out var l)) ends[k] = l = new List<Piece>(); l.Add(p); }
				if (p.KTop != null && p.Prev == null) { string k = p.KTop + ":" + p.R0; if (!starts.TryGetValue(k, out var l)) starts[k] = l = new List<Piece>(); l.Add(p); }
			}
			foreach (var kv in ends)
			{
				if (!starts.TryGetValue(kv.Key, out var fsl)) continue;
				var es = kv.Value.OrderBy(p => p.Xs[p.Rows - 1]).ToList();
				var fs = fsl.OrderBy(p => p.Xs[0]).ToList();
				for (int i = 0; i < Math.Min(es.Count, fs.Count); i++) { es[i].Next = fs[i]; fs[i].Prev = es[i]; }
			}
			var heads = pieces.Where(p => p.Prev == null).ToList();
			// места вставки: проход строк с точным порядком
			var byRow = new List<Piece>[H];
			foreach (var h in heads) { if (byRow[h.R0] == null) byRow[h.R0] = new List<Piece>(); byRow[h.R0].Add(h); }
			var active = new List<Piece>();
			for (int y = 0; y < H; y++)
			{
				if (byRow[y] != null)
					foreach (var h in byRow[y].OrderBy(p => p.Xs[0]))
					{
						int k = 0;
						while (k < active.Count && active[k].Xs[y - active[k].R0] <= h.Xs[0]) k++;
						h.Pos = k;
						active.Insert(k, h);
						if (active.Count > EvMax) throw new Exception($"GVE2: active pieces > {EvMax} in row {y}");
					}
				for (int k = 0; k < active.Count; k++)
				{
					var p = active[k];
					if (y == p.R0 + p.Rows - 1) { if (p.Next != null) active[k] = p.Next; else { active.RemoveAt(k); k--; } }
				}
			}
			nPieces = pieces.Count;
			return Pack(heads, byRow, seed);
		}

		// точная подгонка кусков: целые X0, dX, пара (X + Bias) >> 8 в строках — как у путь А
		static void Fit(List<Piece> outp, List<Cross> seg, string kTop, string kBot)
		{
			int n = seg.Count;
			var xs = seg.Select(q => (q.X - 0.5) / 2 * 256).ToArray();                  // X — без +0.5 путь А
			var pair = seg.Select(q => 2 * (int)Math.Round(q.X / 2) / 2).ToArray();      // пара границы путь А
			var L = pair.Select(v => v * 256 - Bias).ToArray();
			var U = pair.Select(v => v * 256 - Bias + 255).ToArray();
			int k0 = 0;
			Piece prev = null;
			while (k0 < n)
			{
				int bk1 = k0 + 1, bX0 = Math.Min(U[k0], Math.Max(L[k0], (int)Math.Round(xs[k0]))), bdX = 0;
				for (int k1 = k0 + 2; k1 <= n; k1++)
				{
					int m = k1 - 1 - k0;
					int dlo = (int)Math.Ceiling((L[k1 - 1] - U[k0]) / (double)m), dhi = (int)Math.Floor((U[k1 - 1] - L[k0]) / (double)m);
					int dm = (int)Math.Round((xs[k1 - 1] - xs[k0]) / m);
					bool found = false;
					for (int s = 0; s <= dhi - dlo && !found; s++)
						foreach (int d in new[] { dm + s, dm - s })
						{
							if (d < dlo || d > dhi) continue;
							long lo = long.MinValue, hi = long.MaxValue;
							for (int k = k0; k < k1; k++) { lo = Math.Max(lo, L[k] - (long)(k - k0) * d); hi = Math.Min(hi, U[k] - (long)(k - k0) * d); if (lo > hi) break; }
							if (lo <= hi) { found = true; bk1 = k1; bdX = d; bX0 = (int)Math.Min(hi, Math.Max(lo, (long)Math.Round(xs[k0]))); break; }
						}
					if (!found) break;
				}
				var p = new Piece
				{
					R0 = seg[k0].Y, Rows = bk1 - k0, X0 = bX0, DX = bdX, Tex = seg[0].Right,
					Xs = xs.Skip(k0).Take(bk1 - k0).ToArray(), Pair = pair.Skip(k0).Take(bk1 - k0).ToArray(),
					KTop = k0 == 0 ? kTop : null, KBot = bk1 == n ? kBot : null,
				};
				if (prev != null) { prev.Next = p; p.Prev = prev; }
				outp.Add(p);
				prev = p;
				k0 = bk1;
			}
		}

		static byte[] Pack(List<Piece> heads, List<Piece>[] byRow, int[] seed)
		{
			var b = new List<byte>(4096);
			var sd = new List<(int, int)>();
			int last = -2;
			for (int y = 0; y < H; y++) if (seed[y] >= 0 && seed[y] != last) { sd.Add((y, seed[y])); last = seed[y]; }
			if (sd.Count > 255) throw new Exception("GVE2: seeds > 255");
			b.Add((byte)sd.Count);
			foreach (var (y, t) in sd) { b.Add((byte)y); b.Add((byte)t); }
			int prevRow = 0;
			for (int r = 0; r < H; r++)
			{
				if (byRow[r] == null) continue;
				var hs = byRow[r].OrderBy(p => p.Pos).ToList();
				int d = r - prevRow; prevRow = r;
				while (d > 255) { b.Add(255); b.Add(0); d -= 255; }
				if (hs.Count > 255) throw new Exception("GVE2: starts > 255");
				b.Add((byte)d); b.Add((byte)hs.Count);
				foreach (var h in hs)
				{
					int xs = h.X0;
						if (xs < 0 || xs > 65535 || h.Pos > 255) throw new Exception("GVE2: X0/pos out of range");
					b.Add((byte)xs); b.Add((byte)(xs >> 8)); b.Add((byte)h.Pos);
					for (var e = h; ; e = e.Next)
					{
						bool shortOk = e.Rows <= 16 && e.DX >= -2048 && e.DX < 2048;
						if (e.Rows > 256) throw new Exception("GVE2: rows > 256");
						b.Add((byte)((e.Next != null ? 0x80 : 0) | (shortOk ? 0 : 0x40) | e.Tex));
						if (shortOk) { b.Add((byte)(((e.Rows - 1) << 4) | ((e.DX >> 8) & 15))); b.Add((byte)e.DX); }
						else { b.Add((byte)(e.Rows - 1)); b.Add((byte)e.DX); b.Add((byte)(e.DX >> 8)); }
						if (e.Next == null) break;
						int corr = (short)(e.Next.X0 - (e.X0 + e.Rows * e.DX));          // по модулю 2^16, как в движке
						if (corr > -128 && corr < 128) b.Add((byte)corr);
						else { b.Add(0x80); b.Add((byte)corr); b.Add((byte)(corr >> 8)); }
					}
				}
			}
			b.Add(0); b.Add(0);
			return b.ToArray();
		}

		// Распаковка вида как в движке -> отрезки строк в формате путь А (u8 n, n × (len−1, tex))
		public static byte[] Decode(byte[] v, int zoom)
		{
			int R = ZR[zoom], p = 0;
			int nSeed = v[p++];
			var seedRow = new int[nSeed]; var seedTex = new int[nSeed];
			for (int i = 0; i < nSeed; i++) { seedRow[i] = v[p++]; seedTex[i] = v[p++]; }
			var act = new List<int[]>();                          // X, dX, rows, tex, more, ptr
			int[] ReadPiece(int X, int q)
			{
				int f = v[q++], rows, dX;
				if ((f & 0x40) != 0) { rows = v[q++] + 1; dX = (short)(v[q] | (v[q + 1] << 8)); q += 2; }
				else { int h = v[q++], l = v[q++]; rows = (h >> 4) + 1; dX = ((((h & 15) << 8) | l) << 20) >> 20; }
				return new[] { X, dX, rows, f & 15, (f & 0x80) != 0 ? 1 : 0, q };
			}
			int Skip(int q, bool more)
			{
				while (more)
				{
					if (v[q++] == 0x80) q += 2;
					int f = v[q++];
					more = (f & 0x80) != 0;
					q += (f & 0x40) != 0 ? 3 : 2;
				}
				return q;
			}
			int groupRow = v[p], groupN = v[p + 1];
			p += 2;
			if (groupRow == 0 && groupN == 0) groupRow = 999;
			int seedI = 0, cur = 13;
			var outb = new List<byte>();
			for (int y = 0; y < H; y++)
			{
				while (groupRow == y)
				{
					for (int i = 0; i < groupN; i++)
					{
						int X = v[p] | (v[p + 1] << 8), pos = v[p + 2];
						var s = ReadPiece(X, p + 3);
						act.Insert(pos, s);
						p = Skip(s[5], s[4] != 0);
					}
					int d = v[p++], nn = v[p++];
					if (d == 0 && nn == 0) { groupRow = 999; break; }
					groupRow += d; groupN = nn;
				}
				while (seedI < nSeed && seedRow[seedI] == y) cur = seedTex[seedI++];
				bool disc = Disc(R, y, out int pl, out int pr);
				int t = cur, x0 = pl, nr = outb.Count, cnt = 0;
				outb.Add(0);
				for (int k = 0; k < act.Count; k++)
				{
					var s = act[k];
					if (disc)
					{
						int bx = ((s[0] + Bias) & 0xFFFF) >> 8;         // без прижатия: в виде только границы окна
						if (bx > x0) { outb.Add((byte)(bx - x0 - 1)); outb.Add((byte)t); cnt++; x0 = bx; }
					}
					t = s[3];
					s[0] = (s[0] + s[1]) & 0xFFFF;
					if (--s[2] == 0)
					{
						if (s[4] != 0)
						{
							int q = s[5];
							int c = (sbyte)v[q++];
							if (c == -128) { c = (short)(v[q] | (v[q + 1] << 8)); q += 2; }
							var ns = ReadPiece((s[0] + c) & 0xFFFF, q);
							act[k] = ns;
						}
						else { act.RemoveAt(k); k--; }
					}
				}
				if (!disc) { outb.RemoveAt(outb.Count - 1); outb.Add(0); continue; }
				if (x0 <= pr) { outb.Add((byte)(pr - x0)); outb.Add((byte)t); cnt++; }
				outb[nr] = (byte)cnt;
			}
			return outb.ToArray();
		}

		static bool Disc(int R, int y, out int pl, out int pr)
		{
			pl = pr = 0;
			int dy = 2 * y + 1 - 200, rr = 4 * R * R - dy * dy;
			if (rr <= 0) return false;
			int s = 0;
			while ((s + 1) * (s + 1) < rr) s++;
			int lo = Math.Max(0, (256 - s) >> 1), hi = Math.Min(255, (255 + s) >> 1);
			if (hi < lo) return false;
			pl = lo >> 1; pr = hi >> 1;
			return true;
		}

		static byte Probe(GlobeArcs a, double x, double y, double z)
		{
			double l = Math.Atan2(y, x) * 180 / Math.PI, bb = Math.Asin(Math.Max(-1, Math.Min(1, z))) * 180 / Math.PI;
			int gx = (int)Math.Floor(((l % 360) + 360) % 360 / 5.0) % 72;
			int gy = Math.Min(35, Math.Max(0, (int)Math.Floor((bb + 90) / 5.0)));
			byte t = a.Grid[gy * 72 + gx];
			return t == 0xFE ? a.TexAt(x, y, z) : t;
		}

		// Сверка с путём А: разные пары (длина x текстура по строкам)
		static int Compare(byte[] runsA, byte[] runsB, int zoom)
		{
			int R = ZR[zoom], pa = 0, pb = 0, diff = 0;
			for (int y = 0; y < H; y++)
			{
				int na = runsA[pa++], nb = runsB[pb++];
				if (!Disc(R, y, out int pl, out int pr)) { pa += 2 * na; pb += 2 * nb; continue; }
				var ra = new int[128]; var rb = new int[128];
				int x = pl;
				for (int i = 0; i < na; i++) { int len = runsA[pa++] + 1, t = runsA[pa++]; for (int k = 0; k < len && x < 128; k++) ra[x++] = t; }
				x = pl;
				for (int i = 0; i < nb; i++) { int len = runsB[pb++] + 1, t = runsB[pb++]; for (int k = 0; k < len && x < 128; k++) rb[x++] = t; }
				for (int k = pl; k <= pr; k++) if (ra[k] != rb[k]) diff++;
			}
			return diff;
		}

		public static (string info, long bytes) Build(GlobeArcs arcs, string path)
		{
			const int NZoom = GlobeViews.NZoom, ZFirst = GlobeViews.ZFirst;
			var hdr = new byte[Sector];
			BitConverter.GetBytes(0x32455647u).CopyTo(hdr, 0);          // 'GVE2'
			hdr[4] = NZoom; hdr[6] = ZFirst;
			var stats = new string[NZoom];
			long pos = Sector;
			var idx = new byte[NZoom][];
			var blocks = new byte[NZoom][][];
			for (int zi = 0; zi < NZoom; zi++)
			{
				int z = ZFirst + zi;
				var g = GlobeViews.Grid(z);
				int nv = g.nLon * g.nTilt;
				var views = new byte[nv][];
				var diffs = new int[nv]; var np = new int[nv];
				Parallel.For(0, g.nTilt, j =>
				{
					for (int i = 0; i < g.nLon; i++)
					{
						double lon = i * g.lonStep, tilt = (j - g.k) * g.tiltStep;
						int vi = j * g.nLon + i;
						views[vi] = View(arcs, z, lon, tilt, out np[vi]);
						diffs[vi] = Compare(GlobeViews.ViewRuns(arcs, z, lon, tilt), Decode(views[vi], z), z);
					}
				});
				long idxPos = pos;
				var tab = new byte[(nv + 1) * 4];
				pos += tab.Length;
				long tot = 0, max = 0, dsum = 0, dmax = 0, psum = 0;
				for (int k = 0; k < nv; k++)
				{
					if (views[k].Length > MaxView) throw new Exception($"GVE2: z{z} view {k} {views[k].Length} bytes > {MaxView}");
					BitConverter.GetBytes((uint)pos).CopyTo(tab, k * 4);
					pos += views[k].Length;
					tot += views[k].Length; max = Math.Max(max, views[k].Length);
					dsum += diffs[k]; dmax = Math.Max(dmax, diffs[k]); psum += np[k];
				}
				BitConverter.GetBytes((uint)pos).CopyTo(tab, nv * 4);
				idx[zi] = tab; blocks[zi] = views;
				int ho = 8 + zi * 16;
				BitConverter.GetBytes((ushort)g.nLon).CopyTo(hdr, ho);
				BitConverter.GetBytes((ushort)g.nTilt).CopyTo(hdr, ho + 2);
				BitConverter.GetBytes((ushort)Math.Round(g.tiltStep * 65536 / 360)).CopyTo(hdr, ho + 4);
				BitConverter.GetBytes((ushort)ZR[z]).CopyTo(hdr, ho + 6);
				BitConverter.GetBytes((uint)idxPos).CopyTo(hdr, ho + 8);
				stats[zi] = $"z{z} {g.nLon}x{g.nTilt}={nv} views, {psum / (double)nv:0} pieces, {tot / (double)nv:0} B avg, {max} B max, " +
					$"{(tot + tab.Length) / 1048576.0:0.00} MB, pairs differing from runs {dsum} (max {dmax} in a view)";
			}
			using (var fs = new FileStream(path, FileMode.Create, FileAccess.Write))
			{
				fs.Write(hdr, 0, hdr.Length);
				for (int zi = 0; zi < NZoom; zi++)
				{
					fs.Write(idx[zi], 0, idx[zi].Length);
					foreach (var v in blocks[zi]) fs.Write(v, 0, v.Length);
				}
				return (string.Join("; ", stats), fs.Length);
			}
		}
	}
}
