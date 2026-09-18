// Запись пакета ресурсов *.PAK (формат — project_docs/09_converter.md §5).
// Каждый ресурс начинается с границы сектора 512 байт: движок грузит его
// секторами с SD прямо в страницу (DMA SPI->RAM).
using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text;

namespace OxzConv
{
	enum ResType : byte { Img8 = 1, Sprset = 2, Pal = 3, Font = 4, Str = 5, Table = 6, Music = 7, Blob = 8 }

	class PakWriter
	{
		public const int Sector = 512;
		class Item { public int Id; public ResType Type; public byte Flags; public byte[] Data; public int A, B, C; }

		readonly byte game;
		readonly List<Item> items = new List<Item>();

		public PakWriter(string game) { this.game = (byte)(game == "TFTD" ? 2 : 1); }

		public void Add(int id, ResType type, byte[] data, int a = 0, int b = 0, int c = 0, byte flags = 0)
		{
			if (items.Any(it => it.Id == id)) throw new InvalidDataException("duplicate resource id " + id);
			items.Add(new Item { Id = id, Type = type, Data = data, A = a, B = b, C = c, Flags = flags });
		}

		static void U16(byte[] b, int o, int v) { b[o] = (byte)v; b[o + 1] = (byte)(v >> 8); }
		static void U32(byte[] b, int o, int v) { U16(b, o, v); U16(b, o + 2, v >> 16); }

		public (int count, long bytes) Write(string file)
		{
			int n = items.Count;
			int hdrSectors = (16 + 16 * n + Sector - 1) / Sector;
			var hdr = new byte[hdrSectors * Sector];
			Encoding.ASCII.GetBytes("OXZP").CopyTo(hdr, 0);
			hdr[4] = 1; hdr[5] = game;
			U16(hdr, 6, n); U16(hdr, 8, hdrSectors);
			var sorted = items.OrderBy(it => it.Id).ToList();
			long sector = hdrSectors;
			using (var fs = new FileStream(file, FileMode.Create))
			{
				fs.Write(hdr, 0, hdr.Length);            // заполним повторно в конце
				for (int i = 0; i < sorted.Count; i++)
				{
					var it = sorted[i];
					int e = 16 + i * 16;
					int secs = (it.Data.Length + Sector - 1) / Sector;
					if (sector > 0xFFFF) throw new InvalidDataException("pack too large");
					U16(hdr, e, it.Id); hdr[e + 2] = (byte)it.Type; hdr[e + 3] = it.Flags;
					U16(hdr, e + 4, (int)sector); U32(hdr, e + 6, it.Data.Length);
					U16(hdr, e + 10, it.A); U16(hdr, e + 12, it.B); U16(hdr, e + 14, it.C);
					var padded = new byte[secs * Sector];
					Array.Copy(it.Data, padded, it.Data.Length);
					fs.Write(padded, 0, padded.Length);
					sector += secs;
				}
				fs.Position = 0;
				fs.Write(hdr, 0, hdr.Length);
			}
			return (n, sector * Sector);
		}
	}
}
