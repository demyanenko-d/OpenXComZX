// YAML (YamlDotNet) как дерево: Dictionary<object,object>, List<object>, string (скаляры), null.
// Помощники доступа — как обращение к полям объекта в JS (undefined = null).
using System;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using YamlDotNet.Serialization;

namespace OxzConv
{
	static class Y
	{
		public static object Load(string text)
		{
			return new DeserializerBuilder().Build().Deserialize<object>(new StringReader(text));
		}

		public static Dictionary<object, object> Map(object o) => o as Dictionary<object, object>;
		public static List<object> List(object o) => o as List<object>;

		public static object Get(object map, string key)
		{
			var m = Map(map);
			return m != null && m.TryGetValue(key, out var v) ? v : null;
		}

		public static bool Has(object map, string key) { var m = Map(map); return m != null && m.ContainsKey(key); }

		public static string Str(object o) => o == null ? null : o as string ?? o.ToString();

		public static double Num(object o, double def = 0)
		{
			var s = Str(o);
			if (s == null) return def;
			if (s == "true") return 1;
			if (s == "false") return 0;
			if (s.StartsWith("0x", StringComparison.OrdinalIgnoreCase) && long.TryParse(s.Substring(2), NumberStyles.HexNumber, CultureInfo.InvariantCulture, out var h)) return h;
			return double.TryParse(s, NumberStyles.Float, CultureInfo.InvariantCulture, out var d) ? d : def;
		}

		public static int Int(object o, int def = 0) => (int)Num(o, def);

		public static bool Bool(object o) => Str(o) == "true";

		// Скаляр как строка в том виде, как её дал бы String(v) после js-yaml
		// (числа нормализуются: "1.0" -> "1", "0x10" -> "16").
		public static string JsString(object o)
		{
			if (o == null) return "";
			var s = Str(o);
			if (s == "~" || s == "null") return "";
			return s;
		}
	}
}
