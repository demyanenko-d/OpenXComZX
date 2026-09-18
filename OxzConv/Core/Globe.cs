// Геометрия глобуса (ресурс GLOBE, GEO.PAK) из GEODATA/WORLD.DAT — для рендера глобуса
// движком (src/geoscape/earth/globe.c, globe_s.s; план — project_docs/globe.md §5.4 М4б).
//
// Многоугольники WORLD.DAT перекрываются (TFTD — ~5% точек сферы в двух и более, OpenXcom
// рисует их по порядку, поздний сверху) и стыкуются с T-стыками (вершина одного на ребре
// другого). Движку это не по силам, поэтому конвертер заранее строит плоскую карту:
//   1) вершины ближе TolT к чужому ребру (дуге большого круга) вставляются в это ребро и в
//      его многоугольники (почти-T-стыки: TFTD 67, UFO 1; дальше расстояния непрерывны);
//   2) все рёбра пересекаются друг с другом (точки пересечения — новые вершины) —
//      получаются отрезки, которые пересекаются только в концах;
//   3) у каждого отрезка текстура слева и справа — по правилу OpenXcom в точках рядом с
//      серединой (последний по порядку файла многоугольник, содержащий точку, чёт-нечет в
//      гномонической проекции многоугольника; нет такого — океан 13);
//   4) отрезки с одной текстурой с двух сторон выбрасываются, цепочки отрезков на одной дуге
//      с одинаковыми сторонами склеиваются (до MaxArc).
// Остаются только границы, где текстура меняется: движку достаточно пройти строку по
// рёбрам слева направо — текстура между рёбрами известна.
//
// Рёбра разложены по ячейкам 30° x 30° (12 по долготе x 6 по широте) по середине: движок
// отсекает ячейку целиком, если она вся на задней стороне или вне окна. У ячейки свои
// вершины (общие для соседних ячеек повторяются), чтобы проецировать только нужные.
//
// Вершины — единичные векторы Q14: X = cosφ·cosλ, Y = cosφ·sinλ, Z = sinφ (широта φ минус —
// север, как в OpenXcom и state.h), |v| <= 16383. Проекция вида (λ0, наклон C, радиус R)
// линейная: x = R(Y cosλ0 − X sinλ0), y = R(cosC·Z − sinC·W), z = cosC·W + sinC·Z,
// W = X cosλ0 + Y sinλ0 (движок умножает таблицами произведений, globe.md §6.2). Эта
// проекция сохраняет ориентацию: сторона N = A × B отрезка A -> B на экране (x вправо, y
// вниз) — справа от направления A -> B. Координата хранится двумя байтами: младшие 7 бит
// (индекс T_lo) и v >> 7 (байт со знаком, индекс T_hi) — движку не нужно их разбирать.
//
// Сетка 5° (72 по долготе x 36 по широте, от λ = 0 и φ = −90°): текстура клетки, если в
// клетку не заходит ни одно ребро, иначе #FE. Нужна, когда в окне нет ни одного ребра
// (зумы 3–5 посреди океана или суши): вписанный в окно круг на зуме 5 — 8°, клетка
// с центром вида целиком в нём.
//
// Формат v6 (little-endian; globe.md §12.10):
//   u16 nCell (72), размер блоков / 6, 0
//   cell[nCell]: u16 смещение блока ячейки (от начала блоков), u16 размер шапки блока (0 — рёбер
//       нет), u16 рёбер всего, 0; вектор центра (6 байт как у вершин); i16 sinRho (радиус шапки,
//       Q14; 16384 и больше — не отсекать)                                           16 байт
//   блок ячейки: шапка — u8 ns (подблоков 15° с рёбрами, 1..4), 0; записи подблоков ns x 16 байт
//       (как у ячейки, но u16 смещение данных подблока от начала блока, vn, en, 0; центр; sinRho);
//       m зумов 3, 4, 5 — три массива ns x u16 (m = v + (v >> 3) + 2, v = (R · sinRho) >> 14 —
//       отсев подблоков движком без умножений); затем данные подблоков: вершины vn x 6 байт —
//       X, Y, Z по 2 байта: v & 127, v >> 7; рёбра en x 6 байт — u16 va, vb (номер вершины в
//       подблоке * 6 — запись проекции движка: x, y, z), u8 texL, texR (слева / справа от va -> vb
//       на экране; 13 — океан). Все смещения чётные (DMA не видит бит 0 адреса).
//   grid[36][72]: u8
using System;
using System.Collections.Generic;
using System.Linq;

namespace OxzConv
{
	// Дуги карты для растеризатора видов (GlobeViews.cs): концы, нормаль N = A x B, M = N x A
	// (дуга — P(t) = A·cos t + M·sin t, t от 0 до Tab), текстуры сторон, сетка 5° и точная проба
	class GlobeArcs
	{
		public int N;
		public double[] Ax, Ay, Az, Bx, By, Bz, Nx, Ny, Nz, Mx, My, Mz, Ct, St;
		public byte[] TL, TR;
		public byte[] Grid;                       // 36 x 72, #FE — в клетку заходит граница
		public Func<double, double, double, byte> TexAt;
	}

	static class GlobeData
	{
		const int CellLon = 12, CellLat = 6, NCell = CellLon * CellLat, NSub = 4;
		const int MaxSub = 150;                    // вершин / рёбер в подблоке (MAXCV движка)
		static readonly int[] ZoomR = { 90, 120, 180, 280, 450, 720 };
		const int VRec = 6;                       // запись проекции вершины в движке: x, y, z (по 2 байта)
		const int GridLon = 72, GridLat = 36;
		const double TolT = 3e-4;                 // притяжение вершины к ребру (рад)
		const double TolV = 1e-9;                 // совпадение вершин
		const double Eps = 2e-6;                  // отступ точки пробы текстуры от отрезка
		const double MaxArc = 12 * Math.PI / 180; // длина склеенного ребра
		const byte Ocean = 13, Mixed = 0xFE;     // океан — 14-я «текстура» (блок узора движка)

		struct V
		{
			public double X, Y, Z;
			public V(double x, double y, double z) { X = x; Y = y; Z = z; }
			public static V operator +(V a, V b) => new V(a.X + b.X, a.Y + b.Y, a.Z + b.Z);
			public static V operator -(V a, V b) => new V(a.X - b.X, a.Y - b.Y, a.Z - b.Z);
			public static V operator *(V a, double k) => new V(a.X * k, a.Y * k, a.Z * k);
			public static V operator -(V a) => new V(-a.X, -a.Y, -a.Z);
			public static double Dot(V a, V b) => a.X * b.X + a.Y * b.Y + a.Z * b.Z;
			public static V Cross(V a, V b) => new V(a.Y * b.Z - a.Z * b.Y, a.Z * b.X - a.X * b.Z, a.X * b.Y - a.Y * b.X);
			public double Len => Math.Sqrt(X * X + Y * Y + Z * Z);
			public V Norm() { double l = Len; return new V(X / l, Y / l, Z / l); }
		}

		static V Vec(double lon, double lat) => new V(Math.Cos(lat) * Math.Cos(lon), Math.Cos(lat) * Math.Sin(lon), Math.Sin(lat));
		static double Rad(int v8) => v8 * 0.125 * Math.PI / 180.0;
		static int Q14(double v) => Math.Max(-16383, Math.Min(16383, (int)Math.Round(v * 16384)));
		static double Ang(V a, V b) => Math.Atan2(V.Cross(a, b).Len, V.Dot(a, b));

		// Многоугольник для пробы текстуры: вершины в гномонической проекции на плоскость,
		// касательную в центре (дуги больших кругов — прямые), шапка для быстрого отказа
		class Poly
		{
			public int Tex;
			public int[] Vi;                      // вершины (после вставки T-стыков)
			public V C, U, W;                     // центр и базис касательной плоскости
			public double CosR;
			public double[] Px, Py;
			public void Prepare(List<V> vs)
			{
				var s = new V();
				foreach (int i in Vi) s = s + vs[i];
				C = s.Norm();
				CosR = 1;
				foreach (int i in Vi) CosR = Math.Min(CosR, V.Dot(vs[i], C));
				CosR -= 1e-7;
				var t = Math.Abs(C.Z) < 0.9 ? new V(0, 0, 1) : new V(1, 0, 0);
				U = V.Cross(t, C).Norm(); W = V.Cross(C, U);
				Px = new double[Vi.Length]; Py = new double[Vi.Length];
				for (int k = 0; k < Vi.Length; k++) { var q = vs[Vi[k]]; double d = V.Dot(q, C); Px[k] = V.Dot(q, U) / d; Py[k] = V.Dot(q, W) / d; }
			}
			public bool Inside(V p)
			{
				double d = V.Dot(p, C);
				if (d < CosR) return false;
				double x = V.Dot(p, U) / d, y = V.Dot(p, W) / d;
				bool inside = false;
				for (int i = 0, j = Px.Length - 1; i < Px.Length; j = i++)
					if ((Py[i] > y) != (Py[j] > y) && x < (Px[j] - Px[i]) * (y - Py[i]) / (Py[j] - Py[i]) + Px[i]) inside = !inside;
				return inside;
			}
		}

		public static (byte[] data, string info) Build(byte[] dat) => Build(dat, out _);

		public static (byte[] data, string info) Build(byte[] dat, out GlobeArcs arcs)
		{
			// ---- многоугольники файла, вершины по ключу (1/8°)
			var verts = new List<V>();
			var keyOf = new Dictionary<(int, int), int>();
			int VKey(int lo, int la)
			{
				lo = ((lo % 2880) + 2880) % 2880;
				if (Math.Abs(la) >= 720) lo = 0;
				if (!keyOf.TryGetValue((lo, la), out int i)) { i = verts.Count; keyOf[(lo, la)] = i; verts.Add(Vec(Rad(lo), Rad(la))); }
				return i;
			}
			var polys = new List<Poly>();
			int nFile = 0, nDegenerate = 0;
			for (int o = 0; o + 20 <= dat.Length; o += 20, nFile++)
			{
				var v = new short[10];
				for (int i = 0; i < 10; i++) v[i] = BitConverter.ToInt16(dat, o + i * 2);
				int n = v[6] != -1 ? 4 : 3;
				var vi = new List<int>();
				for (int i = 0; i < n; i++)
				{
					int k = VKey(v[i * 2], v[i * 2 + 1]);
					if (vi.Count == 0 || vi[vi.Count - 1] != k) vi.Add(k);
				}
				if (vi.Count > 1 && vi[0] == vi[vi.Count - 1]) vi.RemoveAt(vi.Count - 1);
				if (vi.Count < 3) { nDegenerate++; continue; }
				polys.Add(new Poly { Tex = v[8] & 0x0F, Vi = vi.ToArray() });
			}
			int nOrig = verts.Count;

			// ---- 1. почти-T-стыки: исходные вершины на чужих рёбрах вставляются в рёбра
			// многоугольников (в порядке от начала ребра)
			int nTj = 0;
			foreach (var p in polys)
			{
				var list = new List<int>();
				for (int e = 0; e < p.Vi.Length; e++)
				{
					int a = p.Vi[e], b = p.Vi[(e + 1) % p.Vi.Length];
					list.Add(a);
					V A = verts[a], B = verts[b], N = V.Cross(A, B);
					if (N.Len < 1e-12) continue;
					N = N.Norm();
					double ab = V.Dot(A, B);
					var on = new List<(double t, int i)>();
					for (int k = 0; k < nOrig; k++)
					{
						if (k == a || k == b) continue;
						V Q = verts[k];
						if (Math.Abs(V.Dot(Q, N)) >= TolT) continue;
						if (V.Dot(Q, A) <= ab || V.Dot(Q, B) <= ab) continue;
						on.Add((Ang(A, Q), k));
					}
					foreach (var q in on.OrderBy(q => q.t)) { list.Add(q.i); nTj++; }
				}
				p.Vi = list.ToArray();
			}

			// ---- 2. отрезки (неупорядоченные пары вершин) и их взаимные пересечения
			var segSet = new HashSet<(int, int)>();
			foreach (var p in polys)
				for (int e = 0; e < p.Vi.Length; e++)
				{
					int a = p.Vi[e], b = p.Vi[(e + 1) % p.Vi.Length];
					if (a != b) segSet.Add(a < b ? (a, b) : (b, a));
				}
			var segs = segSet.ToList();
			int ns = segs.Count;
			var sN = new V[ns]; var sC = new V[ns]; var sR = new double[ns];
			for (int s = 0; s < ns; s++)
			{
				V A = verts[segs[s].Item1], B = verts[segs[s].Item2];
				sN[s] = V.Cross(A, B).Norm();
				sC[s] = (A + B).Norm();
				sR[s] = Ang(A, sC[s]);
			}
			// точки на отрезке: вершины (включая концы) с параметром — углом от начала
			var onSeg = new List<(double t, int v)>[ns];
			for (int s = 0; s < ns; s++) onSeg[s] = new List<(double, int)> { (0, segs[s].Item1), (Ang(verts[segs[s].Item1], verts[segs[s].Item2]), segs[s].Item2) };
			int nCross = 0;
			int NewVert(V p)
			{
				for (int k = nOrig; k < verts.Count; k++) if ((verts[k] - p).Len < TolV) return k;
				verts.Add(p);
				return verts.Count - 1;
			}
			for (int s = 0; s < ns; s++)
			{
				int a = segs[s].Item1, b = segs[s].Item2;
				V A = verts[a], B = verts[b];
				for (int u = s + 1; u < ns; u++)
				{
					int c = segs[u].Item1, d = segs[u].Item2;
					if (c == a || c == b || d == a || d == b) continue;
					double rr = sR[s] + sR[u] + 1e-9;
					if (rr < Math.PI && V.Dot(sC[s], sC[u]) < Math.Cos(rr)) continue;
					V L = V.Cross(sN[s], sN[u]);
					if (L.Len < 1e-12) continue;          // одна дуга: общие куски — через T-стыки
					V P = L.Norm();
					if (V.Dot(P, sC[s]) < 0) P = -P;
					// P строго внутри обоих отрезков
					if (V.Dot(V.Cross(A, P), sN[s]) <= 1e-13 || V.Dot(V.Cross(P, B), sN[s]) <= 1e-13) continue;
					V Cc = verts[c], D = verts[d];
					if (V.Dot(V.Cross(Cc, P), sN[u]) <= 1e-13 || V.Dot(V.Cross(P, D), sN[u]) <= 1e-13) continue;
					int k = NewVert(P);
					onSeg[s].Add((Ang(A, P), k));
					onSeg[u].Add((Ang(Cc, P), k));
					nCross++;
				}
			}
			// куски отрезков между соседними точками
			var pieces = new HashSet<(int, int)>();
			for (int s = 0; s < ns; s++)
			{
				var l = onSeg[s].OrderBy(q => q.t).Select(q => q.v).ToList();
				for (int i = 0; i + 1 < l.Count; i++)
					if (l[i] != l[i + 1]) pieces.Add(l[i] < l[i + 1] ? (l[i], l[i + 1]) : (l[i + 1], l[i]));
			}

			// ---- 3. текстуры сторон кусков
			foreach (var p in polys) p.Prepare(verts);
			byte TexAt(V q)
			{
				for (int i = polys.Count - 1; i >= 0; i--) if (polys[i].Inside(q)) return (byte)polys[i].Tex;
				return Ocean;
			}
			var kept = new List<(int a, int b, byte tl, byte tr)>();
			int nPieces = pieces.Count;
			foreach (var (a, b) in pieces)
			{
				V A = verts[a], B = verts[b];
				V M = (A + B).Norm(), N = V.Cross(A, B).Norm();
				byte tr = TexAt((M + N * Eps).Norm()), tl = TexAt((M - N * Eps).Norm());
				if (tl != tr) kept.Add((a, b, tl, tr));
			}

			// ---- 4. склейка цепочек: вершина с двумя рёбрами на одной дуге и теми же сторонами
			var inc = new Dictionary<int, List<int>>();
			for (int i = 0; i < kept.Count; i++)
			{
				foreach (int v in new[] { kept[i].a, kept[i].b })
				{
					if (!inc.TryGetValue(v, out var l)) inc[v] = l = new List<int>();
					l.Add(i);
				}
			}
			var alive = Enumerable.Repeat(true, kept.Count).ToArray();
			var arr = kept.ToArray();
			int nMerged = 0;
			foreach (var kv in inc)
			{
				int v = kv.Key;
				var l = kv.Value.Where(i => alive[i]).ToList();
				if (l.Count != 2) continue;
				var e1 = arr[l[0]]; var e2 = arr[l[1]];
				// e1 направить в v, e2 — из v
				if (e1.b != v) e1 = (e1.b, e1.a, e1.tr, e1.tl);
				if (e2.a != v) e2 = (e2.b, e2.a, e2.tr, e2.tl);
				if (e1.a == e2.b || e1.tl != e2.tl || e1.tr != e2.tr) continue;
				V A = verts[e1.a], M = verts[v], B = verts[e2.b];
				V N = V.Cross(A, B);
				if (N.Len < 1e-9 || Math.Abs(V.Dot(M, N.Norm())) > 1e-9) continue;
				if (V.Dot(V.Cross(A, M), N) <= 0 || V.Dot(V.Cross(M, B), N) <= 0) continue;
				if (Ang(A, B) > MaxArc) continue;
				alive[l[1]] = false;
				arr[l[0]] = (e1.a, e2.b, e1.tl, e1.tr);
				// у дальнего конца e2 ребро l[1] заменяется на l[0]
				var lb = inc[e2.b]; lb.Remove(l[1]); lb.Add(l[0]);
				kv.Value.Clear();
				nMerged++;
			}
			var edgesAll = Enumerable.Range(0, arr.Length).Where(i => alive[i]).Select(i => arr[i]).ToList();

			// ---- 5. ячейки 30° по середине ребра, внутри — подблоки 15° (2 x 2); вершины подблока, шапки
			int Cell15(V p, out int sub)
			{
				double lon = Math.Atan2(p.Y, p.X) * 180 / Math.PI, lat = Math.Asin(Math.Max(-1, Math.Min(1, p.Z))) * 180 / Math.PI;
				int l15 = (int)Math.Floor(((lon % 360) + 360) % 360 / 15.0) % (CellLon * 2);
				int b15 = Math.Min(CellLat * 2 - 1, Math.Max(0, (int)Math.Floor((lat + 90) / 15.0)));
				sub = (b15 % 2) * 2 + l15 % 2;
				return (b15 / 2) * CellLon + l15 / 2;
			}
			var cellEdges = new List<(int a, int b, byte tl, byte tr)>[NCell, NSub];
			for (int c = 0; c < NCell; c++) for (int k = 0; k < NSub; k++) cellEdges[c, k] = new List<(int, int, byte, byte)>();
			foreach (var e in edgesAll) { int c = Cell15((verts[e.a] + verts[e.b]).Norm(), out int k); cellEdges[c, k].Add(e); }
			var bOut = new List<byte>(); var cOut = new List<byte>();
			void W16(List<byte> o, int x) { o.Add((byte)x); o.Add((byte)(x >> 8)); }
			void WVec(List<byte> o, V q)
			{
				foreach (double c in new[] { q.X, q.Y, q.Z }) { int v = Q14(c); o.Add((byte)(v & 127)); o.Add((byte)(v >> 7)); }
			}
			// шапка: центр (нормированная сумма концов), sinρ (Q14, 16384 — не отсекать)
			(V cc, int sinRho, double rho) Cap(List<(int a, int b, byte tl, byte tr)> es)
			{
				var sum = new V();
				foreach (var e in es) sum = sum + verts[e.a] + verts[e.b];
				if (es.Count == 0 || sum.Len <= 1e-9) return (new V(1, 0, 0), 16384, 0);
				V cc = sum.Norm();
				double rho = 0;
				foreach (var e in es) rho = Math.Max(rho, Math.Max(Ang(cc, verts[e.a]), Ang(cc, verts[e.b])));
				rho += 0.5 * Math.PI / 180;                          // запас на округления проекции
				return (cc, rho >= Math.PI / 2 ? 16384 : (int)Math.Ceiling(Math.Sin(rho) * 16384), rho);
			}
			int nVert = 0, nEdge = 0, maxSubVerts = 0, maxSubEdges = 0, nDrop = 0, maxHdr = 0, nSubs = 0;
			double maxRho = 0, maxSubRho = 0;
			for (int c = 0; c < NCell; c++)
			{
				var all = new List<(int a, int b, byte tl, byte tr)>();
				var subRec = new List<byte>(); var subM = new List<byte>[3]; var subData = new List<byte>();
				for (int z = 0; z < 3; z++) subM[z] = new List<byte>();
				int nsb = 0, cellEn = 0;
				for (int k = 0; k < NSub; k++) if (cellEdges[c, k].Count > 0) nsb++;
				int hdr = 2 + nsb * (16 + 6);                         // u8 число, u8 0; записи; m зумов 3–5
				for (int k = 0; k < NSub; k++)
				{
					var es = cellEdges[c, k];
					if (es.Count == 0) continue;
					var vOut = new List<byte>(); var eOut = new List<byte>();
					var qmap = new Dictionary<(int, int, int), int>();   // совпавшие после Q14
					int vn = 0;
					int Loc(int g)
					{
						V q = verts[g];
						var key = (Q14(q.X), Q14(q.Y), Q14(q.Z));
						if (!qmap.TryGetValue(key, out int i)) { i = vn++; qmap[key] = i; WVec(vOut, q); }
						return i;
					}
					var keptE = new List<(int a, int b, byte tl, byte tr)>();
					foreach (var e in es)
					{
						int la = Loc(e.a), lb = Loc(e.b);
						if (la == lb) { nDrop++; continue; }
						W16(eOut, la * VRec); W16(eOut, lb * VRec); eOut.Add(e.tl); eOut.Add(e.tr);
						keptE.Add(e);
					}
					int en = keptE.Count;
					if (vn > MaxSub || en > MaxSub) throw new Exception($"globe: sub-block {c}/{k}: {vn} vertices, {en} edges > {MaxSub}");
					nVert += vn; nEdge += en; cellEn += en; nSubs++;
					maxSubVerts = Math.Max(maxSubVerts, vn); maxSubEdges = Math.Max(maxSubEdges, en);
					all.AddRange(keptE);
					var (cc, sinRho, rho) = Cap(keptE);
					maxSubRho = Math.Max(maxSubRho, rho);
					W16(subRec, hdr + subData.Count); W16(subRec, vn); W16(subRec, en); W16(subRec, 0);
					WVec(subRec, cc); W16(subRec, sinRho);
					// m = v + (v >> 3) + 2, v = (R · sinρ) >> 14 — как cm_fill движка (sinρ >= 16384: v = 0)
					for (int z = 0; z < 3; z++)
					{
						int v = sinRho >= 16384 ? 0 : (ZoomR[3 + z] * sinRho) >> 14;
						W16(subM[z], v + (v >> 3) + 2);
					}
					subData.AddRange(vOut); subData.AddRange(eOut);
				}
				var cap = Cap(all);
				maxRho = Math.Max(maxRho, cap.rho);
				maxHdr = Math.Max(maxHdr, hdr);
				// запись ячейки: смещение блока, размер шапки, рёбер всего, 0; центр; sinρ
				W16(cOut, bOut.Count); W16(cOut, nsb > 0 ? hdr : 0); W16(cOut, cellEn); W16(cOut, 0);
				WVec(cOut, cap.cc); W16(cOut, cap.sinRho);
				if (nsb == 0) continue;
				bOut.Add((byte)nsb); bOut.Add(0);
				bOut.AddRange(subRec); foreach (var m in subM) bOut.AddRange(m);
				bOut.AddRange(subData);
			}
			while (bOut.Count % 6 != 0) bOut.Add(0);                 // блоки кратны 6: движок берёт размер как h1 · 6

			// ---- 6. сетка 5°: клетки, куда заходит ребро, — #FE; остальные — текстура центра
			var grid = new byte[GridLat * GridLon];
			var touched = new bool[GridLat * GridLon];
			int GridOf(V p)
			{
				double lon = Math.Atan2(p.Y, p.X) * 180 / Math.PI, lat = Math.Asin(Math.Max(-1, Math.Min(1, p.Z))) * 180 / Math.PI;
				int gx = (int)Math.Floor(((lon % 360) + 360) % 360 / 5.0) % GridLon;
				int gy = Math.Min(GridLat - 1, Math.Max(0, (int)Math.Floor((lat + 90) / 5.0)));
				return gy * GridLon + gx;
			}
			foreach (var e in edgesAll)
			{
				V A = verts[e.a], B = verts[e.b];
				int steps = Math.Max(2, (int)Math.Ceiling(Ang(A, B) / (0.05 * Math.PI / 180)));
				for (int i = 0; i <= steps; i++) touched[GridOf((A * (1 - (double)i / steps) + B * ((double)i / steps)).Norm())] = true;
			}
			int nMixed = 0;
			for (int gy = 0; gy < GridLat; gy++)
				for (int gx = 0; gx < GridLon; gx++)
				{
					int g = gy * GridLon + gx;
					if (touched[g]) { grid[g] = Mixed; nMixed++; continue; }
					grid[g] = TexAt(Vec((gx * 5 + 2.5) * Math.PI / 180, (gy * 5 - 90 + 2.5) * Math.PI / 180));
				}

			// ---- 7. дуги для растеризатора видов (GlobeViews.cs): точные векторы, без Q14
			{
				int n = edgesAll.Count;
				arcs = new GlobeArcs
				{
					N = n,
					Ax = new double[n], Ay = new double[n], Az = new double[n],
					Bx = new double[n], By = new double[n], Bz = new double[n],
					Nx = new double[n], Ny = new double[n], Nz = new double[n],
					Mx = new double[n], My = new double[n], Mz = new double[n],
					Ct = new double[n], St = new double[n],
					TL = new byte[n], TR = new byte[n],
					Grid = grid,
					TexAt = (x, y, z) => TexAt(new V(x, y, z)),
				};
				for (int i = 0; i < n; i++)
				{
					var e = edgesAll[i];
					V A = verts[e.a], B = verts[e.b], N = V.Cross(A, B).Norm(), M = V.Cross(N, A);
					double t = Ang(A, B);
					arcs.Ax[i] = A.X; arcs.Ay[i] = A.Y; arcs.Az[i] = A.Z;
					arcs.Bx[i] = B.X; arcs.By[i] = B.Y; arcs.Bz[i] = B.Z;
					arcs.Nx[i] = N.X; arcs.Ny[i] = N.Y; arcs.Nz[i] = N.Z;
					arcs.Mx[i] = M.X; arcs.My[i] = M.Y; arcs.Mz[i] = M.Z;
					arcs.Ct[i] = Math.Cos(t); arcs.St[i] = Math.Sin(t);
					arcs.TL[i] = e.tl; arcs.TR[i] = e.tr;
				}
			}

			var o2 = new List<byte>();
			W16(o2, NCell); W16(o2, bOut.Count / 6); W16(o2, 0);
			o2.AddRange(cOut); o2.AddRange(bOut); o2.AddRange(grid);
			string info = $"{polys.Count} polygons ({nDegenerate} degenerate), {nTj} near-T-junctions, {nCross} crossings, " +
				$"{nPieces} pieces -> {kept.Count} texture boundaries -> {nEdge} edges ({nMerged} merged, {nDrop} dropped), " +
				$"{nVert} vertices in {nSubs} sub-blocks ({maxSubVerts} / {maxSubEdges} max in a sub-block, cell header {maxHdr} max), grid {nMixed} mixed, " +
				$"max radius cell {maxRho * 180 / Math.PI:F0}° / sub-block {maxSubRho * 180 / Math.PI:F0}°, blocks {bOut.Count} B";
			return (o2.ToArray(), info);
		}
	}
}
