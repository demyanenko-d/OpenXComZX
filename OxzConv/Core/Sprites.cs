// Картинки под железо TS-Config: 8 бит/пиксель, чётная ширина (DMA — словами),
// обрезка по непрозрачным пикселям (0 — прозрачный, DMA BLT1).
using System;
using System.Collections.Generic;
using System.IO;

namespace OxzConv
{
	static class Sprites
	{
		// Габариты непрозрачных пикселей; x и ширина выровнены на 2. null — пустой кадр.
		public static int[] OpaqueBox(Img img)
		{
			int x0 = img.W, y0 = img.H, x1 = -1, y1 = -1;
			for (int y = 0; y < img.H; y++)
				for (int x = 0; x < img.W; x++)
					if (img.Px[y * img.W + x] != 0)
					{
						if (x < x0) x0 = x;
						if (x > x1) x1 = x;
						if (y < y0) y0 = y;
						if (y > y1) y1 = y;
					}
			if (x1 < 0) return null;
			x0 &= ~1;
			int w = x1 - x0 + 1;
			if ((w & 1) != 0) w++;
			if (x0 + w > img.W) x0 = img.W - w;
			return new[] { x0, y0, w, y1 - y0 + 1 };
		}

		public static byte[] Crop(Img img, int[] box)
		{
			var o = new byte[box[2] * box[3]];
			for (int y = 0; y < box[3]; y++)
				Array.Copy(img.Px, (box[1] + y) * img.W + box[0], o, y * box[2], box[2]);
			return o;
		}

		public static byte[] EncodeImg8(Img img)
		{
			if ((img.W & 1) != 0) throw new InvalidDataException("IMG8 width must be even");
			return (byte[])img.Px.Clone();
		}

		// SPRSET: u16 n, u8 cell_w, u8 cell_h; n x { u8 x, u8 y, u8 w, u8 h, u16 offs/2 };
		// затем данные кадров (w*h байт, w чётная). Пустой кадр — w = 0.
		public static byte[] EncodeSprset(List<Img> frames)
		{
			int n = frames.Count, cw = frames[0].W, ch = frames[0].H;
			if (cw > 255 || ch > 255) throw new InvalidDataException("SPRSET cell too big");
			var hdr = new byte[4 + n * 6];
			hdr[0] = (byte)n; hdr[1] = (byte)(n >> 8); hdr[2] = (byte)cw; hdr[3] = (byte)ch;
			var data = new MemoryStream();
			for (int i = 0; i < n; i++)
			{
				var box = OpaqueBox(frames[i]);
				int e = 4 + i * 6;
				if (box == null) continue;
				var d = Crop(frames[i], box);
				int offs = (int)data.Length;
				if (offs / 2 > 0xFFFF) throw new InvalidDataException("SPRSET too large");
				hdr[e] = (byte)box[0]; hdr[e + 1] = (byte)box[1]; hdr[e + 2] = (byte)box[2]; hdr[e + 3] = (byte)box[3];
				hdr[e + 4] = (byte)(offs / 2); hdr[e + 5] = (byte)(offs / 2 >> 8);
				data.Write(d, 0, d.Length);
			}
			var o = new byte[hdr.Length + data.Length];
			Array.Copy(hdr, o, hdr.Length);
			data.ToArray().CopyTo(o, hdr.Length);
			return o;
		}

		// Все кадры в одну картинку-лист (для предпросмотра).
		public static Img Sheet(List<Img> frames, int cols = 16)
		{
			int cw = frames[0].W, ch = frames[0].H, rows = (frames.Count + cols - 1) / cols;
			var img = new Img(cw * Math.Min(cols, frames.Count), ch * rows);
			for (int i = 0; i < frames.Count; i++)
			{
				int ox = i % cols * cw, oy = i / cols * ch;
				for (int y = 0; y < ch; y++) Array.Copy(frames[i].Px, y * cw, img.Px, (oy + y) * img.W + ox, cw);
			}
			return img;
		}
	}
}
