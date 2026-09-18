// interfaces.rul (цвета, палитры и геометрия элементов экранов) -> ресурс UI.
// UI: u16 n (= размер реестра ui_screens.txt); u16 offs[n] (0 — нет экрана);
// блок экрана: u16 палитра (номер ресурса, 0 — нет), u8 родитель (#FF), u8 музыка
// (#FF, пока не заполняется), u8 число элементов; элементы по 14 байт:
// u8 id, u8 флаги (b0 color, b1 color2, b2 border, b3 pos, b4 size, b5 TFTDMode),
// u8 color, u8 color2, u8 border, u8 0, i16 x, i16 y, u16 w, u16 h.
// Элементы родителя, не переопределённые экраном, копируются (RuleInterface::getElement).
using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;

namespace OxzConv
{
	static class Interfaces
	{
		class Screen { public string Type; public object Node; }

		static List<Screen> LoadScreens(OxcomData ox, string folder, out Dictionary<string, object> byType)
		{
			var doc = Y.Load(ox.ReadText($"standard/{folder}/interfaces.rul"));
			var order = new List<Screen>();
			byType = new Dictionary<string, object>(StringComparer.Ordinal);
			foreach (var s in Y.List(Y.Get(doc, "interfaces")) ?? new List<object>())
			{
				string t = Y.Str(Y.Get(s, "type"));
				if (!byType.ContainsKey(t)) order.Add(new Screen { Type = t });
				byType[t] = s;
			}
			foreach (var sc in order) sc.Node = byType[sc.Type];
			return order;
		}

		static List<KeyValuePair<string, object>> AllElements(object s, Dictionary<string, object> byType, int depth = 0)
		{
			var own = new List<KeyValuePair<string, object>>();
			var seen = new HashSet<string>(StringComparer.Ordinal);
			foreach (var e in Y.List(Y.Get(s, "elements")) ?? new List<object>())
			{
				string id = Y.Str(Y.Get(e, "id"));
				int at = own.FindIndex(kv => kv.Key == id);
				if (at >= 0) own[at] = new KeyValuePair<string, object>(id, e); else own.Add(new KeyValuePair<string, object>(id, e));
				seen.Add(id);
			}
			string parent = Y.Str(Y.Get(s, "parent"));
			if (parent != null && byType.TryGetValue(parent, out var p) && depth < 8)
				foreach (var kv in AllElements(p, byType, depth + 1))
					if (!seen.Contains(kv.Key)) { own.Add(kv); seen.Add(kv.Key); }
			return own;
		}

		public static (byte[] data, int screens, List<string> unknown) Build(OxcomData ox, string folder, ResIds ids)
		{
			var screens = Registry.Load("ui_screens.txt");
			var elements = Registry.Load("ui_elements.txt");
			var sIdx = Registry.Index(screens);
			var eIdx = Registry.Index(elements);
			var order = LoadScreens(ox, folder, out var byType);
			var unknown = new List<string>();
			int n = screens.Count;
			var head = new byte[2 + 2 * n];
			head[0] = (byte)n; head[1] = (byte)(n >> 8);
			var blocks = new MemoryStream();
			int pos = head.Length;
			foreach (var sc in order)
			{
				if (!sIdx.ContainsKey(sc.Type)) { unknown.Add("screen " + sc.Type); continue; }
				var els = AllElements(sc.Node, byType).Where(kv => { if (eIdx.ContainsKey(kv.Key)) return true; unknown.Add("element " + kv.Key); return false; }).ToList();
				var b = new byte[5 + els.Count * 14];
				string pal = Y.Str(Y.Get(sc.Node, "palette"));
				int palId = pal != null ? ids.Id(pal) : 0;
				b[0] = (byte)palId; b[1] = (byte)(palId >> 8);
				string parent = Y.Str(Y.Get(sc.Node, "parent"));
				b[2] = (byte)(parent != null && sIdx.ContainsKey(parent) ? sIdx[parent] : 0xFF);
				b[3] = 0xFF;
				b[4] = (byte)els.Count;
				for (int i = 0; i < els.Count; i++)
				{
					var e = els[i].Value;
					int o = 5 + i * 14, f = 0;
					if (Y.Has(e, "color")) f |= 1;
					if (Y.Has(e, "color2")) f |= 2;
					if (Y.Has(e, "border")) f |= 4;
					var posL = Y.List(Y.Get(e, "pos")); var sizeL = Y.List(Y.Get(e, "size"));
					if (posL != null) f |= 8;
					if (sizeL != null) f |= 16;
					if (Y.Bool(Y.Get(e, "TFTDMode"))) f |= 32;
					b[o] = (byte)eIdx[els[i].Key]; b[o + 1] = (byte)f;
					b[o + 2] = (byte)Y.Int(Y.Get(e, "color")); b[o + 3] = (byte)Y.Int(Y.Get(e, "color2")); b[o + 4] = (byte)Y.Int(Y.Get(e, "border"));
					void I16(int at, int v) { b[at] = (byte)v; b[at + 1] = (byte)(v >> 8); }
					I16(o + 6, posL != null ? Y.Int(posL[0]) : 0); I16(o + 8, posL != null ? Y.Int(posL[1]) : 0);
					I16(o + 10, sizeL != null ? Y.Int(sizeL[0]) : 0); I16(o + 12, sizeL != null ? Y.Int(sizeL[1]) : 0);
				}
				int idx = sIdx[sc.Type];
				head[2 + 2 * idx] = (byte)pos; head[3 + 2 * idx] = (byte)(pos >> 8);
				blocks.Write(b, 0, b.Length);
				pos += b.Length;
			}
			var data = new byte[head.Length + blocks.Length];
			head.CopyTo(data, 0);
			blocks.ToArray().CopyTo(data, head.Length);
			return (data, order.Count, unknown);
		}

		// Палитра экрана (State::setInterface/setPalette): палитра экрана или родителя
		// (по умолчанию PAL_GEOSCAPE) и блок BACKPALS из элемента «palette» (-1 — нет).
		public static Func<string, (string pal, int backpal)> ScreenPalettes(OxcomData ox, string folder)
		{
			LoadScreens(ox, folder, out var byType);
			return type =>
			{
				if (type == null || !byType.TryGetValue(type, out var s)) return ("PAL_GEOSCAPE", -1);
				string ps = Y.Str(Y.Get(s, "parent"));
				object parent = ps != null && byType.TryGetValue(ps, out var pp) ? pp : null;
				string pal = Y.Str(Y.Get(s, "palette")) ?? (parent != null ? Y.Str(Y.Get(parent, "palette")) : null) ?? "PAL_GEOSCAPE";
				object el = (Y.List(Y.Get(s, "elements")) ?? new List<object>()).FirstOrDefault(e => Y.Str(Y.Get(e, "id")) == "palette");
				if (el == null && parent != null) el = (Y.List(Y.Get(parent, "elements")) ?? new List<object>()).FirstOrDefault(e => Y.Str(Y.Get(e, "id")) == "palette");
				return (pal, el != null && Y.Has(el, "color") ? Y.Int(Y.Get(el, "color")) : -1);
			};
		}
	}
}
