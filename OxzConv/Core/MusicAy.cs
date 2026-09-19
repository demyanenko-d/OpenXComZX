// Музыка для чипов без FM: тот же трек, сведённый с двенадцати голосов плеера на три канала
// AY-3-8910 (или шесть — два чипа TurboSound / SSG-часть YM2203). Поток покадровый и в том же
// формате, что у OPL3 (20 §4), поэтому плеер в прерывании остаётся прежним — меняется только
// процедура вывода в порты.
//
// Почему нельзя пересчитывать на лету: в MUSIC.PAK лежат записи в регистры OPL3 (двенадцать
// FM-голосов), а у AY три канала прямоугольника с общей огибающей. Сведение — это выбор, что
// из двенадцати голосов слышно, и оно должно быть сделано заранее.
//
// Громкость канала берётся не из velocity, а из посчитанной огибающей ноты (MusicEnv):
// иначе нота звучит ровной полкой до снятия, и трек идёт на максимуме без динамики (22 §10.5).
// Самый низкий бас поднимается на октаву: ниже ~60 Гц меандр AY звучит не нотой, а треском.
using System;
using System.Collections.Generic;
using System.Linq;

namespace OxzConv
{
	static class MusicAy
	{
		public const int Clock = 1774400;          // частота AY на TS-Config (02 §7)
		const int Chans = 3;
		// Шкала AY вдвое короче, чем у OPL: сжимаем динамику, иначе средние по силе ноты
		// уходят в -24 дБ и трек звучит в четверть громкости.
		const double Compress = 0.7;
		const double Hyst = 4.0;                   // фора голосу, уже сидящему на канале, дБ
		const double Chorus = 18.0;                // штраф хорусному дублю: берём, только если больше некого
		const int BassHz100 = 6000;                // ниже — поднимаем на октаву

		// Скорость изменения громкости канала, ступеней за кадр (ступень 3 дБ). Без ограничения
		// нота вступает скачком на десяток ступеней за 20 мс — это и слышно как щелчок.
		const int RampUp = 5, RampDown = 4;
		const int NoteDip = 3;                     // насколько приглушить канал на кадре смены ноты

		class Slot { public int Voice = -1, Vol, Period; }

		// Что на самом деле сыграет канал: ниже ~60 Гц меандр звучит не нотой, а треском,
		// поэтому бас поднимается на октаву (в отдельных треках таких нот почти все).
		static int Play(int hz100)
		{
			while (hz100 > 0 && hz100 < BassHz100) hz100 *= 2;
			return hz100;
		}

		// Голос плеера со своей огибающей: velocity запоминается на key-on, дальше громкость
		// падает по огибающей патча, и после снятия ноты голос ещё доигрывает затухание.
		class Voice
		{
			public MusicEnv Env = new MusicEnv();
			public int Patch = -1, Trig = -1, Freq;
			public double Vel;                      // затухание от velocity, дБ
			public bool Chorus;                     // расстроенный дубль (добавка OpenXcom)
			public double Att = MusicEnv.Silence;
		}

		// Свести кадры голосов в поток записей в регистры AY.
		// voices[f] — состояние 12 голосов плеера на кадр (Music.VoiceFrame).
		public static (byte[] data, int maxWrites, int loopOffset) Encode(List<Music.VoiceFrame> voices, List<byte[]> patches, bool loop)
		{
			var slots = Enumerable.Range(0, Chans).Select(_ => new Slot()).ToArray();
			var vs = Enumerable.Range(0, 12).Select(_ => new Voice()).ToArray();
			var last = Enumerable.Repeat(-1, 16).ToArray();
			var o = new List<byte>();
			int idle = 0, maxw = 0, loopOffset = -1;
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
				// Точка повтора: забываем состояние регистров, и кадр 1 выписывает всё, что нужно.
				// Так после прыжка на loop_offset чип получает верные значения из самого потока —
				// не нужен ни блок ресинка, ни кадр глушения в конце, который обрывал звук на
				// стыке круга (в записи — провал до нуля на 60 мс, 22 §10.11).
				if (f == 1 && loop)
				{
					FlushIdle(); loopOffset = o.Count;
					for (int r0 = 0; r0 < 16; r0++) last[r0] = -1;
					// Громкости считаем уже нулевыми: тогда кадр 1 не пишет нули явно, и на стыке
					// круга ноты конца трека доигрывают, а не обрываются (как у потока OPL3).
					last[8] = last[9] = last[10] = 0;
					last[7] = 0x3F;                 // и микшер: иначе кадр 1 выключает тоны и обрывает звук
				}
				var vf = voices[f];

				// Огибающие: новая нота — заново, снятая — в затухание, и шаг на кадр
				for (int i = 0; i < 12; i++)
				{
					var v = vs[i];
					int pi = vf.Patch[i];
					if (vf.Key[i])
					{
						if (pi >= 0 && pi < patches.Count && (pi != v.Patch || vf.Trig[i] != v.Trig))
						{
							v.Env.SetPatch(patches[pi], Music.FrameHz);
							v.Env.KeyOn();
							v.Patch = pi; v.Trig = vf.Trig[i];
							int vel = vf.Vol[i] > 127 ? 127 : vf.Vol[i];
							v.Vel = (63 - ((127 * vel) >> 8)) * 0.75;   // velocity -> TL несущей, 0.75 дБ
								v.Chorus = vf.Chorus[i];
						}
						v.Freq = vf.Freq[i];
					}
					else if (v.Trig >= 0) { v.Env.KeyOff(); v.Trig = -1; }
					v.Att = v.Env.Step() + v.Vel;
					if (vf.Freq[i] > 0) v.Freq = vf.Freq[i];
				}

				// Кого слышно: три самых громких по огибающей. Тому, кто уже сидит на канале,
				// даётся фора Hyst — иначе два почти равных голоса каждый кадр меняются
				// каналами и нота дрожит. Без выбора «первых трёх» каналы залипали на
				// затухающих хвостах: за трек меню менялось всего три десятка нот.
				double Eff(int i) => vs[i].Att - (slots.Any(x => x.Voice == i) ? Hyst : 0) + (vs[i].Chorus ? Chorus : 0);
				var order = new List<int>();
				foreach (var i in Enumerable.Range(0, 12).Where(k => vs[k].Att < MusicEnv.Silence && vs[k].Freq > 0).OrderBy(Eff))
				{
					// Унисон в каналы не пускаем: два тона, разошедшиеся на процент, дают биения
					// («дрожание нот»), а канал отнимают у настоящей второй ноты. Сравниваем уже
					// поднятые частоты: бас 36 Гц и нота 73 Гц — это октава, но после подъёма
					// баса они сливаются в один тон.
					if (order.Any(j => Math.Abs((double)Play(vs[j].Freq) / Play(vs[i].Freq) - 1) < 0.03)) continue;
					order.Add(i);
					if (order.Count == Chans) break;
				}
				// Мелодию — обязательно. По одной громкости в каналы шёл почти один бас: в записи
				// эмулятора 68 % энергии лежало ниже 100 Гц против 39 % у OPL3 (tools/proto/wavscan.js).
				// Поэтому самый высокий из слышимых голосов занимает место самого тихого.
				int hi = -1;
				foreach (var i in Enumerable.Range(0, 12))
					if (vs[i].Att < MusicEnv.Silence && vs[i].Freq > 0 && !vs[i].Chorus &&
						(hi < 0 || vs[i].Freq > vs[hi].Freq)) hi = i;
				if (hi >= 0 && order.Count == Chans && !order.Contains(hi) &&
					!order.Any(j => Math.Abs((double)Play(vs[j].Freq) / Play(vs[hi].Freq) - 1) < 0.03))
					order[Chans - 1] = hi;
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
					// У AY всего 15 ступеней по 3 дБ — весь слышимый диапазон канала 45 дБ,
					// и затухание ниже него уже не отличить от тишины.
					int step = s.Voice < 0 ? 0 : (int)Math.Round((45.0 - vs[s.Voice].Att * Compress) / 3.0);
					if (step > 15) step = 15;
					if (s.Voice < 0 || step < 1)
					{
						s.Vol = s.Vol > RampDown ? s.Vol - RampDown : 0;   // гасим не рывком
						Reg(8 + c, s.Vol);
						if (s.Vol == 0) mixer |= 1 << c;    // тон выключаем, только когда доехали до нуля
						continue;
					}
					int hz100 = Play(vs[s.Voice].Freq);
					int period = hz100 > 0 ? (int)(100L * Clock / (16L * hz100)) : 0;
					if (period < 1) period = 1;
					if (period > 4095) period = 4095;
					// Смена тона при полной громкости щёлкает (меандр обрывается на полпути):
					// на кадре смены ноты приглушаем канал, дальше громкость доедет рампой.
					if (s.Period > 0 && Math.Abs((double)period / s.Period - 1) > 0.03) step -= NoteDip;
					s.Period = period;
					Reg(c * 2, period & 0xFF);
					Reg(c * 2 + 1, period >> 8);
					if (step < 1) step = 1;
					if (step > s.Vol + RampUp) step = s.Vol + RampUp;
					else if (step < s.Vol - RampDown) step = s.Vol - RampDown;
					s.Vol = step;
					Reg(8 + c, step);
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
			o.Add(0xFF);
			var hdr = new byte[12];
			BitConverter.GetBytes(loopOffset >= 0 ? (uint)loopOffset : 0xFFFFFFFF).CopyTo(hdr, 0);
			BitConverter.GetBytes(voices.Count).CopyTo(hdr, 4);
			hdr[8] = (byte)maxw; hdr[9] = (byte)(maxw >> 8);
			return (hdr.Concat(o).ToArray(), maxw, loopOffset);
		}
	}
}
