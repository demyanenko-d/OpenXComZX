// Строки: YAML OpenXcom (common + xcom1/xcom2) -> однобайтовая таблица STR.
//
// Кодировка байта: #01 {ALT}, #02 {SMALLLINE}, #03 {N}, #04 неразрывный пробел,
// #0A {NEWLINE}, #10..#19 {0}..{9}, #20..#7E ASCII, #80..#FF прочие символы
// (кодовая таблица CHARSET). Номера: ключи движка (Data/engine_strings.txt), затем
// прочие ключи игры по алфавиту. Мн. число — «STR_X_one», «STR_X_other» (как
// Language::getString(id, n) OpenXcom; движок выбирает форму str_n).
// Строка не пересекает границу 16 КБ (движок читает её через Win3 целиком).
using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text;
using System.Text.RegularExpressions;

namespace OxzConv
{
	class Charset
	{
		public readonly List<int> Extra = new List<int>();
		readonly Dictionary<int, int> map = new Dictionary<int, int>();

		public int Code(int cp)
		{
			if (cp >= 0x20 && cp <= 0x7E) return cp;
			if (cp == 0xA0) return 0x04;
			if (!map.TryGetValue(cp, out var c))
			{
				if (Extra.Count >= 128) throw new InvalidDataException("charset overflow (more than 128 non-ASCII chars)");
				c = 0x80 + Extra.Count;
				map[cp] = c;
				Extra.Add(cp);
			}
			return c;
		}

		// Все печатные символы по кодам: (код, кодовая точка).
		public List<(int code, int cp)> Glyphs()
		{
			var g = new List<(int, int)>();
			for (int c = 0x20; c <= 0x7E; c++) g.Add((c, c));
			for (int i = 0; i < Extra.Count; i++) g.Add((0x80 + i, Extra[i]));
			return g;
		}
	}

	static class Strings
	{
		const int Page = 16384;

		// Слияние: общие строки, затем строки игры (перекрывают).
		public static Dictionary<string, string> Load(OxcomData ox, string gameFolder, string lang)
		{
			var o = new Dictionary<string, string>(StringComparer.Ordinal);
			void Add(object doc)
			{
				var m = Y.Map(Y.Get(doc, lang));
				if (m == null) return;
				foreach (var kv in m)
				{
					string k = Y.Str(kv.Key);
					if (kv.Value is Dictionary<object, object> forms)
						foreach (var f in forms) o[k + "_" + Y.Str(f.Key)] = Y.JsString(f.Value);
					else o[k] = Y.JsString(kv.Value);
				}
			}
			Add(Y.Load(ox.ReadText($"common/Language/{lang}.yml")));
			Add(Y.Load(ox.ReadText($"standard/{gameFolder}/Language/{lang}.yml")));
			return o;
		}

		public static byte[] Encode(string text, Charset cs)
		{
			var o = new List<byte>();
			string s = text.Replace("{NEWLINE}", "\n").Replace("{SMALLLINE}", "\x02").Replace("{ALT}", "\x01");
			for (int i = 0; i < s.Length; i++)
			{
				char ch = s[i];
				if (ch == '{')
				{
					var m = Regex.Match(s.Substring(i), @"^\{(N|\d)\}");
					if (m.Success) { o.Add((byte)(m.Groups[1].Value == "N" ? 0x03 : 0x10 + (m.Groups[1].Value[0] - '0'))); i += m.Length - 1; continue; }
				}
				if (ch == '\n') o.Add(0x0A);
				else if (ch == '\x01' || ch == '\x02') o.Add((byte)ch);
				else if (ch == '\r') continue;
				else
				{
					int cp = char.IsHighSurrogate(ch) && i + 1 < s.Length && char.IsLowSurrogate(s[i + 1]) ? char.ConvertToUtf32(ch, s[++i]) : ch;
					o.Add((byte)cs.Code(cp));
				}
			}
			o.Add(0);
			return o.ToArray();
		}

		public class Table
		{
			public byte[] Data; public int Count, EngineCount;
			public Dictionary<string, int> Index; public List<string> Missing;
		}

		// STR: u16 n, 3-байтные смещения[n] от конца таблицы (#FFFFFF — нет), строки с нулём.
		public static Table Build(Dictionary<string, string> strings, Charset cs)
		{
			var keys = Registry.Load("engine_strings.txt");
			var known = new HashSet<string>(keys, StringComparer.Ordinal);
			var extra = strings.Keys.Where(k => !known.Contains(k)).ToList();
			extra.Sort(string.CompareOrdinal);
			var all = keys.Concat(extra).ToList();
			int n = all.Count, hdrLen = 2 + 3 * n;
			var body = new MemoryStream();
			var offs = new int[n];
			int pos = hdrLen;
			for (int i = 0; i < n; i++)
			{
				if (!strings.TryGetValue(all[i], out var v)) { offs[i] = 0xFFFFFF; continue; }
				var b = Encode(v, cs);
				if (b.Length > Page) throw new InvalidDataException("string too long: " + all[i]);
				if (pos / Page != (pos + b.Length - 1) / Page)
				{
					int pad = Page - pos % Page;
					body.Write(new byte[pad], 0, pad);
					pos += pad;
				}
				offs[i] = pos - hdrLen;
				body.Write(b, 0, b.Length);
				pos += b.Length;
			}
			var data = new byte[hdrLen + body.Length];
			data[0] = (byte)n; data[1] = (byte)(n >> 8);
			for (int i = 0; i < n; i++) { data[2 + i * 3] = (byte)offs[i]; data[3 + i * 3] = (byte)(offs[i] >> 8); data[4 + i * 3] = (byte)(offs[i] >> 16); }
			body.ToArray().CopyTo(data, hdrLen);
			var index = new Dictionary<string, int>(StringComparer.Ordinal);
			for (int i = 0; i < n; i++) index[all[i]] = i;
			return new Table { Data = data, Count = n, EngineCount = keys.Count, Index = index, Missing = keys.Where(k => !strings.ContainsKey(k)).ToList() };
		}
	}
}
