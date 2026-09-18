// Шрифты OpenXcom (common/Language/Font.dat + PNG) -> ресурс FONT.
// Глиф — ячейка сетки PNG; ширина — от левого до правого непрозрачного столбца
// (Font::init). Пиксели — уровни 0..5; при выводе цвет = цвет_текста + уровень*mul
// (Text::draw, PaletteShift): 4 бита на пиксель, старший полубайт — левый.
// FONT: u8 cell_w, u8 cell_h, i8 spacing, u8 first=#20, u16 n; n x {u8 width,
// u16 offset}; глифы — cell_h строк по ceil(width/2) байт.
using System;
using System.Collections.Generic;
using System.IO;

namespace OxzConv
{
	class Glyph { public Img Img; public int X, Y, W, H; public bool Empty; }

	class Font
	{
		public string Id; public int W, H, Spacing;
		public Dictionary<int, Glyph> Glyphs = new Dictionary<int, Glyph>();
	}

	static class Fonts
	{
		public static Dictionary<string, Font> Load(OxcomData ox)
		{
			var doc = Y.Load(ox.ReadText("common/Language/Font.dat"));
			var fonts = new Dictionary<string, Font>();
			foreach (var fo in Y.List(Y.Get(doc, "fonts")))
			{
				var f = new Font { Id = Y.Str(Y.Get(fo, "id")), W = Y.Int(Y.Get(fo, "width")), H = Y.Int(Y.Get(fo, "height")), Spacing = Y.Int(Y.Get(fo, "spacing")) };
				foreach (var im in Y.List(Y.Get(fo, "images")))
				{
					string file = Y.Str(Y.Get(im, "file"));
					if (!ox.Exists("common/Language/" + file)) continue;       // CJK-шрифты не вшиты
					int w = Y.Has(im, "width") ? Y.Int(Y.Get(im, "width")) : f.W, h = Y.Has(im, "height") ? Y.Int(Y.Get(im, "height")) : f.H;
					var img = Png.DecodeIndexed(ox.ReadBytes("common/Language/" + file));
					int cols = img.W / w;
					string chars = Y.Str(Y.Get(im, "chars")).Replace("\n", "");
					int idx = 0;
					for (int ci = 0; ci < chars.Length; ci++, idx++)
					{
						int cp = char.IsHighSurrogate(chars[ci]) && ci + 1 < chars.Length ? char.ConvertToUtf32(chars[ci], chars[++ci]) : chars[ci];
						if (f.Glyphs.ContainsKey(cp)) continue;
						int sx = idx % cols * w, sy = idx / cols * h, left = -1, right = -1;
						for (int x = sx; x < sx + w && left < 0; x++)
							for (int y = sy; y < sy + h; y++) if (img.Px[y * img.W + x] != 0) { left = x; break; }
						for (int x = sx + w - 1; x >= sx && right < 0; x--)
							for (int y = sy; y < sy + h; y++) if (img.Px[y * img.W + x] != 0) { right = x; break; }
						f.Glyphs[cp] = new Glyph { Img = img, X = left < 0 ? sx : left, Y = sy, W = left < 0 ? 0 : right - left + 1, H = h };
					}
				}
				fonts[f.Id] = f;
			}
			return fonts;
		}

		public static (byte[] data, int missing) Encode(Font font, List<(int code, int cp)> glyphList)
		{
			int n = glyphList.Count, missing = 0;
			var hdr = new byte[6 + 3 * n];
			hdr[0] = (byte)font.W; hdr[1] = (byte)font.H; hdr[2] = (byte)(sbyte)font.Spacing; hdr[3] = 0x20; hdr[4] = (byte)n; hdr[5] = (byte)(n >> 8);
			var data = new MemoryStream();
			for (int i = 0; i < n; i++)
			{
				int cp = glyphList[i].cp;
				Glyph g;
				if (cp == 0x20) g = new Glyph { W = Math.Max(1, font.W >> 1), H = font.H, Empty = true };
				else if (!font.Glyphs.TryGetValue(cp, out g)) { g = font.Glyphs[0x3F]; missing++; }
				int w = g.W, bpr = (w + 1) / 2;
				var d = new byte[bpr * font.H];
				if (!g.Empty)
					for (int y = 0; y < g.H; y++)
						for (int x = 0; x < w; x++)
						{
							int v = g.Img.Px[(g.Y + y) * g.Img.W + g.X + x] & 0x0F;
							d[y * bpr + (x >> 1)] |= (byte)((x & 1) != 0 ? v : v << 4);
						}
				int offs = (int)data.Length;
				hdr[6 + i * 3] = (byte)w; hdr[7 + i * 3] = (byte)offs; hdr[8 + i * 3] = (byte)(offs >> 8);
				data.Write(d, 0, d.Length);
			}
			var o = new byte[hdr.Length + data.Length];
			hdr.CopyTo(o, 0);
			data.ToArray().CopyTo(o, hdr.Length);
			return (o, missing);
		}
	}
}
