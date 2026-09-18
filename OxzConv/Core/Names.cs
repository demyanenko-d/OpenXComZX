// Пулы имён солдат (common/SoldierName/*.nam, soldiers.rul soldierNames: SoldierName/)
// -> ресурс NAMES (LANG.PAK). Как SoldierNamePool в OpenXcom: пул выбирается
// случайно, имя и фамилия — по полу; нет женских фамилий — берутся мужские.
// NAMES: u8 n; u16 offs[n] (от начала ресурса); пул: u8 cnt[4] (maleFirst,
// femaleFirst, maleLast, femaleLast; до 255), затем строки подряд (кодировка
// строк игры, завершающий 0) в том же порядке.
using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;

namespace OxzConv
{
	static class Names
	{
		static readonly string[] Keys = { "maleFirst", "femaleFirst", "maleLast", "femaleLast" };

		public static (byte[] data, int pools, int names) Build(OxcomData ox, Charset cs, List<string> warnings)
		{
			var files = ox.Files("common/SoldierName", ".nam");
			var pools = new List<byte[]>();
			int total = 0;
			foreach (var f in files)
			{
				object doc;
				try { doc = Y.Load(ox.ReadText("common/SoldierName/" + f)); }
				catch (Exception e) { warnings.Add($"SoldierName/{f}: {e.Message} — пул пропущен"); continue; }
				var body = new MemoryStream();
				var cnt = new byte[4];
				for (int k = 0; k < 4; k++)
				{
					var l = (Y.List(Y.Get(doc, Keys[k])) ?? new List<object>()).Select(Y.Str).Where(s => !string.IsNullOrEmpty(s)).Take(255).ToList();
					cnt[k] = (byte)l.Count;
					foreach (var s in l) { var b = Strings.Encode(s, cs); body.Write(b, 0, b.Length); }
					total += l.Count;
				}
				if (cnt[0] == 0 && cnt[1] == 0) continue;
				pools.Add(cnt.Concat(body.ToArray()).ToArray());
			}
			if (pools.Count > 255) pools = pools.Take(255).ToList();
			var o = new MemoryStream();
			o.WriteByte((byte)pools.Count);
			int pos = 1 + pools.Count * 2;
			foreach (var p in pools)
			{
				if (pos > 0xFFFF) throw new InvalidDataException("NAMES too large");
				o.WriteByte((byte)pos); o.WriteByte((byte)(pos >> 8));
				pos += p.Length;
			}
			foreach (var p in pools) o.Write(p, 0, p.Length);
			return (o.ToArray(), pools.Count, total);
		}
	}
}
