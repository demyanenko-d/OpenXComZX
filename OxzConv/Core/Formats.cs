// Декодеры форматов оригинальной X-COM (UFO и TFTD).
// Эталон — загрузчики OpenXcom: src/Engine/Surface.cpp (SCR, SPK, BDY),
// src/Engine/SurfaceSet.cpp (PCK/TAB, DAT), src/Engine/Palette.cpp.
using System;
using System.Collections.Generic;
using System.IO;
using System.Text;

namespace OxzConv
{
	// Картинка 8 бит/пиксель, 0 — прозрачный.
	class Img
	{
		public int W, H;
		public byte[] Px;
		public Img(int w, int h) { W = w; H = h; Px = new byte[w * h]; }
	}

	// Запись «по порядку» с переходом на следующую строку (setPixelIterative).
	class PixelWriter
	{
		readonly Img img;
		int i;
		public PixelWriter(Img img) { this.img = img; }
		public int Row => i / img.W;
		public void Put(byte v) { if (i < img.Px.Length) img.Px[i] = v; i++; }
	}

	static class Formats
	{
		public static Img DecodeScr(byte[] buf, int w = 320, int h = 200)
		{
			var img = new Img(w, h);
			Array.Copy(buf, img.Px, Math.Min(buf.Length, w * h));
			return img;
		}

		// SPK: слова u16: FFFF n — n*2 прозрачных, FFFE n — n*2 литералов, FFFD — конец.
		public static Img DecodeSpk(byte[] buf, int w = 320, int h = 200)
		{
			var img = new Img(w, h);
			var wr = new PixelWriter(img);
			int p = 0;
			while (p + 2 <= buf.Length)
			{
				int flag = buf[p] | buf[p + 1] << 8; p += 2;
				if (flag == 0xFFFF) { int n = buf[p] | buf[p + 1] << 8; p += 2; for (int i = 0; i < n * 2; i++) wr.Put(0); }
				else if (flag == 0xFFFE) { int n = buf[p] | buf[p + 1] << 8; p += 2; for (int i = 0; i < n * 2; i++) wr.Put(buf[p++]); }
				else if (flag == 0xFFFD) break;
			}
			return img;
		}

		// BDY (TFTD): байт >= 129 — повтор следующего байта (257 - b) раз, иначе (b + 1)
		// литералов; серия не переходит на следующую строку.
		public static Img DecodeBdy(byte[] buf, int w = 320, int h = 200)
		{
			var img = new Img(w, h);
			var wr = new PixelWriter(img);
			int p = 0;
			while (p < buf.Length)
			{
				int b = buf[p++];
				if (b >= 129)
				{
					int n = 257 - b; byte v = p < buf.Length ? buf[p] : (byte)0; p++;
					int row = wr.Row;
					for (int i = 0; i < n; i++) { wr.Put(v); if (wr.Row != row) break; }
				}
				else
				{
					int n = b + 1, row = wr.Row;
					for (int i = 0; i < n; i++) { byte v = p < buf.Length ? buf[p] : (byte)0; p++; if (wr.Row == row) wr.Put(v); }
				}
			}
			return img;
		}

		// Число кадров по TAB (SurfaceSet::loadPck): первые 4 байта как int32 — не 0
		// (второе 16-битное смещение уже не 0) => смещения 16-битные, иначе 32-битные (TFTD бой).
		public static int TabFrames(byte[] tab)
		{
			if (tab == null || tab.Length < 2) return 1;
			if (tab.Length < 4) return tab.Length >> 1;
			int off32 = tab[0] | tab[1] << 8 | tab[2] << 16 | tab[3] << 24;
			return off32 != 0 ? tab.Length >> 1 : tab.Length >> 2;
		}

		// PCK: кадры подряд; кадр = байт «пропустить строк», далее FE n — n прозрачных,
		// FF — конец кадра, иначе — пиксель.
		public static List<Img> DecodePck(byte[] pck, byte[] tab, int w = 32, int h = 40)
		{
			int n = TabFrames(tab);
			var frames = new List<Img>();
			int p = 0;
			for (int f = 0; f < n; f++)
			{
				var img = new Img(w, h);
				var wr = new PixelWriter(img);
				if (p >= pck.Length) { frames.Add(img); continue; }
				int skip = pck[p++];
				for (int i = 0; i < skip * w; i++) wr.Put(0);
				while (p < pck.Length)
				{
					byte v = pck[p++];
					if (v == 255) break;
					if (v == 254) { int k = p < pck.Length ? pck[p] : 0; p++; for (int i = 0; i < k; i++) wr.Put(0); }
					else wr.Put(v);
				}
				frames.Add(img);
			}
			return frames;
		}

		public static List<Img> DecodeDatSet(byte[] buf, int w, int h)
		{
			int n = buf.Length / (w * h);
			var frames = new List<Img>();
			for (int f = 0; f < n; f++)
			{
				var img = new Img(w, h);
				Array.Copy(buf, f * w * h, img.Px, 0, w * h);
				frames.Add(img);
			}
			return frames;
		}

		// PALETTES.DAT: палитры по 768 + 6 байт, RGB по 6 бит (Palette::palOffset).
		public static List<byte[]> DecodePalettes(byte[] buf)
		{
			var outp = new List<byte[]>();
			for (int off = 0; off + 768 <= buf.Length; off += 774)
			{
				var p = new byte[768];
				Array.Copy(buf, off, p, 0, 768);
				outp.Add(p);
			}
			return outp;
		}

		public static byte[] DecodeBackpals(byte[] buf)
		{
			var p = new byte[384];
			Array.Copy(buf, p, Math.Min(384, buf.Length));
			return p;
		}

		// IFF ILBM / PBM (Deluxe Paint): BMHD, CMAP (8 бит), BODY (ByteRun1 или без сжатия).
		public static (Img img, byte[] pal) DecodeLbm(byte[] buf)
		{
			if (Encoding.ASCII.GetString(buf, 0, 4) != "FORM") throw new InvalidDataException("LBM: no FORM");
			string kind = Encoding.ASCII.GetString(buf, 8, 4);
			int p = 12, bw = 0, bh = 0, planes = 0, comp = 0;
			bool hasBmhd = false;
			byte[] cmap = null, body = null;
			while (p + 8 <= buf.Length)
			{
				string id = Encoding.ASCII.GetString(buf, p, 4);
				int len = buf[p + 4] << 24 | buf[p + 5] << 16 | buf[p + 6] << 8 | buf[p + 7];
				int dlen = Math.Min(len, buf.Length - p - 8);
				var data = new byte[Math.Max(0, dlen)];
				Array.Copy(buf, p + 8, data, 0, data.Length);
				if (id == "BMHD") { bw = data[0] << 8 | data[1]; bh = data[2] << 8 | data[3]; planes = data[8]; comp = data[10]; hasBmhd = true; }
				else if (id == "CMAP") cmap = data;
				else if (id == "BODY") body = data;
				p += 8 + len + (len & 1);
			}
			if (!hasBmhd) throw new InvalidDataException("LBM: no BMHD");
			var pal = new byte[768];
			if (cmap != null) Array.Copy(cmap, pal, Math.Min(768, cmap.Length));
			var img = new Img(bw, bh);
			if (body == null) return (img, pal);

			int rowBytes = kind == "PBM " ? bw + (bw & 1) : ((bw + 15) >> 4) * 2 * planes;
			var raw = new byte[rowBytes * bh];
			if (comp == 1)
			{
				int s = 0, d = 0;
				while (s < body.Length && d < raw.Length)
				{
					int c = body[s++];
					if (c < 128) { for (int i = 0; i <= c && d < raw.Length; i++) raw[d++] = s < body.Length ? body[s++] : (byte)0; }
					else if (c > 128) { byte v = s < body.Length ? body[s++] : (byte)0; for (int i = 0; i < 257 - c && d < raw.Length; i++) raw[d++] = v; }
				}
			}
			else Array.Copy(body, raw, Math.Min(body.Length, raw.Length));

			if (kind == "PBM ")
			{
				for (int y = 0; y < bh; y++) Array.Copy(raw, y * rowBytes, img.Px, y * bw, bw);
			}
			else
			{
				int planeBytes = ((bw + 15) >> 4) * 2;
				for (int y = 0; y < bh; y++)
					for (int x = 0; x < bw; x++)
					{
						int v = 0;
						for (int pl = 0; pl < planes; pl++)
						{
							byte b = raw[y * rowBytes + pl * planeBytes + (x >> 3)];
							if ((b & (0x80 >> (x & 7))) != 0) v |= 1 << pl;
						}
						img.Px[y * bw + x] = (byte)v;
					}
			}
			return (img, pal);
		}
	}
}
