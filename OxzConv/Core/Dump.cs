// Проверка результата: OxzConv.exe dump <каталог OXZ/игра> [таблица [число]]
// Без таблицы — список ресурсов всех пакетов; с таблицей — записи RULES.PAK,
// раскодированные по схеме: числа, текст строк (LANG.PAK), названия записей по ссылкам.
using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text;

namespace OxzConv
{
	static class Dump
	{
		class Res { public int Id, Type, A, B, C; public byte[] Data; }

		static List<Res> ReadPak(string file)
		{
			var b = File.ReadAllBytes(file);
			int n = b[6] | b[7] << 8;
			var l = new List<Res>();
			for (int i = 0; i < n; i++)
			{
				int e = 16 + i * 16;
				int sec = b[e + 4] | b[e + 5] << 8, size = BitConverter.ToInt32(b, e + 6);
				var d = new byte[size];
				Array.Copy(b, sec * 512, d, 0, size);
				l.Add(new Res { Id = b[e] | b[e + 1] << 8, Type = b[e + 2], A = b[e + 10] | b[e + 11] << 8, B = b[e + 12] | b[e + 13] << 8, C = b[e + 14] | b[e + 15] << 8, Data = d });
			}
			return l;
		}

		static string[] Strings(string dir)
		{
			var s = ReadPak(Path.Combine(dir, "LANG.PAK")).First(r => r.Id == 0x0020).Data;
			int n = s[0] | s[1] << 8, hdr = 2 + 3 * n;
			var o = new string[n];
			for (int i = 0; i < n; i++)
			{
				int off = s[2 + i * 3] | s[3 + i * 3] << 8 | s[4 + i * 3] << 16;
				if (off == 0xFFFFFF) continue;
				var sb = new StringBuilder();
				for (int p = hdr + off; s[p] != 0; p++) sb.Append(s[p] >= 0x20 && s[p] < 0x7F ? (char)s[p] : s[p] == 0x0A ? '|' : '·');
				o[i] = sb.ToString();
			}
			return o;
		}

		public static int Run(string dir, string table, int count, Action<string> log)
		{
			if (table == null)
			{
				foreach (var f in Directory.GetFiles(dir, "*.PAK").OrderBy(x => x, StringComparer.Ordinal))
				{
					var l = ReadPak(f);
					log($"{Path.GetFileName(f)}: {l.Count} resources");
					foreach (var r in l) log($"  #{r.Id:X4} type {r.Type} {r.Data.Length,8} bytes  a={r.A} b={r.B} c={r.C}");
				}
				return 0;
			}
			var schema = RulesSchema.Tables();
			int ti = schema.FindIndex(t => t.Name == table);
			var rules = ReadPak(Path.Combine(dir, "RULES.PAK"));
			var str = Strings(dir);
			byte[] Tab(int i) => rules.First(r => r.Id == RulesSchema.ResBase + i).Data;
			string NameOf(int tIdx, int rec)
			{
				if (rec == 0xFFFF) return "-";
				var d = Tab(tIdx); int rs = d[2] | d[3] << 8;
				int s = d[8 + rec * rs] | d[9 + rec * rs] << 8;       // первое поле — name (str)
				return $"{rec}:{(s < str.Length ? str[s] : "?")}";
			}
			if (ti < 0) { log("unknown table " + table); return 2; }
			var data = Tab(ti);
			int n = data[0] | data[1] << 8, size = data[2] | data[3] << 8, tail = data[4] | data[5] << 8;
			log($"{table}: {n} records x {size} bytes, tail at {tail}");
			var t = schema[ti];
			for (int rec = 0; rec < Math.Min(n, count); rec++)
			{
				int o = 8 + rec * size;
				var parts = new List<string>();
				int R(int k) { int v = 0; for (int i = 0; i < k; i++) v |= data[o + i] << (8 * i); o += k; return v; }
				foreach (var f in t.Fields)
				{
					string cn = f.CName ?? f.Name;
					string[] ka = f.Kind.Split(new[] { ':' }, 2);
					string val;
					switch (ka[0])
					{
					case "u8": val = R(1).ToString(); break;
					case "i8": val = ((sbyte)R(1)).ToString(); break;
					case "u16": val = R(2).ToString(); break;
					case "i16": case "x100": val = ((short)R(2)).ToString(); break;
					case "lon": val = (R(2) * 360.0 / 65536).ToString("0.##"); break;
					case "lat": val = ((short)R(2) * 90.0 / 16384).ToString("0.##"); break;
					case "areas":
						{
							int off = R(2), cnt = R(1);
							var items = new List<string>();
							for (int i = 0; i < cnt; i++)
							{
								int p = tail + off + i * 8;
								int U(int k) => data[p + k] | data[p + k + 1] << 8;
								items.Add($"[{U(0) * 360.0 / 65536:0.#},{U(2) * 360.0 / 65536:0.#},{(short)U(4) * 90.0 / 16384:0.#},{(short)U(6) * 90.0 / 16384:0.#}]");
							}
							val = string.Join(" ", items);
							break;
						}
					case "u32": case "i32": val = R(4).ToString(); break;
					case "str": { int s = R(2); val = s == 0xFFFF ? "-" : $"\"{(s < str.Length ? str[s] : "?")}\""; break; }
					case "res": val = $"#{R(2):X4}"; break;
					case "ref": { int v = R(2); int tt = schema.FindIndex(x => x.Name == ka[1].Split('|')[0]); val = tt >= 0 ? NameOf(tt, v) : v.ToString(); break; }
					case "stats": { var sb = new List<string>(); for (int i = 0; i < 11; i++) sb.Add(R(1).ToString()); val = "[" + string.Join(",", sb) + "]"; break; }
					default:
						{
							int off = R(2), cnt = R(1);
							var items = new List<string>();
							for (int i = 0; i < cnt; i++)
							{
								int p = tail + off + i * (ka[0] == "refmap" ? 4 : 2);
								int v = data[p] | data[p + 1] << 8;
								if (ka[0] == "refs" || ka[0] == "refmap")
								{
									var tabs = ka[1].Split('|');
									int tt = schema.FindIndex(x => x.Name == tabs[(v & 0x8000) != 0 && tabs.Length > 1 ? 1 : 0]);
									string nm = tt >= 0 ? NameOf(tt, v == 0xFFFF ? v : v & 0x7FFF) : v.ToString();
									items.Add(ka[0] == "refmap" ? $"{nm}={(short)(data[p + 2] | data[p + 3] << 8)}" : nm);
								}
								else items.Add(((short)v).ToString());
							}
							val = "[" + string.Join(", ", items) + "]";
							break;
						}
					}
					parts.Add($"{cn}={val}");
				}
				if (t.Flags.Count > 0)
				{
					int fl = R(t.Flags.Count > 8 ? 2 : 1);
					parts.Add("flags=" + string.Join("+", t.Flags.Where((fn, i) => (fl & (1 << i)) != 0).Select(fn => fn.Split('=')[0])));
				}
				log($"[{rec}] " + string.Join("  ", parts));
			}
			return 0;
		}
	}
}
