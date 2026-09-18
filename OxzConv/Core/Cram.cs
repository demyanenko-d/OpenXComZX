// Палитры: 6-битный VGA (оригинал) -> слова CRAM TS-Config и 8 бит (предпросмотр).
// CRAM: 0RRRRRGG GGGBBBBB, 5 бит на канал: round(v6 * 31 / 63). Пересчёт под ШИМ-ЦАП
// стокового ZX-Evo (уровни 0..24) делает движок при загрузке (02 §4).
using System;

namespace OxzConv
{
	static class Cram
	{
		static int To5(int v6) => (int)Math.Round((v6 & 63) * 31 / 63.0, MidpointRounding.AwayFromZero);

		public static byte[] Pal6ToCram(byte[] pal6)
		{
			var o = new byte[512];
			for (int i = 0; i < 256 && i * 3 + 2 < pal6.Length; i++)
			{
				int w = To5(pal6[i * 3]) << 10 | To5(pal6[i * 3 + 1]) << 5 | To5(pal6[i * 3 + 2]);
				o[i * 2] = (byte)w; o[i * 2 + 1] = (byte)(w >> 8);
			}
			return o;
		}

		public static byte[] Pal6To8(byte[] pal6)
		{
			var o = new byte[768];
			for (int i = 0; i < 768 && i < pal6.Length; i++) o[i] = (byte)Math.Round((pal6[i] & 63) * 255 / 63.0, MidpointRounding.AwayFromZero);
			return o;
		}

		public static byte[] Pal8To6(byte[] pal8)
		{
			var o = new byte[768];
			for (int i = 0; i < 768 && i < pal8.Length; i++) o[i] = (byte)(pal8[i] >> 2);
			return o;
		}
	}
}
