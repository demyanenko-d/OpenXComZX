// Музыка для режима «2 x YM2203» (11_sound.md, вариант 4): тот же трек, сведённый с
// двенадцати FM-голосов плеера ADLIB на шесть FM-каналов двух YM2203. SSG-части обоих
// чипов не трогаем вовсе — они остаются эффектам, как AY в режиме с OPL3.
//
// Поток покадровый и того же формата, что у OPL3 (22 §2): «банк 0» — записи в чип 0,
// «банк 1» — в чип 1. Плеер в прерывании остаётся прежним, меняется лишь процедура
// вывода (mus_out_ym в src/kernel/win0/music_s.s): байт выбора чипа в #FFFD, затем пары
// «регистр, значение» через #FFFD/#BFFD.
//
// Перенос тембра. У ADLIB патч — два оператора OPL2 (24 байта на сэмпл), у OPN четыре.
// Берём алгоритм 4 (две параллельные пары: S1->S2 и S3->S4, выход S2+S4) и пользуемся
// только первой парой: S1 — модулятор (на нём же обратная связь, как в OPL), S2 —
// несущая. S3/S4 не включаются key-on'ом и потому молчат. Для аддитивного патча
// (connection = 1, оба оператора слышны) берём алгоритм 7 — там все четыре параллельны,
// и наши S1+S2 звучат вместе.
//
// Чего у YM2203 нет: KSL, формы волны кроме синуса, LFO (тремоло/вибрато). Эти поля
// патча отбрасываются. «Перкуссионный» режим OPL (EGT = 0, нота сама затухает) даётся
// вторым спадом OPN: SR = RR, у выдержанного (EGT = 1) SR = 0.
using System;
using System.Collections.Generic;
using System.Linq;

namespace OxzConv
{
	static class MusicFm
	{
		// Тактовая FM-части на TurboSound FM — вдвое выше SSG (02 §7); в эмуляторе
		// Chip2203->OPN.ST.clock = conf.sound.ayfq * 2 (sndchip.cpp:214).
		public const int Clock = 3548800;
		const int Chips = 2, Chans = 6;

		// RelAge — сколько кадров канал молчит после key-off. Новую ноту отдаём тому каналу,
		// который молчит дольше: снятая нота на OPL ещё доигрывает затухание, и если сразу
		// занять её канал, звук обрывается там, где в оригинале он ещё слышен.
		class Slot { public int Voice = -1, Trig = -1, Patch = -1, RelAge = 9999; public bool On; }

		// F-number OPN: f = fnum * clock * 2^(block-1) / (72 * 2^20), где 72 — делитель
		// прескалера с учётом разделения времени между операторами (emul_2203.cpp:1446).
		public static (int fnum, int block) Fnum(int hz100)
		{
			if (hz100 <= 0) return (0, 0);
			for (int block = 0; block < 8; block++)
			{
				long fn = (long)hz100 * 72 * (1L << 21) / ((long)Clock * 100 * (1L << block));
				if (fn <= 2047) return ((int)(fn < 1 ? 1 : fn), block);
			}
			return (2047, 7);
		}

		static int Rate(int r4) => r4 >= 15 ? 31 : r4 * 2;      // 4 бита OPL -> 5 бит OPN

		public static (byte[] data, int maxWrites, int loopOffset) Encode(List<Music.VoiceFrame> voices, List<byte[]> patches, bool loop)
		{
			var slots = Enumerable.Range(0, Chans).Select(_ => new Slot()).ToArray();
			var last = new[] { Enumerable.Repeat(-1, 256).ToArray(), Enumerable.Repeat(-1, 256).ToArray() };
			var cur = new[] { new List<byte>(), new List<byte>() };
			var o = new List<byte>();
			int idle = 0, maxw = 0, loopOffset = -1;

			void Reg(int chip, int rg, int val)
			{
				if (last[chip][rg] == val) return;
				last[chip][rg] = val;
				cur[chip].Add((byte)rg); cur[chip].Add((byte)val);
			}
			// key on/off (#28) и частота дедупликации не подлежат: #28 — событие, а старший
			// байт частоты (#A4) чип держит в одной защёлке на все каналы и применяет только
			// при записи #A0 — значит пару надо писать целиком или не писать вовсе.
			void KeyOn(int chip, int ch, bool on) { cur[chip].Add(0x28); cur[chip].Add((byte)(on ? 0x30 | ch : ch)); }
			void Freq(int chip, int ch, int hz100)
			{
				var (fn, bl) = Fnum(hz100);
				int hi = (bl << 3) | (fn >> 8), lo = fn & 0xFF;
				if (last[chip][0xA4 + ch] == hi && last[chip][0xA0 + ch] == lo) return;
				last[chip][0xA4 + ch] = hi; last[chip][0xA0 + ch] = lo;
				cur[chip].Add((byte)(0xA4 + ch)); cur[chip].Add((byte)hi);
				cur[chip].Add((byte)(0xA0 + ch)); cur[chip].Add((byte)lo);
			}
			// Оператор OPL (байты патча) -> четвёрка регистров OPN. off: 0 — S1, 8 — S2
			// (порядок регистров OPN — S1, S3, S2, S4 с шагом 4).
			void Oper(int chip, int ch, int off, int ctl, int adsr, int slrr, int tl)
			{
				Reg(chip, 0x30 + off + ch, ctl & 0x0F);                      // DT = 0, MULT как в OPL
				// TL переносится один к одному: шаг у OPL2 и OPN одинаковый, 0.75 дБ. Удвоение
				// (была такая попытка) вдвое растягивает затухание в децибелах — тихие ноты
				// пропадают, а громкие после компенсации перегружают сумму каналов и хрипят.
				Reg(chip, 0x40 + off + ch, tl > 127 ? 127 : tl);
				Reg(chip, 0x50 + off + ch, ((ctl & 0x10) != 0 ? 0x80 : 0) | Rate(adsr >> 4));
				Reg(chip, 0x60 + off + ch, Rate(adsr & 0x0F));               // AM нет (нет LFO)
				Reg(chip, 0x70 + off + ch, (ctl & 0x20) != 0 ? 0 : Rate(slrr & 0x0F));
				Reg(chip, 0x80 + off + ch, slrr);                            // SL и RR совпадают по смыслу
				Reg(chip, 0x90 + off + ch, 0);                               // SSG-EG выключен
			}

			void FlushIdle() { while (idle > 0) { int k = Math.Min(idle, 126); o.Add((byte)(0x80 + k)); idle -= k; } }
			void FlushFrame()
			{
				int n0 = cur[0].Count / 2, n1 = cur[1].Count / 2;
				maxw = Math.Max(maxw, n0 + n1);
				if (n0 == 0 && n1 == 0) { idle++; return; }
				FlushIdle();
				for (int i = 0, j = 0; i < n0 || j < n1;)
				{
					int k0 = Math.Min(127, n0 - i), k1 = Math.Min(255, n1 - j);
					o.Add((byte)k0); o.AddRange(cur[0].GetRange(i * 2, k0 * 2));
					o.Add((byte)k1); o.AddRange(cur[1].GetRange(j * 2, k1 * 2));
					i += k0; j += k1;
				}
			}

			for (int f = 0; f < voices.Count; f++)
			{
				// Как и у OPL3 (22 §2.3): кадр 0 ставит чипы в исходное состояние, тело трека
				// начинается с кадра 1 — туда и возвращается плеер по loop_offset.
				// Точка повтора: забываем состояние регистров — кадр 1 выпишет всё сам, и после прыжка
				// на loop_offset чипы получают верные значения из потока. Кадр глушения в конце,
				// который для этого стоял раньше, обрывал звук на стыке круга (22 §10.11).
				if (f == 1 && loop) { FlushIdle(); loopOffset = o.Count; for (int chip = 0; chip < Chips; chip++) for (int r0 = 0; r0 < 256; r0++) last[chip][r0] = -1; }
				cur[0].Clear(); cur[1].Clear();
				if (f == 0)
					for (int chip = 0; chip < Chips; chip++)
					{
						Reg(chip, 0x27, 0x00);                               // таймеры и спецрежим выключены
						for (int ch = 0; ch < 3; ch++) KeyOn(chip, ch, false);
					}

				var v = voices[f];
				// Хорус (расстроенный дубль, добавка OpenXcom) берём последним, а унисон не
				// пускаем вовсе: две почти одинаковые ноты съедали по два канала из шести
				// и давали биения — на FM это слышно как хрип (22 §10.6).
				var order = new List<int>();
				foreach (var i in Enumerable.Range(0, 12).Where(k => v.Vol[k] > 0 && v.Freq[k] > 0 && v.Patch[k] >= 0)
					.OrderByDescending(k => v.Vol[k] - (v.Chorus[k] ? 48 : 0)))
				{
					if (order.Any(j => Math.Abs((double)v.Freq[j] / v.Freq[i] - 1) < 0.03)) continue;
					order.Add(i);
					if (order.Count == Chans) break;
				}
				var taken = new bool[12];
				foreach (var s in slots)
				{
					if (s.Voice >= 0 && order.Contains(s.Voice)) { taken[s.Voice] = true; continue; }
					s.Voice = -1;
				}
				foreach (var s in slots) if (s.Voice < 0) s.RelAge++;
				foreach (var s in slots.Where(x => x.Voice < 0).OrderByDescending(x => x.RelAge))
				{
					int pick = -1;
					foreach (var i in order) if (!taken[i]) { pick = i; break; }
					if (pick < 0) continue;
					s.Voice = pick; taken[pick] = true;
				}

				for (int c = 0; c < Chans; c++)
				{
					var s = slots[c];
					int chip = c / 3, ch = c % 3;
					if (s.Voice < 0)
					{
						if (s.On) { KeyOn(chip, ch, false); s.On = false; s.Trig = -1; s.RelAge = 0; }
						continue;
					}
					int i = s.Voice, pi = v.Patch[i];
					byte[] pt = pi < patches.Count ? patches[pi] : null;
					if (pt == null) continue;
					bool fresh = !s.On || s.Trig != v.Trig[i] || s.Patch != pi;
					int con = (pt[10] ^ 0x01) & 1, fb = (pt[10] ^ 0x01) >> 1 & 7;
					int tlCar = 63 - ((127 * (v.Vol[i] > 127 ? 127 : v.Vol[i])) >> 8);
					if (s.Patch != pi)
					{
						Reg(chip, 0xB0 + ch, (fb << 3) | (con != 0 ? 7 : 4));
						Oper(chip, ch, 0, pt[0], pt[4], pt[6], (~pt[2]) & 0x3F);         // S1 — модулятор
						s.Patch = pi;
					}
					Oper(chip, ch, 8, pt[1], pt[5], pt[7], tlCar);                      // S2 — несущая
					Freq(chip, ch, v.Freq[i]);
					if (fresh)
					{
						if (s.On) KeyOn(chip, ch, false);
						KeyOn(chip, ch, true);
						s.On = true; s.Trig = v.Trig[i];
					}
				}
				FlushFrame();
			}
			FlushIdle();
			o.Add(0xFF);
			var hdr = new byte[12];
			BitConverter.GetBytes(loopOffset >= 0 ? (uint)loopOffset : 0xFFFFFFFF).CopyTo(hdr, 0);
			BitConverter.GetBytes(voices.Count).CopyTo(hdr, 4);
			hdr[8] = (byte)maxw; hdr[9] = (byte)(maxw >> 8);
			return (hdr.Concat(o).ToArray(), maxw, loopOffset);
		}
	}
}
