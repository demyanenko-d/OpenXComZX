// Предрасчёт видов глобуса (GVIEW.PAK на SD-карте) — путь А с ограниченным наклоном,
// project_docs/globe.md §12.5. Для каждого вида сетки (зум, шаг поворота, шаг наклона)
// записываются готовые отрезки строк экрана: движку остаётся лить их в порты DMA.
// Образец и проверка точности — tools/globe_views.js (0.00–0.03 % от попиксельного эталона).
//
// Геометрия. Оси вида: ex = (−sinλ0, cosλ0, 0), ey = (−sinC·cosλ0, −sinC·sinλ0, cosC),
// ez = (cosC·cosλ0, cosC·sinλ0, sinC); экран x = 128 + R·(ex·P), y = 100 + R·(ey·P), видно при
// ez·P > 0. Строка экрана — окружность на шаре ey·P = v = (y + 0.5 − 100)/R, точки её
// P = v·ey + c·(cosθ·ez + sinθ·ex), c = √(1 − v²); видно |θ| < 90°, x = 128 + R·c·sinθ + 0.5,
// и x монотонен по θ, поэтому порядок отрезков — это порядок x.
//
// Граница карты — дуга P(t) = A·cos t + M·sin t (M = N × A, N = A × B, t от 0 до tAB). Её
// пересечение со строкой: N·P = 0, то есть (N·ez)·cosθ + (N·ex)·sinθ = −v·(N·ey)/c — два
// корня θ = φ ± w, φ = atan2(N·ex, N·ez), cos w = d/|(N·ez, N·ex)|. Тригонометрия не нужна:
// cosφ, sinφ хранятся на дугу, sin w = √(1 − cos²w), дальше формулы сложения. Попадание в
// дугу — через cos t = A·P и sin t = M·P: 0 < t < tAB ⟺ sin t > 0 и sin t·cos tAB < cos t·sin tAB.
// Текстура со стороны большего x — по направлению дуги: ey·(N × P) = (ey·M)·cos t − (ey·A)·sin t.
//
// Отсев строк: ey·P вдоль дуги — синусоида (ey·A)·cos t + (ey·M)·sin t, её пределы это концы
// плюс экстремум ±√((ey·A)² + (ey·M)²), если производная меняет знак между концами
// (производная на концах — ey·M и ey·(N × B) = (ey·M)·cos tAB − (ey·A)·sin tAB).
//
// Формат GVIEW.PAK:
//   сектор 0 — заголовок: u32 'GVW1', u16 nZoom, u16 zFirst, затем на зум 16 байт:
//     u16 nLon, u16 nTilt, u16 tiltStep (65536 = 360°), u16 R, u32 idxSec, u32 datSec
//     (наклон вида j — (j − (nTilt − 1) / 2) · tiltStep; поворот вида i — i · 65536 / nLon)
//   таблица зума (с idxSec): u16 secOff[nLon · nTilt] — сектор вида от datSec, затем u16 конца
//   вид (с начала сектора): на каждую из 200 строк — u8 n, затем n × (u8 len−1, u8 tex),
//     где len — длина отрезка в парах пикселей (DMALen = len − 1), tex 0..13 (13 — океан).
//     Отрезки покрывают ровно пары диска строки (движок берёт их из своей таблицы sh_row).
using System;
using System.Collections.Generic;
using System.IO;
using System.Threading.Tasks;

namespace OxzConv
{
	static class GlobeViews
	{
		public const int ZFirst = 0, NZoom = 3;       // зумы 0–2; 3–5 остаются рёберному рендеру
		static readonly int[] ZR = { 90, 120, 180, 280, 450, 720 };
		const int H = 200, Sector = 512;
		// Наклон на зумах 0–2 ограничен ±27°: окно там покрывает по широте ±34° и больше, то есть
		// видно до 61° широты и выше; полный наклон остаётся зумам 3–5 (рёберный рендер), где окно
		// узкое и наклон — единственный способ ходить на север-юг. Шаг наклона — родной, 0.6 от
		// шага поворота (9° / 4.5° / 3°), чтобы наклон не огрубился.
		const double TiltMax = 27, TiltMin = 0;

		public static (int nLon, int nTilt, int k, double lonStep, double tiltStep) Grid(int z)
		{
			double lonStep = 15.0 / (z + 1), tiltStep = Math.Max(lonStep * 0.6, TiltMin);
			int k = (int)Math.Floor(TiltMax / tiltStep + 1e-9);
			return (24 * (z + 1), 2 * k + 1, k, lonStep, tiltStep);
		}

		// Пары диска строки (как globe.c rows_init): пиксель внутри, если
		// (2i + 1 − 256)² + (2j + 1 − 200)² < 4R²; пара — если внутри хоть один её пиксель
		static bool Disc(int R, int y, out int pl, out int pr)
		{
			pl = pr = 0;
			int dy = 2 * y + 1 - 200, rr = 4 * R * R - dy * dy;
			if (rr <= 0) return false;
			int s = 0;                                  // как tools/gen_globe_tab.js: s² < rr
			while ((s + 1) * (s + 1) < rr) s++;
			int lo = Math.Max(0, (256 - s) >> 1), hi = Math.Min(255, (255 + s) >> 1);
			if (hi < lo) return false;
			pl = lo >> 1; pr = hi >> 1;
			return true;
		}

		struct Bnd { public double X; public byte Right, Left; }

		// Один вид -> байты (200 строк: u8 n, n × (len−1, tex))
		static byte[] View(GlobeArcs a, int zoom, double lonDeg, double tiltDeg)
		{
			int R = ZR[zoom];
			double l0 = lonDeg * Math.PI / 180, C = tiltDeg * Math.PI / 180;
			double cl = Math.Cos(l0), sl = Math.Sin(l0), cc = Math.Cos(C), sc = Math.Sin(C);
			double exx = -sl, exy = cl, exz = 0;
			double eyx = -sc * cl, eyy = -sc * sl, eyz = cc;
			double ezx = cc * cl, ezy = cc * sl, ezz = sc;
			int n = a.N;
			var vA = new double[n]; var dA = new double[n]; var m2 = new double[n];
			var cph = new double[n]; var sph = new double[n]; var ny = new double[n];
			var aex = new double[n]; var aez = new double[n];
			var mex = new double[n]; var mez = new double[n];
			var rows = new List<int>[H];
			for (int y = 0; y < H; y++) rows[y] = null;
			for (int i = 0; i < n; i++)
			{
				double va = eyx * a.Ax[i] + eyy * a.Ay[i] + eyz * a.Az[i];
				double vb = eyx * a.Bx[i] + eyy * a.By[i] + eyz * a.Bz[i];
				double da = eyx * a.Mx[i] + eyy * a.My[i] + eyz * a.Mz[i];
				double db = da * a.Ct[i] - va * a.St[i];          // ey·(N × B)
				double lo = Math.Min(va, vb), hi = Math.Max(va, vb);
				double m = Math.Sqrt(va * va + da * da);
				if (da > 0 && db < 0) hi = m;
				else if (da < 0 && db > 0) lo = -m;
				int y0 = (int)Math.Ceiling(99.5 + R * lo), y1 = (int)Math.Floor(99.5 + R * hi);
				if (y0 < 0) y0 = 0;
				if (y1 > H - 1) y1 = H - 1;
				if (y1 < y0) continue;
				double nxi = a.Nx[i] * exx + a.Ny[i] * exy + a.Nz[i] * exz;
				double nyi = a.Nx[i] * eyx + a.Ny[i] * eyy + a.Nz[i] * eyz;
				double nzi = a.Nx[i] * ezx + a.Ny[i] * ezy + a.Nz[i] * ezz;
				double mm = Math.Sqrt(nzi * nzi + nxi * nxi);
				if (mm < 1e-12) continue;
				vA[i] = va; dA[i] = da; ny[i] = nyi; m2[i] = mm;
				cph[i] = nzi / mm; sph[i] = nxi / mm;
				aex[i] = a.Ax[i] * exx + a.Ay[i] * exy + a.Az[i] * exz;
				aez[i] = a.Ax[i] * ezx + a.Ay[i] * ezy + a.Az[i] * ezz;
				mex[i] = a.Mx[i] * exx + a.My[i] * exy + a.Mz[i] * exz;
				mez[i] = a.Mx[i] * ezx + a.My[i] * ezy + a.Mz[i] * ezz;
				for (int y = y0; y <= y1; y++)
				{
					if (rows[y] == null) rows[y] = new List<int>();
					rows[y].Add(i);
				}
			}
			var outb = new List<byte>(H * 8);
			var list = new List<Bnd>(64);
			for (int y = 0; y < H; y++)
			{
				int pl, pr;
				if (!Disc(R, y, out pl, out pr)) { outb.Add(0); continue; }
				double v = (y + 0.5 - 100) / R, c = Math.Sqrt(Math.Max(0, 1 - v * v));
				list.Clear();
				if (c > 1e-9 && rows[y] != null)
					foreach (int i in rows[y])
					{
						double d = -v * ny[i] / c;
						if (Math.Abs(d) > m2[i]) continue;
						double cw = d / m2[i], sw = Math.Sqrt(Math.Max(0, 1 - cw * cw));
						for (int s = 0; s < 2; s++)
						{
							double sgn = s == 0 ? 1 : -1;
							double cth = cph[i] * cw - sgn * sph[i] * sw;
							double sth = sph[i] * cw + sgn * cph[i] * sw;
							if (cth <= 0) continue;                       // задняя сторона
							double cp = v * vA[i] + c * (cth * aez[i] + sth * aex[i]);   // cos t
							double sp = v * dA[i] + c * (cth * mez[i] + sth * mex[i]);   // sin t
							if (sp <= 0 || sp * a.Ct[i] - cp * a.St[i] >= 0) continue;   // вне дуги
							bool down = dA[i] * cp - vA[i] * sp > 0;      // дуга идёт вниз по экрану
							list.Add(new Bnd
							{
								X = 128 + R * c * sth + 0.5,
								Right = down ? a.TL[i] : a.TR[i],
								Left = down ? a.TR[i] : a.TL[i],
							});
						}
					}
				list.Sort((p, q) => p.X.CompareTo(q.X));
				byte cur = list.Count > 0 ? list[0].Left : Probe(a, v * eyx + c * ezx, v * eyy + c * ezy, v * eyz + c * ezz);
				int x0 = pl * 2, xEnd = pr * 2 + 2, nr = outb.Count;
				outb.Add(0);
				int cnt = 0;
				foreach (var b in list)
				{
					int x = 2 * (int)Math.Round(b.X / 2);
					if (x > x0)
					{
						int xe = Math.Min(x, xEnd);
						if (xe > x0) { outb.Add((byte)((xe - x0) / 2 - 1)); outb.Add(cur); cnt++; x0 = xe; }
					}
					cur = b.Right;
				}
				if (x0 < xEnd) { outb.Add((byte)((xEnd - x0) / 2 - 1)); outb.Add(cur); cnt++; }
				outb[nr] = (byte)cnt;
			}
			return outb.ToArray();
		}

		// Текстура точки: сетка 5° (#FE — в клетку заходит граница), иначе точная проба
		static byte Probe(GlobeArcs a, double x, double y, double z)
		{
			double l = Math.Atan2(y, x) * 180 / Math.PI, b = Math.Asin(Math.Max(-1, Math.Min(1, z))) * 180 / Math.PI;
			int gx = (int)Math.Floor(((l % 360) + 360) % 360 / 5.0) % 72;
			int gy = Math.Min(35, Math.Max(0, (int)Math.Floor((b + 90) / 5.0)));
			byte t = a.Grid[gy * 72 + gx];
			return t == 0xFE ? a.TexAt(x, y, z) : t;
		}

		public static (string info, long bytes) Build(GlobeArcs arcs, string path)
		{
			var hdr = new byte[Sector];
			BitConverter.GetBytes(0x31575647u).CopyTo(hdr, 0);          // 'GVW1'
			hdr[4] = NZoom; hdr[6] = ZFirst;
			var blocks = new List<byte[]>[NZoom];
			var stats = new string[NZoom];
			long sec = 1;
			var idxSec = new long[NZoom]; var datSec = new long[NZoom];
			var idx = new byte[NZoom][];
			for (int zi = 0; zi < NZoom; zi++)
			{
				int z = ZFirst + zi;
				var g = Grid(z);
				int nv = g.nLon * g.nTilt;
				var views = new byte[nv][];
				Parallel.For(0, g.nTilt, j =>
				{
					for (int i = 0; i < g.nLon; i++)
						views[j * g.nLon + i] = View(arcs, z, i * g.lonStep, (j - g.k) * g.tiltStep);
				});
				idxSec[zi] = sec;
				int idxBytes = (nv + 1) * 2;
				sec += (idxBytes + Sector - 1) / Sector;
				datSec[zi] = sec;
				var tab = new byte[((idxBytes + Sector - 1) / Sector) * Sector];
				long off = 0, tot = 0, max = 0;
				var list = new List<byte[]>(nv);
				for (int k = 0; k < nv; k++)
				{
					BitConverter.GetBytes((ushort)off).CopyTo(tab, k * 2);
					int s = (views[k].Length + Sector - 1) / Sector;
					off += s;
					tot += views[k].Length;
					if (views[k].Length > max) max = views[k].Length;
					list.Add(views[k]);
				}
				BitConverter.GetBytes((ushort)off).CopyTo(tab, nv * 2);
				idx[zi] = tab;
				blocks[zi] = list;
				sec += off;
				int ho = 8 + zi * 16;
				BitConverter.GetBytes((ushort)g.nLon).CopyTo(hdr, ho);
				BitConverter.GetBytes((ushort)g.nTilt).CopyTo(hdr, ho + 2);
				BitConverter.GetBytes((ushort)Math.Round(g.tiltStep * 65536 / 360)).CopyTo(hdr, ho + 4);
				BitConverter.GetBytes((ushort)ZR[z]).CopyTo(hdr, ho + 6);
				BitConverter.GetBytes((uint)idxSec[zi]).CopyTo(hdr, ho + 8);
				BitConverter.GetBytes((uint)datSec[zi]).CopyTo(hdr, ho + 12);
				stats[zi] = $"z{z} {g.nLon}x{g.nTilt}={nv} views, {tot / (double)nv / 1024:0.0} KB avg, {max / 1024.0:0.0} KB max, {off * Sector / 1048576.0:0.0} MB";
			}
			using (var fs = new FileStream(path, FileMode.Create, FileAccess.Write))
			{
				fs.Write(hdr, 0, hdr.Length);
				for (int zi = 0; zi < NZoom; zi++)
				{
					fs.Write(idx[zi], 0, idx[zi].Length);
					var pad = new byte[Sector];
					foreach (var v in blocks[zi])
					{
						fs.Write(v, 0, v.Length);
						int r = v.Length % Sector;
						if (r != 0) fs.Write(pad, 0, Sector - r);
					}
				}
				return (string.Join("; ", stats), fs.Length);
			}
		}
	}
}
