// Музыка для чипов без FM: тот же трек, сведённый с двенадцати голосов плеера на три канала
// AY-3-8910 (или шесть — два чипа TurboSound / SSG-часть YM2203). Поток покадровый и в том же
// формате, что у OPL3 (20 §4), поэтому плеер в прерывании остаётся прежним — меняется только
// процедура вывода в порты.
//
// Почему нельзя пересчитывать на лету: в MUSIC.PAK лежат записи в регистры OPL3 (двенадцать
// FM-голосов), а у AY три канала прямоугольника с общей огибающей. Сведение — это выбор, что
// из двенадцати голосов слышно, и оно должно быть сделано заранее.
using System;
using System.Collections.Generic;
using System.Linq;

namespace OxzConv
{
	static class MusicAy
	{
		public const int Clock = 1774400;          // частота AY на TS-Config (02 §7)
		const int Chans = 3;

		// Громкость AY логарифмическая (примерно -3 дБ на ступень), у плеера — линейная 0..127.
		// Таблица переводит одно в другое: тихие ноты не должны пропадать совсем.
		static readonly int[] VolTab = BuildVol();

		static int[] BuildVol()
		{
			var t = new int[128];
			for (int v = 1; v < 128; v++)
			{
				double db = 20 * Math.Log10(v / 127.0);     // -42..0 дБ
				int step = (int)Math.Round(15 + db / 3.0);  // ступень AY = 3 дБ
				t[v] = step < 1 ? 1 : step > 15 ? 15 : step;
			}
			return t;
		}

		class Slot { public int Voice = -1; }

		// Свести кадры голосов в поток записей в регистры AY.
		// voices[f] — состояние 12 голосов плеера на кадр (Music.VoiceFrame).
		public static (byte[] data, int maxWrites, int loopOffset) Encode(List<Music.VoiceFrame> voices, bool loop)
		{
			var slots = Enumerable.Range(0, Chans).Select(_ => new Slot()).ToArray();
			var last = Enumerable.Repeat(-1, 16).ToArray();
			var o = new List<byte>();
			int idle = 0, maxw = 0, loopOffset = -1;
			int[] loopState = null;                     // состояние регистров в точке повтора
			var cur = new List<byte>();

			void Reg(int r, int v)
			{
				if (last[r] == v) return;
				last[r] = v;
				cur.Add((byte)r); cur.Add((byte)v);
			}
			void FlushIdle() { while (idle > 0) { int k = Math.Min(idle, 126); o.Add((byte)(0x80 + k)); idle -= k; } }

			for (int f = 0; f < voices.Count; f++)
			{
				// Повтор — как у потока OPL3 (22 §2.3): кадр 0 ставит чип в исходное состояние,
				// тело трека начинается с кадра 1, туда же возвращается плеер.
				if (f == 1 && loop) { FlushIdle(); loopOffset = o.Count; loopState = (int[])last.Clone(); }
				var freq = voices[f].Freq; var vol = voices[f].Vol;
				// Кого слышно: берём самые громкие голоса, но держим уже звучащие на своих
				// каналах — иначе мелодия прыгает между каналами и слышны щелчки.
				var order = Enumerable.Range(0, 12).Where(i => vol[i] > 0 && freq[i] > 0)
					.OrderByDescending(i => vol[i]).ToList();
				var taken = new bool[12];
				foreach (var s in slots)
				{
					if (s.Voice >= 0 && order.Contains(s.Voice)) { taken[s.Voice] = true; continue; }
					s.Voice = -1;
				}
				foreach (var s in slots)
				{
					if (s.Voice >= 0) continue;
					int pick = -1;
					foreach (var i in order) if (!taken[i]) { pick = i; break; }
					if (pick < 0) continue;
					s.Voice = pick; taken[pick] = true;
				}
				cur.Clear();
				int mixer = 0;
				for (int c = 0; c < Chans; c++)
				{
					var s = slots[c];
					if (s.Voice < 0 || vol[s.Voice] == 0)
					{
						Reg(8 + c, 0);
						mixer |= 1 << c;                    // канал тона выключен
						continue;
					}
					int hz100 = freq[s.Voice];
					int period = hz100 > 0 ? (int)(100L * Clock / (16L * hz100)) : 0;
					if (period < 1) period = 1;
					if (period > 4095) period = 4095;
					Reg(c * 2, period & 0xFF);
					Reg(c * 2 + 1, period >> 8);
					Reg(8 + c, VolTab[vol[s.Voice] > 127 ? 127 : vol[s.Voice]]);
				}
				Reg(7, mixer | 0x38);                       // шум выключен, тоны по маске
				int n = cur.Count / 2;
				maxw = Math.Max(maxw, n);
				if (n == 0) { idle++; continue; }
				FlushIdle();
				for (int i = 0; i < n;)
				{
					int k = Math.Min(127, n - i);
					o.Add((byte)k); o.AddRange(cur.GetRange(i * 2, k * 2));
					o.Add(0);                               // второй банк не используется
					i += k;
				}
			}
			FlushIdle();
			// Возврат состояния к точке повтора: за круг регистры разошлись с тем, какими были
			// в кадре 1, и без этого второй круг звучит иначе первого (у OPL3 то же делает блок
			// ресинка, 22 §2.4). Здесь это обычный кадр в конце потока — плеер его просто играет.
			if (loopState != null)
			{
				cur.Clear();
				for (int r0 = 0; r0 < 16; r0++)
					if (loopState[r0] >= 0 && loopState[r0] != last[r0]) { last[r0] = loopState[r0]; cur.Add((byte)r0); cur.Add((byte)loopState[r0]); }
				int m = cur.Count / 2;
				if (m > 0)
				{
					maxw = Math.Max(maxw, m);
					o.Add((byte)m); o.AddRange(cur); o.Add(0);
				}
			}
			o.Add(0xFF);
			var hdr = new byte[12];
			BitConverter.GetBytes(loopOffset >= 0 ? (uint)loopOffset : 0xFFFFFFFF).CopyTo(hdr, 0);
			BitConverter.GetBytes(voices.Count).CopyTo(hdr, 4);
			hdr[8] = (byte)maxw; hdr[9] = (byte)(maxw >> 8);
			return (hdr.Concat(o).ToArray(), maxw, loopOffset);
		}
	}
}
