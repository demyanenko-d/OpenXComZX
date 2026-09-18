// Правила OpenXcom (*.rul, YAML) -> бинарные таблицы по схеме (RulesSchema.cs).
//
// Таблица (ресурс TABLE): заголовок 8 байт: u16 n, u16 размер записи,
// u16 смещение «хвоста» от начала ресурса, u16 0; n записей фиксированного размера
// (little-endian, без выравнивания — как упакованные struct SDCC), затем «хвост» со
// списками. Таблица должна помещаться в 16 КБ (движок читает её через одно окно Win3).
//
// Типы полей: u8 i8 u16 i16 u32 i32; flag (бит в поле flags); x100 (дробное*100 -> i16);
// str (ключ строки -> номер STRINGS, #FFFF — нет); ref:T (ключ записи таблицы T -> номер,
// #FFFF — нет; «ref:items|crafts» — во второй таблице номер | #8000); res (имя картинки ->
// номер ресурса); refs:T (список ключей), refmap:T (объект {ключ: число}), nums (список
// чисел), nums100 (список дробных *100), stats (11 статов по u8): в записи u16 смещение в
// хвосте + u8 число элементов (у stats — 11 байт прямо в записи).
using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text;

namespace OxzConv
{
	delegate object FieldFn(object entry, string key, RuleSet rs);

	class Field
	{
		public string Name;          // имя поля в YAML (или null для вычисляемых)
		public string Kind;          // тип (см. выше)
		public object Default;       // значение, если ключа нет
		public string CName;         // имя в C (по умолчанию = Name)
		public FieldFn Fn;           // вычисляемое значение
		public string Comment;
	}

	class TableSchema
	{
		public string Name, Section, Key = "type", Comment;
		public bool SortByKey;                                       // std::map в OpenXcom (units)
		public Func<RuleSet, List<KeyValuePair<string, object>>, List<KeyValuePair<string, object>>> Sort;
		public Func<RuleSet, List<KeyValuePair<string, object>>> Source;   // записи строятся из других разделов (зоны регионов, текстуры глобуса)
		public List<Field> Fields = new List<Field>();
		public List<string> Flags = new List<string>();              // имена флагов по битам
	}

	class RuleSet
	{
		public readonly Dictionary<string, object> Sections;
		public readonly Dictionary<string, List<KeyValuePair<string, object>>> Entries = new Dictionary<string, List<KeyValuePair<string, object>>>();
		public readonly Dictionary<string, Dictionary<string, int>> Index = new Dictionary<string, Dictionary<string, int>>();
		public readonly List<string> Warnings = new List<string>();
		readonly List<TableSchema> schema;
		readonly Dictionary<string, int> strIndex;
		readonly Func<string, int> resId;

		public static readonly string[] StatNames = { "tu", "stamina", "health", "bravery", "reactions", "firing", "throwing", "strength", "psiStrength", "psiSkill", "melee" };

		public RuleSet(Dictionary<string, object> sections, List<TableSchema> schema, Dictionary<string, int> strIndex, Func<string, int> resId)
		{
			Sections = sections; this.schema = schema; this.strIndex = strIndex; this.resId = resId;
			foreach (var t in schema) Entries[t.Name] = t.Source == null ? Merge(t) : new List<KeyValuePair<string, object>>();
			foreach (var t in schema.Where(t => t.Source != null)) Entries[t.Name] = t.Source(this);
			foreach (var t in schema)
			{
				if (t.SortByKey) Entries[t.Name].Sort((a, b) => string.CompareOrdinal(a.Key, b.Key));
			}
			foreach (var t in schema) RebuildIndex(t.Name);
			// сортировки, зависящие от других таблиц (craftWeapons по launcher и т.п.)
			foreach (var t in schema.Where(t => t.Sort != null)) { Entries[t.Name] = t.Sort(this, Entries[t.Name]); RebuildIndex(t.Name); }
		}

		void RebuildIndex(string table)
		{
			var d = new Dictionary<string, int>(StringComparer.Ordinal);
			var l = Entries[table];
			for (int i = 0; i < l.Count; i++) d[l[i].Key] = i;
			Index[table] = d;
		}

		// Записи раздела с одинаковым ключом сливаются (поздние поля перекрывают),
		// порядок — первого появления (как loadRule/listOrder); «delete: X» удаляет.
		List<KeyValuePair<string, object>> Merge(TableSchema t)
		{
			var order = new List<string>();
			var map = new Dictionary<string, Dictionary<object, object>>(StringComparer.Ordinal);
			foreach (var e in Y.List(Sections.TryGetValue(t.Section ?? t.Name, out var s) ? s : null) ?? new List<object>())
			{
				var m = Y.Map(e);
				if (m == null) continue;
				if (m.ContainsKey("delete")) { string dk = Y.Str(m["delete"]); map.Remove(dk); order.Remove(dk); continue; }
				string k = Y.Str(Y.Get(m, t.Key));
				if (k == null) continue;
				if (!map.TryGetValue(k, out var acc)) { acc = new Dictionary<object, object>(); map[k] = acc; order.Add(k); }
				foreach (var kv in m) acc[kv.Key] = kv.Value;
			}
			return order.Select(k => new KeyValuePair<string, object>(k, map[k])).ToList();
		}

		public int ListOrder(string table, string key) => Index.TryGetValue(table, out var d) && key != null && d.TryGetValue(key, out var i) ? i : -1;

		// Номер записи без предупреждения (или -1)
		public int Find(string table, object keyObj)
		{
			string key = Y.Str(keyObj);
			return key != null && Index.TryGetValue(table, out var d) && d.TryGetValue(key, out var i) ? i : -1;
		}

		// Ранг ключа среди ключей таблицы по порядку std::map<std::string> (сравнение байтов)
		public int SortRank(string table, string key) => Entries[table].Select(kv => kv.Key).Count(k => string.CompareOrdinal(k, key) < 0);

		// Кэш вычислений схемы между таблицами (начала зон регионов и т.п.)
		public readonly Dictionary<string, object> Cache = new Dictionary<string, object>(StringComparer.Ordinal);

		public int Ref(string kind, object keyObj, string where)
		{
			string key = Y.Str(keyObj);
			if (string.IsNullOrEmpty(key)) return 0xFFFF;
			var tables = kind.Split('|');
			for (int i = 0; i < tables.Length; i++)
			{
				if (!Index.TryGetValue(tables[i], out var idx)) throw new InvalidDataException("schema: unknown table " + tables[i]);
				if (idx.TryGetValue(key, out var v)) return v | (i > 0 ? 0x8000 : 0);
			}
			Warnings.Add($"{where}: unknown {kind} '{key}'");
			return 0xFFFF;
		}

		public int Str(object keyObj)
		{
			string key = Y.Str(keyObj);
			if (string.IsNullOrEmpty(key)) return 0xFFFF;
			return strIndex.TryGetValue(key, out var i) ? i : 0xFFFF;
		}

		public int Res(object nameObj) { string n = Y.Str(nameObj); return string.IsNullOrEmpty(n) ? 0 : resId(n); }
		public Func<string, int> ResFind = n => -1;   // номер существующего ресурса или -1

		static int Size(string kind)
		{
			switch (kind.Split(':')[0])
			{
			case "u8": case "i8": return 1;
			case "u16": case "i16": case "x100": case "str": case "ref": case "res": case "lon": case "lat": return 2;
			case "u32": case "i32": return 4;
			case "refs": case "refmap": case "nums": case "nums100": case "areas": return 3;
			case "stats": return 11;
			default: throw new InvalidDataException("schema: bad kind " + kind);
			}
		}

		public static int RecordSize(TableSchema t) => t.Fields.Sum(f => Size(f.Kind)) + (t.Flags.Count > 0 ? (t.Flags.Count > 8 ? 2 : 1) : 0);

		static long Clamp(double v, long lo, long hi) => (long)Math.Max(lo, Math.Min(hi, Math.Round(v, MidpointRounding.AwayFromZero)));

		// «Двоичные углы» движка (src/inc/state.h): долгота 0..65535 = 0..360°
		// (360 -> 65535, чтобы не завернуть конец диапазона), широта ±16384 = ±90°.
		public static int Lon(double deg) { double d = deg % 360; if (d < 0) d += 360; return deg >= 360 ? 0xFFFF : (int)Clamp(d * 65536 / 360, 0, 0xFFFF); }
		public static int Lat(double deg) => (int)Clamp(deg * 16384 / 90, -16384, 16384);

		public (byte[] data, int count, int recSize) Build(TableSchema t)
		{
			int recSize = RecordSize(t);
			var recs = new MemoryStream();
			var tail = new MemoryStream();
			foreach (var kv in Entries[t.Name])
			{
				var e = kv.Value;
				var b = new byte[recSize];
				int o = 0;
				void W(long v, int n) { for (int i = 0; i < n; i++) b[o + i] = (byte)(v >> (8 * i)); o += n; }
				int TailPush(byte[] buf) { int off = (int)tail.Length; tail.Write(buf, 0, buf.Length); return off; }
				foreach (var f in t.Fields)
				{
					object v = f.Fn != null ? f.Fn(e, kv.Key, this) : f.Name == "$key" ? kv.Key : Y.Get(e, f.Name);
					if (v == null) v = f.Default;
					string where = $"{t.Name}.{kv.Key}.{f.Name ?? f.CName}";
					string[] ka = f.Kind.Split(new[] { ':' }, 2);
					switch (ka[0])
					{
					case "u8": W(Clamp(Y.Num(v), 0, 255), 1); break;
					case "i8": W(Clamp(Y.Num(v), -128, 127), 1); break;
					case "u16": W(Clamp(Y.Num(v), 0, 0xFFFF), 2); break;
					case "i16": W(Clamp(Y.Num(v), -32768, 32767), 2); break;
					case "u32": W(Clamp(Y.Num(v), 0, 0xFFFFFFFF), 4); break;
					case "i32": W(Clamp(Y.Num(v), int.MinValue, int.MaxValue), 4); break;
					case "x100": W(Clamp(Y.Num(v) * 100, -32768, 32767), 2); break;
					case "lon": W(Lon(Y.Num(v)), 2); break;
					case "lat": W(Lat(Y.Num(v)), 2); break;
					case "str": W(Str(v), 2); break;
					case "ref": W(Ref(ka[1], v, where), 2); break;
					case "res": W(Res(v), 2); break;
					case "stats":
						for (int i = 0; i < 11; i++) W(Clamp(Y.Num(Y.Get(v, StatNames[i])), 0, 255), 1);
						break;
					case "refs": case "refmap": case "nums": case "nums100": case "areas":
						{
							var items = new List<int>();
							int count;
							if (ka[0] == "refs") { foreach (var k in Y.List(v) ?? new List<object>()) items.Add(Ref(ka[1], k, where)); count = items.Count; }
							else if (ka[0] == "nums") { foreach (var k in Flatten(v)) items.Add((int)Clamp(Y.Num(k), -32768, 32767)); count = items.Count; }
							else if (ka[0] == "nums100") { foreach (var k in Flatten(v)) items.Add((int)Clamp(Y.Num(k) * 100, -32768, 32767)); count = items.Count; }
							else if (ka[0] == "areas")
							{
								// [lonMin, lonMax, latMin, latMax] x n: долготы — u16, широты — i16 (двоичные углы)
								var l = Flatten(v).Select(x => Y.Num(x)).ToList();
								for (int i = 0; i + 3 < l.Count; i += 4) { items.Add(Lon(l[i])); items.Add(Lon(l[i + 1])); items.Add(Lat(l[i + 2])); items.Add(Lat(l[i + 3])); }
								count = items.Count / 4;
							}
							else
							{
								foreach (var p in Y.Map(v) ?? new Dictionary<object, object>()) { items.Add(Ref(ka[1], p.Key, where)); items.Add((int)Clamp(Y.Num(p.Value), -32768, 32767)); }
								count = items.Count / 2;
							}
							var buf = new byte[items.Count * 2];
							for (int i = 0; i < items.Count; i++) { buf[i * 2] = (byte)items[i]; buf[i * 2 + 1] = (byte)(items[i] >> 8); }
							int off = items.Count > 0 ? TailPush(buf) : 0;
							W(off, 2); W(Math.Min(count, 255), 1);
							break;
						}
					}
				}
				if (t.Flags.Count > 0)
				{
					int fl = 0;
					for (int i = 0; i < t.Flags.Count; i++)
					{
						var parts = t.Flags[i].Split('=');     // «recover=true» — умолчание true
						object fv = Y.Get(e, parts[0]);
						bool on = fv != null ? Y.Bool(fv) : parts.Length > 1 && parts[1] == "true";
						if (on) fl |= 1 << i;
					}
					W(fl, t.Flags.Count > 8 ? 2 : 1);
				}
				recs.Write(b, 0, b.Length);
			}
			int n = Entries[t.Name].Count;
			var hdr = new byte[8];
			hdr[0] = (byte)n; hdr[1] = (byte)(n >> 8); hdr[2] = (byte)recSize; hdr[3] = (byte)(recSize >> 8);
			int tailOff = 8 + (int)recs.Length;
			hdr[4] = (byte)tailOff; hdr[5] = (byte)(tailOff >> 8);
			var data = hdr.Concat(recs.ToArray()).Concat(tail.ToArray()).ToArray();
			if (data.Length > 16384) Warnings.Add($"{t.Name}: table {data.Length} bytes > 16 KB");
			return (data, n, recSize);
		}

		static IEnumerable<object> Flatten(object v)
		{
			var l = Y.List(v);
			if (l == null) { if (v != null) yield return v; yield break; }
			foreach (var x in l) foreach (var y in Flatten(x)) yield return y;
		}

		// Все разделы игры из файлов *.rul (по алфавиту): списки склеиваются, прочее перекрывается.
		public static Dictionary<string, object> LoadSections(OxcomData ox, string folder)
		{
			var sections = new Dictionary<string, object>(StringComparer.Ordinal);
			foreach (var f in ox.RuleFiles(folder))
			{
				var doc = Y.Map(Y.Load(ox.ReadText($"standard/{folder}/{f}")));
				if (doc == null) continue;
				foreach (var kv in doc)
				{
					string sec = Y.Str(kv.Key);
					if (kv.Value is List<object> l)
					{
						if (sections.TryGetValue(sec, out var ex) && ex is List<object> el) el.AddRange(l);
						else sections[sec] = new List<object>(l);
					}
					else sections[sec] = kv.Value;
				}
			}
			return sections;
		}

		// Заголовок C: структуры записей, номера ресурсов таблиц, биты флагов.
		public static string Header(List<TableSchema> schema, int resBase)
		{
			var sb = new StringBuilder();
			sb.Append("// Сгенерировано OxzConv (Core/RulesSchema.cs) — не править вручную.\n");
			sb.Append("// Таблицы правил (RULES.PAK): заголовок rtable_t, записи r_<таблица>_t, «хвост» со списками.\n");
			sb.Append("#ifndef RULES_H\n#define RULES_H\n\n#include <stdint.h>\n\n");
			sb.Append("typedef struct { uint16_t off; uint8_t n; } rlist_t;   // хвост: u16 элементы (refmap — пары u16 номер, i16 число)\n");
			sb.Append("typedef struct { uint16_t n, rec_size, tail, reserved; } rtable_t;\n");
			sb.Append("typedef struct { uint8_t tu, stamina, health, bravery, reactions, firing, throwing, strength, psi_strength, psi_skill, melee; } rstats_t;\n");
			sb.Append("_Static_assert(sizeof(rlist_t) == 3 && sizeof(rstats_t) == 11, \"packed records\");\n");
			sb.Append("#define RNONE      0xFFFF\n#define RREF_ALT   0x8000   // ref на вторую таблицу («items|crafts» -> craft)\n\n");
			for (int i = 0; i < schema.Count; i++)
				sb.Append($"#define RES_RULE_{schema[i].Name.ToUpperInvariant().PadRight(16)} 0x{resBase + i:X4}\n");
			sb.Append($"#define RES_RULE_VARS             0x{resBase - 1:X4}\n\n");
			foreach (var t in schema)
			{
				sb.Append($"// {t.Section ?? t.Name}{(t.Comment != null ? " — " + t.Comment : "")}\n");
				sb.Append("typedef struct {\n");
				foreach (var f in t.Fields)
				{
					string k = f.Kind.Split(':')[0];
					string ct = k == "u8" ? "uint8_t" : k == "i8" ? "int8_t" : k == "u16" || k == "str" || k == "ref" || k == "res" || k == "lon" ? "uint16_t" : k == "i16" || k == "x100" || k == "lat" ? "int16_t"
						: k == "u32" ? "uint32_t" : k == "i32" ? "int32_t" : k == "stats" ? "rstats_t" : "rlist_t";
					string cn = f.CName ?? (f.Name == "$key" ? "name" : f.Name);
					string note = f.Comment ?? (f.Kind.Contains(":") ? f.Kind : k == "x100" ? "* 100" : k == "nums100" ? "i16 * 100"
							: k == "lon" ? "двоичный угол 0..65535" : k == "lat" ? "двоичный угол ±16384"
							: k == "areas" ? "{u16 lonMin, lonMax; i16 latMin, latMax} x n, двоичные углы" : null);
					sb.Append($"\t{ct.PadRight(9)} {Snake(cn)};{(note != null ? "   // " + note : "")}\n");
				}
				if (t.Flags.Count > 0) sb.Append($"\t{(t.Flags.Count > 8 ? "uint16_t" : "uint8_t").PadRight(9)} flags;\n");
				sb.Append($"}} r_{t.Name}_t;\n");
				sb.Append($"_Static_assert(sizeof(r_{t.Name}_t) == {RecordSize(t)}, \"r_{t.Name}_t: size must match the table record\");\n");
				for (int i = 0; i < t.Flags.Count; i++)
					sb.Append($"#define {t.Name.ToUpperInvariant()}_F_{Snake(t.Flags[i].Split('=')[0]).ToUpperInvariant().PadRight(20)} 0x{1 << i:X4}\n");
				sb.Append('\n');
			}
			sb.Append("#endif\n");
			return sb.ToString();
		}

		static string Snake(string s)
		{
			var sb = new StringBuilder();
			for (int i = 0; i < s.Length; i++)
			{
				char c = s[i];
				if (char.IsUpper(c) && i > 0 && !char.IsUpper(s[i - 1])) sb.Append('_');
				sb.Append(char.IsLetterOrDigit(c) ? char.ToLowerInvariant(c) : '_');
			}
			return sb.ToString();
		}
	}
}
