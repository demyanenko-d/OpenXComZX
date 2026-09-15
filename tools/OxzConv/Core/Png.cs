// PNG без внешних зависимостей: запись индексированных картинок (предпросмотр) и
// чтение индексированных (шрифты OpenXcom). .NET 4.8 не имеет ZLibStream — поток
// zlib собирается вручную: заголовок 78 9C + DeflateStream + Adler-32.
using System;
using System.Collections.Generic;
using System.IO;
using System.IO.Compression;
using System.Text;

namespace OxzConv
{
	static class Png
	{
		static readonly uint[] crcTable = MakeCrc();

		static uint[] MakeCrc()
		{
			var t = new uint[256];
			for (uint n = 0; n < 256; n++)
			{
				uint c = n;
				for (int k = 0; k < 8; k++) c = (c & 1) != 0 ? 0xEDB88320 ^ (c >> 1) : c >> 1;
				t[n] = c;
			}
			return t;
		}

		static uint Crc(byte[] b, int off, int len)
		{
			uint c = 0xFFFFFFFF;
			for (int i = off; i < off + len; i++) c = crcTable[(c ^ b[i]) & 0xFF] ^ (c >> 8);
			return c ^ 0xFFFFFFFF;
		}

		static void Be32(Stream s, uint v) { s.WriteByte((byte)(v >> 24)); s.WriteByte((byte)(v >> 16)); s.WriteByte((byte)(v >> 8)); s.WriteByte((byte)v); }

		static void Chunk(Stream s, string type, byte[] data)
		{
			Be32(s, (uint)data.Length);
			var td = new byte[4 + data.Length];
			Encoding.ASCII.GetBytes(type, 0, 4, td, 0);
			Array.Copy(data, 0, td, 4, data.Length);
			s.Write(td, 0, td.Length);
			Be32(s, Crc(td, 0, td.Length));
		}

		public static byte[] Zlib(byte[] raw)
		{
			using (var ms = new MemoryStream())
			{
				ms.WriteByte(0x78); ms.WriteByte(0x9C);
				using (var ds = new DeflateStream(ms, CompressionLevel.Optimal, true)) ds.Write(raw, 0, raw.Length);
				uint a = 1, b = 0;
				foreach (var x in raw) { a = (a + x) % 65521; b = (b + a) % 65521; }
				Be32(ms, b << 16 | a);
				return ms.ToArray();
			}
		}

		public static byte[] Unzlib(byte[] z)
		{
			using (var ms = new MemoryStream(z, 2, z.Length - 2))
			using (var ds = new DeflateStream(ms, CompressionMode.Decompress))
			using (var o = new MemoryStream()) { ds.CopyTo(o); return o.ToArray(); }
		}

		// pal8 — 768 байт, 8 бит на канал.
		public static byte[] EncodeIndexed(Img img, byte[] pal8)
		{
			using (var s = new MemoryStream())
			{
				s.Write(new byte[] { 0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A }, 0, 8);
				var ihdr = new byte[13];
				ihdr[0] = (byte)(img.W >> 24); ihdr[1] = (byte)(img.W >> 16); ihdr[2] = (byte)(img.W >> 8); ihdr[3] = (byte)img.W;
				ihdr[4] = (byte)(img.H >> 24); ihdr[5] = (byte)(img.H >> 16); ihdr[6] = (byte)(img.H >> 8); ihdr[7] = (byte)img.H;
				ihdr[8] = 8; ihdr[9] = 3;
				Chunk(s, "IHDR", ihdr);
				Chunk(s, "PLTE", pal8);
				var raw = new byte[(img.W + 1) * img.H];
				for (int y = 0; y < img.H; y++) Array.Copy(img.Px, y * img.W, raw, y * (img.W + 1) + 1, img.W);
				Chunk(s, "IDAT", Zlib(raw));
				Chunk(s, "IEND", new byte[0]);
				return s.ToArray();
			}
		}

		// Чтение индексированного PNG (1/2/4/8 бит, без чересстрочности).
		public static Img DecodeIndexed(byte[] buf)
		{
			int p = 8, w = 0, h = 0, depth = 0, ctype = 0, inter = 0;
			var idat = new MemoryStream();
			while (p + 8 <= buf.Length)
			{
				int len = buf[p] << 24 | buf[p + 1] << 16 | buf[p + 2] << 8 | buf[p + 3];
				string type = Encoding.ASCII.GetString(buf, p + 4, 4);
				if (type == "IHDR")
				{
					w = buf[p + 8] << 24 | buf[p + 9] << 16 | buf[p + 10] << 8 | buf[p + 11];
					h = buf[p + 12] << 24 | buf[p + 13] << 16 | buf[p + 14] << 8 | buf[p + 15];
					depth = buf[p + 16]; ctype = buf[p + 17]; inter = buf[p + 20];
				}
				else if (type == "IDAT") idat.Write(buf, p + 8, len);
				p += 12 + len;
			}
			if (ctype != 3 || inter != 0) throw new InvalidDataException("PNG: only non-interlaced indexed images supported");
			int stride = (w * depth + 7) / 8;
			var raw = Unzlib(idat.ToArray());
			var img = new Img(w, h);
			var prev = new byte[stride];
			var line = new byte[stride];
			for (int y = 0; y < h; y++)
			{
				int f = raw[y * (stride + 1)];
				Array.Copy(raw, y * (stride + 1) + 1, line, 0, stride);
				for (int i = 0; i < stride; i++)
				{
					int a = i >= 1 ? line[i - 1] : 0, b = prev[i], c = i >= 1 ? prev[i - 1] : 0, v = line[i];
					if (f == 1) v += a;
					else if (f == 2) v += b;
					else if (f == 3) v += (a + b) >> 1;
					else if (f == 4) { int pp = a + b - c, pa = Math.Abs(pp - a), pb = Math.Abs(pp - b), pc = Math.Abs(pp - c); v += pa <= pb && pa <= pc ? a : pb <= pc ? b : c; }
					line[i] = (byte)v;
				}
				for (int x = 0; x < w; x++)
				{
					int bit = x * depth;
					img.Px[y * w + x] = (byte)((line[bit >> 3] >> (8 - depth - (bit & 7))) & ((1 << depth) - 1));
				}
				var t = prev; prev = line; line = t;
			}
			return img;
		}
	}
}
