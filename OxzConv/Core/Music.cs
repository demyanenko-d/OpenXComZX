// Музыка ADLIB.CAT -> покадровый поток записей в OPL3 (ресурс MUSIC) и VGM.
//
// Оригинальный плеер (OpenXcom src/Engine/Adlib/adlplayer.cpp, «X-COM Adlib Player
// by Volutar») выполняется офлайн с родным тиком 44100/629 = 70.11 Гц; записи в
// регистры раскладываются по кадрам TS-Config (3 500 000 / 71 680 = 48.83 Гц):
// движку остаётся раз в кадр вывести готовые пары «регистр, значение».
// 12 голосов плеера: 0-8 -> OPL3 банк 0, 9-11 -> банк 1 каналы 0-2 (операторы
// смещений 24-29 -> 0-5 банка 1). Режим OPL3 (NEW=1), в C0-C8 биты L+R.
//
// MUSIC: u32 loop_offset (от начала потока, #FFFFFFFF — без повтора), u32 кадров,
// u16 макс. записей за кадр, u16 0; записи: #00..#7F — n0 пар (рег, знач) банка 0,
// байт n1 и n1 пар банка 1, конец кадра; #81..#FE — пауза (b - #80) кадров; #FF — конец.
using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text;

namespace OxzConv
{
	static class Music
	{
		public const double TickHz = 44100.0 / 629;
		public const double FrameHz = 3500000.0 / 71680;

		static readonly int[] Freq1 = { 0x0B5, 0x0C0, 0x0CC, 0x0D8, 0x0E5, 0x0F2, 0x101, 0x110, 0x120, 0x131, 0x143, 0x157 };
		static readonly int[] Freq2 = { 0x16B, 0x181, 0x198, 0x1B0, 0x1CA, 0x1E5, 0x202, 0x220, 0x241, 0x263, 0x287, 0x2AE };
		static readonly int[] FreqTable = Freq1.Concat(Enumerable.Repeat(Freq2, 8).SelectMany(x => x)).ToArray();
		static readonly int[] OctaveTable = Enumerable.Repeat(0, 24).Concat(Enumerable.Range(1, 7).SelectMany(o => Enumerable.Repeat(o, 12))).ToArray();
		static readonly int[] DetuneTable = Enumerable.Repeat(new[] { 3, 3, 3, 3, 4, 4, 4, 4, 4, 5, 5, 5 }, 9).SelectMany(x => x).ToArray();
		static readonly int[] InstrOrder = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 10, 11, 12, 13, 14, 15, 9 };
		static readonly int[] Ops1 = { 0, 1, 2, 8, 9, 10, 16, 17, 18, 24, 25, 26 };

		public static List<(int offset, int size)> CatEntries(byte[] buf)
		{
			int n = BitConverter.ToInt32(buf, 0) / 8;
			var o = new List<(int, int)>();
			for (int i = 0; i < n; i++) o.Add((BitConverter.ToInt32(buf, i * 8), BitConverter.ToInt32(buf, i * 8 + 4)));
			return o;
		}

		// Данные трека как у AdlibMusic::load(adlibcat->load(track, true), size).
		public static byte[] CatTrack(byte[] buf, (int offset, int size) e)
		{
			int size = e.size;
			if (buf[e.offset] <= 56) size += buf[e.offset] + 1;
			size = Math.Min(size, buf.Length - e.offset);
			var d = new byte[size];
			Array.Copy(buf, e.offset, d, 0, size);
			return d;
		}

		class Chan { public int Note, Instr, Sample = 0xFF, Freq, HiFreq, Volume, Duration; }
		class Ins { public int Sample, Prev, Volume, Pitch, Delay, Addr = -1, Start = -1, Ret = -1; }

		class Player
		{
			readonly byte[] d; readonly Action<int, int> reg;
			readonly Chan[] ch = Enumerable.Range(0, 12).Select(_ => new Chan()).ToArray();
			readonly Ins[] ins = Enumerable.Range(0, 16).Select(_ => new Ins()).ToArray();
			readonly int[] chorus = new int[16];
			readonly List<int> subtracks = new List<int>();
			int vol = 127, tempo = 120, tempoRun = 60, tempoInc = 70, samples;
			public bool Playing, Looped;

			public Player(byte[] data, Action<int, int> reg) { d = data; this.reg = reg; }

			int D(int p) => p >= 0 && p < d.Length ? d[p] : 0;
			int U16(int p) => D(p) | D(p + 1) << 8;
			int NumSeq(Ins I) { int v = 0, c; do { c = D(I.Addr++); v = (v << 7) + (c & 0x7F); } while ((c & 0x80) != 0); return v; }
			void SetAmp(int c, int value) => reg(0x43 + Ops1[c], ~(value >> 1) & 0x3F);
			void ClearChannels() { foreach (var c in ch) { c.Sample = 0xFF; c.Note = 0; } }
			void ResetChannels() { ClearChannels(); for (int i = 0; i < 12; i++) { reg(0xB0 + i, 0); SetAmp(i, 0); } }

			int At(int[] t, int i, int def) => i >= 0 && i < t.Length ? t[i] : def;
			int PitchedFreq(int note, int instr)
			{
				int p = ins[instr].Pitch, bas = At(FreqTable, note, 0);
				if (p == 0) return bas;
				if (p > 0) return bas + At(DetuneTable, note, 3) * p;
				return bas + (note > 0 ? At(DetuneTable, note - 1, 3) : 7) * p;
			}

			void SetPitch(int instr, int pitch)
			{
				ins[instr].Pitch = pitch;
				for (int i = 0; i < 12; i++)
				{
					var c = ch[i];
					if (c.Note != 0 && c.Instr == instr)
					{
						int f = PitchedFreq(c.Note, instr);
						c.Freq = f & 0xFF;
						reg(0xA0 + i, f & 0xFF);
						int hf = ((f >> 8) & 3 | At(OctaveTable, c.Note, 0) << 2) & 0xFF;
						c.HiFreq = hf;
						reg(0xB0 + i, hf | 0x20);
					}
				}
			}

			(int c, bool same) UnusedChannel(int sample)
			{
				int maxchan = 0, maxdur = 0;
				foreach (var c in ch) c.Duration++;
				for (int i = 0; i < 12; i++)
				{
					if (ch[i].Duration > maxdur) { maxdur = ch[i].Duration; maxchan = i; }
					if (ch[i].Note == 0) { maxchan = i; break; }
				}
				bool same = false;
				if (ch[maxchan].Sample == sample) same = true; else ch[maxchan].Sample = sample;
				ch[maxchan].Duration = 0;
				return (maxchan, same);
			}

			void PlayNote(int note, int volume, int instr)
			{
				int sample = ins[instr].Sample, s = samples + sample * 24;
				note = (note - 1) & 0xFF;
				if (volume == 0)
				{
					for (int i = 0; i < 12; i++)
						if (ch[i].Note == note && ch[i].Instr == instr) { ch[i].Note = 0; reg(0xB0 + i, ch[i].HiFreq); }
					return;
				}
				if (volume > 127) volume = 127;
				var (c, same) = UnusedChannel(sample);
				var cc = ch[c]; int op1 = Ops1[c];
				cc.Volume = volume; cc.Note = note; cc.Instr = instr;
				if (!same)
				{
					reg(0x20 + op1, D(s)); reg(0x23 + op1, D(s + 1));
					int ampl = D(s + 2);
					reg(0x40 + op1, (~ampl & 0x3F) | (ampl & 0xC0));
				}
				reg(0xB0 + c, cc.HiFreq);
				reg(0x43 + op1, ~((vol * volume) >> 8) & 0x3F);
				if (!same)
				{
					reg(0x60 + op1, D(s + 4)); reg(0x63 + op1, D(s + 5));
					reg(0x80 + op1, D(s + 6)); reg(0x83 + op1, D(s + 7));
					reg(0xE0 + op1, D(s + 8)); reg(0xE3 + op1, D(s + 9));
					reg(0xC0 + c, D(s + 10) ^ 0x01);
				}
				int f = PitchedFreq(note, instr);
				cc.Freq = f & 0xFF;
				reg(0xA0 + c, f & 0xFF);
				int hf = (f >> 8 | At(OctaveTable, note, 0) << 2) & 0xFF;
				cc.HiFreq = hf;
				reg(0xB0 + c, hf | 0x20);
			}

			bool FreeChannel() => ch.Any(c => c.Note == 0);

			void NoteOff(int note, int i)
			{
				PlayNote(note, 0, i);
				if (chorus[i] != 0 && chorus[i] < 16) PlayNote(note, 0, chorus[i]);
			}

			int Decode(int i, ref bool loop)
			{
				var I = ins[i];
				int delay = 0;
				do
				{
					int op = D(I.Addr++);
					if (op == 0xFE) { int a = D(I.Addr++); I.Ret = I.Addr; I.Addr = a < subtracks.Count ? subtracks[a] : d.Length; }
					else if (op == 0xFD) { if (I.Ret >= 0) { I.Addr = I.Ret; I.Ret = -1; } }
					else if (op == 0xFF) { Playing = false; delay = 0; break; }
					else if (op >= 0x80) { I.Prev = op; op = D(I.Addr++); }
					if (op < 0x80)
					{
						int a1 = op;
						switch (I.Prev & 0xF0)
						{
						case 0x80: I.Addr++; NoteOff(a1, i); break;
						case 0x90:
							{
								int a2 = D(I.Addr++);
								if (a2 == 0) NoteOff(a1, i);
								else
								{
									int v = (a2 * I.Volume) >> 7, ci = chorus[i];
									if (ci != 0 && ci < 16 && FreeChannel())
									{
										ins[ci].Sample = I.Sample;
										ins[ci].Pitch = I.Pitch - 1;
										PlayNote(a1, v, ci);
									}
									PlayNote(a1, v, i);
								}
								break;
							}
						case 0xB0:
							{
								int a2 = D(I.Addr++);
								if (a1 == 0 && a2 != 0) tempo = (int)(a2 * 0.8);
								else if (a1 == 7) I.Volume = a2;
								else if (a1 == 0x7E) chorus[i] = (a2 - 1) & 0xFF;
								else if (a1 == 0x7F) chorus[i] = 0;
								break;
							}
						case 0xC0:
							if (a1 == 0x7E) loop = true; else I.Sample = a1;
							break;
						case 0xE0:
							I.Pitch = a1 - 16;
							SetPitch(i, a1 - 16);
							if (chorus[i] != 0 && chorus[i] < 16) SetPitch(chorus[i], a1 - 17);
							break;
						}
					}
					if (!Playing) break;
					int vv = 0, c;
					do { c = D(I.Addr++); vv = (vv << 7) + (c & 0x7F); } while ((c & 0x80) != 0);
					delay = vv;
				} while (delay == 0);
				return delay;
			}

			void InitData()
			{
				int len = d.Length, p = 0;
				foreach (var I in ins) I.Start = -1;
				int fmt = d[0] > 56 ? 0 : 1;
				if (fmt == 1) p += d[0] + 1;
				tempo = D(p++);
				samples = p + 1;
				p += D(p) * 24 + 1;
				int nsub = D(p++);
				for (int i = 0; i < nsub; i++) { int add = U16(p); subtracks.Add(p + 4); p += add; }
				int nins = D(p++);
				for (int i = 0; i < nins; i++)
				{
					int add = U16(p);
					if (fmt == 1) { int j = D(p + 4); if (j > 15) j = 15; ins[j].Start = p + 5; }
					else if (i < 16) ins[i].Start = p + 4;
					p += add;
					if (p >= len) break;
				}
			}

			void InitMusic()
			{
				for (int i = 0; i < 16; i++)
				{
					var I = ins[i];
					I.Pitch = 0; chorus[i] = 0;
					if (I.Start >= 0) { I.Addr = I.Start; I.Delay = NumSeq(I); }
					else { I.Addr = -1; I.Delay = 0; }
				}
			}

			void AdlibInit()
			{
				for (int i = 1; i < 0xF5; i++) reg(i, 0);
				reg(0x04, 0x60); reg(0x04, 0x80); reg(0x01, 0x20);
				reg(0xA8, 0x01); reg(0x08, 0x40); reg(0xBD, 0xC0);
			}

			public void Setup()
			{
				Playing = false;
				InitData();
				InitMusic();
				AdlibInit();
				ResetChannels();
				tempo = (int)(tempo * 0.4);
				tempoRun = tempo;
				Playing = true;
				for (int i = 0; i < 12; i++) SetAmp(i, (ch[i].Volume * vol) >> 7);
			}

			public void Tick()
			{
				if (!Playing) return;
				tempoRun -= tempo;
				if (tempoRun > 0) return;
				tempoRun += tempoInc;
				bool loop;
				do
				{
					loop = false;
					for (int k = 0; k < 16; k++)
					{
						int i = InstrOrder[k]; var I = ins[i];
						if (I.Addr < 0) continue;
						if (I.Delay == 0) { I.Delay = Decode(i, ref loop); if (!Playing) break; }
						I.Delay--;
					}
					if (!loop && Playing) break;
					Looped = true;
					InitMusic();
					ClearChannels();
				} while (loop);
			}
		}

		// Регистр 12-голосного «OPL2» плеера -> (банк, регистр OPL3) или null.
		static (int bank, int reg)? MapReg(int r)
		{
			if (r == 0xBD || r < 0x20) return (0, r);
			int hi = r & 0xF0;
			if (hi == 0xA0 || hi == 0xB0 || hi == 0xC0)
			{
				int c = r & 0x0F;
				if (c < 9) return (0, r);
				if (c < 12) return (1, hi | (c - 9));
				return null;
			}
			if (r >= 0x20 && r < 0xA0 || r >= 0xE0 && r < 0x100)
			{
				int bas = r & 0xE0, off = r & 0x1F;
				if (off <= 0x15) return (0, r);
				if (off >= 0x18 && off <= 0x1D) return (1, bas | (off - 0x18));
				return null;
			}
			return null;
		}

		// Frames — по кадру на 1/50 с (пары «регистр, значение» отдельно для банка 0 и 1).
		// Init/End — состояние регистров после первого кадра и в конце потока: их разница и есть
		// блок ресинка, без которого второй круг звучит иначе первого (22 §2.4).
		public class Render
		{
			public List<List<byte>[]> Frames = new List<List<byte>[]>();
			public bool Loop;
			public int[][] Init, End;
			public double Seconds => Frames.Count / FrameHz;
		}

		public static Render RenderTrack(byte[] data, double maxSeconds = 1200)
		{
			var r = new Render();
			int firstLoopFrame = -1;
			var last = new[] { Enumerable.Repeat(-1, 256).ToArray(), Enumerable.Repeat(-1, 256).ToArray() };
			List<byte>[] cur = null;
			void Emit(int bank, int rg, int val)
			{
				if (bank == 0 && (rg & 0xF0) == 0xC0 && (rg & 0x0F) < 9 || bank == 1 && (rg & 0xF0) == 0xC0) val |= 0x30;
				if (last[bank][rg] == val) return;
				last[bank][rg] = val;
				cur[bank].Add((byte)rg); cur[bank].Add((byte)val);
			}
			var p = new Player(data, (rg, v) => { var m = MapReg(rg & 0xFF); if (m.HasValue) Emit(m.Value.bank, m.Value.reg, v & 0xFF); });
			cur = new[] { new List<byte>(), new List<byte>() };
			r.Frames.Add(cur);
			Emit(1, 0x05, 0x01);
			Emit(1, 0x04, 0x00);
			p.Setup();
			int maxTicks = (int)Math.Floor(maxSeconds * TickHz);
			int loopTick = -1;
			int[][] init = null, end = null;
			for (int k = 1; k <= maxTicks && p.Playing; k++)
			{
				int f = (int)Math.Floor(k * FrameHz / TickHz);
				while (r.Frames.Count <= f) r.Frames.Add(new[] { new List<byte>(), new List<byte>() });
				if (init == null && f > 0) init = new[] { (int[])last[0].Clone(), (int[])last[1].Clone() };
				cur = r.Frames[f];
				var before = new[] { (int[])last[0].Clone(), (int[])last[1].Clone() };
				p.Tick();
				// Опкод повтора — ещё не признак зацикленности: у девяти треков оригинала декодер
				// через пару тиков натыкается на конец и трек играется один раз (22 §2.2).
				if (p.Looped && firstLoopFrame < 0) { firstLoopFrame = f; loopTick = k; end = before; }
				if (loopTick > 0 && k >= loopTick + 10) break;
			}
			r.Loop = firstLoopFrame >= 0 && p.Playing;
			if (r.Loop && firstLoopFrame < r.Frames.Count)
				r.Frames.RemoveRange(firstLoopFrame, r.Frames.Count - firstLoopFrame);   // 22 §2.3
			r.Init = init ?? new[] { (int[])last[0].Clone(), (int[])last[1].Clone() };
			r.End = end ?? new[] { (int[])last[0].Clone(), (int[])last[1].Clone() };
			return r;
		}

		public static (byte[] data, int maxWrites) EncodeStream(Render r)
		{
			var o = new List<byte>();
			int idle = 0, maxw = 0, loopOffset = -1;
			void FlushIdle() { while (idle > 0) { int k = Math.Min(idle, 126); o.Add((byte)(0x80 + k)); idle -= k; } }
			for (int idx = 0; idx < r.Frames.Count; idx++)
			{
				if (idx == 1) { FlushIdle(); loopOffset = o.Count; }
				var b0 = r.Frames[idx][0]; var b1 = r.Frames[idx][1];
				int n0 = b0.Count / 2, n1 = b1.Count / 2;
				maxw = Math.Max(maxw, n0 + n1);
				if (n0 == 0 && n1 == 0) { idle++; continue; }
				FlushIdle();
				for (int i = 0, j = 0; i < n0 || j < n1;)
				{
					int k0 = Math.Min(127, n0 - i), k1 = Math.Min(255, n1 - j);
					o.Add((byte)k0); o.AddRange(b0.GetRange(i * 2, k0 * 2));
					o.Add((byte)k1); o.AddRange(b1.GetRange(j * 2, k1 * 2));
					i += k0; j += k1;
				}
			}
			FlushIdle();
			o.Add(0xFF);
			// Блок ресинка (22 §2.4): к точке повтора состояние чипа расходится с тем, каким оно
			// было в начале, на десятки регистров — включая зависшие ноты. После #FF кладём эту
			// разницу в формате кадра, плеер проигрывает её перед прыжком на loop_offset.
			int resync = 0;
			if (r.Loop && r.Init != null && r.End != null)
			{
				var diff = new[] { new List<byte>(), new List<byte>() };
				for (int bank = 0; bank < 2; bank++)
					for (int rg = 0; rg < 256; rg++)
					{
						int want = r.Init[bank][rg], have = r.End[bank][rg];
						if (want < 0 || want == have) continue;
						diff[bank].Add((byte)rg); diff[bank].Add((byte)want);
					}
				int d0 = diff[0].Count / 2, d1 = diff[1].Count / 2;
				resync = d0 + d1;
				for (int i = 0, j = 0; i < d0 || j < d1;)
				{
					int k0 = Math.Min(127, d0 - i), k1 = Math.Min(255, d1 - j);
					o.Add((byte)k0); o.AddRange(diff[0].GetRange(i * 2, k0 * 2));
					o.Add((byte)k1); o.AddRange(diff[1].GetRange(j * 2, k1 * 2));
					i += k0; j += k1;
				}
			}
			var hdr = new byte[12];
			BitConverter.GetBytes(r.Loop && loopOffset >= 0 ? (uint)loopOffset : 0xFFFFFFFF).CopyTo(hdr, 0);
			BitConverter.GetBytes(r.Frames.Count).CopyTo(hdr, 4);
			hdr[8] = (byte)maxw; hdr[9] = (byte)(maxw >> 8);
			hdr[10] = (byte)resync; hdr[11] = (byte)(resync >> 8);
			return (hdr.Concat(o).ToArray(), maxw);
		}

		// VGM 1.51 (YMF262) для прослушивания на ПК.
		public static byte[] ToVgm(Render r)
		{
			var cmds = new List<byte>();
			double acc = 0; int samplesTotal = 0, loopByte = -1, loopSamples = 0;
			void Wait(int n) { while (n > 0) { int k = Math.Min(n, 65535); cmds.Add(0x61); cmds.Add((byte)k); cmds.Add((byte)(k >> 8)); n -= k; } }
			for (int idx = 0; idx < r.Frames.Count; idx++)
			{
				if (idx == 1 && r.Loop) { loopByte = cmds.Count; loopSamples = samplesTotal; }
				var f = r.Frames[idx];
				for (int i = 0; i < f[0].Count; i += 2) { cmds.Add(0x5E); cmds.Add(f[0][i]); cmds.Add(f[0][i + 1]); }
				for (int i = 0; i < f[1].Count; i += 2) { cmds.Add(0x5F); cmds.Add(f[1][i]); cmds.Add(f[1][i + 1]); }
				acc += 44100 / FrameHz;
				int n = (int)Math.Floor(acc); acc -= n; samplesTotal += n; Wait(n);
			}
			cmds.Add(0x66);
			var hdr = new byte[0x100];
			Encoding.ASCII.GetBytes("Vgm ").CopyTo(hdr, 0);
			void U32(int o, int v) => BitConverter.GetBytes(v).CopyTo(hdr, o);
			U32(0x04, 0x100 + cmds.Count - 4);
			U32(0x08, 0x151);
			U32(0x18, samplesTotal);
			if (loopByte >= 0) { U32(0x1C, 0x100 + loopByte - 0x1C); U32(0x20, samplesTotal - loopSamples); }
			U32(0x24, 50);
			U32(0x34, 0x100 - 0x34);
			U32(0x5C, 14318180);
			return hdr.Concat(cmds).ToArray();
		}
	}
}
