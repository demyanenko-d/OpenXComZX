// Музыка из MIDI оригинала (`SOUND/GM.CAT`) — второй источник для сведения на AY и FM,
// альтернатива прогону ADLIB-плеера (Music.cs). Включается ключом `--music midi`.
//
// Зачем. В ADLIB.CAT лежит партитура, уже разложенная на 12 голосов OPL2 со своим
// распределителем каналов, детюном и «хорусом» OpenXcom: сведение на три канала AY
// вынуждено угадывать, что из этого мелодия, а что расстроенный дубль (22 §10.6).
// В GM.CAT — та же музыка, но в виде MIDI: явные каналы, ноты, velocity, контроллер 7,
// педаль и ударные на канале 9. Из неё голоса раздаются сразу, без хоруса и детюна.
//
// Формат `GM.CAT`. Контейнер тот же, что у `ADLIB.CAT` (Music.CatEntries), внутри записи:
//   u8 len, имя[len] (с нулём)
//   u8 темп (BPM), u8 число подпоследовательностей
//   подпоследовательности: u16 длина (от себя), u16 0, данные (начинаются с задержки)
//   u8 число дорожек
//   дорожки: u8 канал MIDI, u16 длина (от себя), u16 0, данные (начинаются с задержки)
// Данные — MIDI с running status: задержка VLQ перед каждым событием, статус дорожки
// всегда с нулевым номером канала (настоящий канал — байт заголовка дорожки).
// Служебные опкоды на месте статуса: #FE n — вызов подпоследовательности n, #FD — возврат
// (задержка читается уже по месту возврата), #FF — конец дорожки. Смена программы на 126
// (`C0 7E`) — метка повтора трека, ровно как в ADLIB (adlplayer.cpp:683).
// Делений на четверть — 24 (сверено с `GMGEO1.MID` и др.: те же треки при 384 делениях,
// длительности сходятся с прогоном ADLIB до секунд, 22 §11.1).
//
// Разбор `.MID` (SMF формата 0 и 1) сделан тем же кодом — им проверялся разбор CAT.
using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;

namespace OxzConv
{
	static class MusicMidi
	{
		public const int DrumCh = 9;          // канал ударных GM (нумерация с нуля)
		const int Voices = 12;                // столько же голосов, сколько у плеера ADLIB
		const int BendRange = 2;              // полутонов на полный отвод колеса (умолчание GM)

		// Событие в абсолютных тиках. Kind — старший полубайт статуса MIDI,
		// #F0 — метка повтора, #F1 — смена темпа (Val — микросекунд на четверть).
		public class Ev { public int Tick; public byte Ch, Kind, A, B; public int Val; }

		public class Song
		{
			public string Name = "";
			public int Ppqn = 24, Bpm = 120;
			public List<Ev> Events = new List<Ev>();
			public int LoopTick = -1;          // тик метки повтора; -1 — трек одношаговый
			public int EndTick;
			public int Notes;
		}

		// ------------------------------------------------------------------ разбор GM.CAT

		static int Vlq(byte[] d, ref int p)
		{
			int v = 0, c;
			do { c = p < d.Length ? d[p] : 0; p++; v = (v << 7) | (c & 0x7F); } while ((c & 0x80) != 0 && p < d.Length);
			return v;
		}

		public static Song ParseCat(byte[] d)
		{
			var s = new Song();
			int p = d[0] + 1;
			s.Name = Encoding.ASCII.GetString(d, 1, p - 1).Trim('\0', ' ');
			s.Ppqn = 24;
			s.Bpm = d[p++];
			if (s.Bpm < 20) s.Bpm = 120;
			int nsub = d[p++];
			var subs = new int[nsub];
			for (int i = 0; i < nsub; i++)
			{
				int add = d[p] | d[p + 1] << 8;
				subs[i] = p + 4;
				p += add;
				if (add == 0 || p > d.Length) throw new Exception("GM.CAT: битая подпоследовательность");
			}
			int ntrk = d[p++];
			var starts = new List<int>();
			var chans = new List<int>();
			for (int i = 0; i < ntrk; i++)
			{
				int ch = d[p++], add = d[p] | d[p + 1] << 8;
				chans.Add(ch); starts.Add(p + 4);
				p += add;
				if (add == 0 || p > d.Length) throw new Exception("GM.CAT: битая дорожка");
			}
			for (int i = 0; i < starts.Count; i++) CatTrack(d, subs, chans[i], starts[i], s);
			Finish(s);
			return s;
		}

		// Одна дорожка GM.CAT: задержка перед событием, running status, #FE/#FD/#FF.
		static void CatTrack(byte[] d, int[] subs, int ch, int start, Song s)
		{
			int p = start, tick = 0, ret = -1, st = 0, guard = 0;
			tick += Vlq(d, ref p);
			while (p < d.Length)
			{
				if (guard++ > 1000000) break;
				int op = d[p++];
				if (op == 0xFF) break;
				if (op == 0xFE)
				{
					int a = d[p++];
					ret = p;
					if (a >= subs.Length) break;
					p = subs[a];
				}
				else if (op == 0xFD)
				{
					if (ret < 0) break;
					p = ret; ret = -1;
				}
				else
				{
					if (op >= 0x80) { st = op; op = d[p++]; }
					int hi = st & 0xF0;
					int a = op, b = (hi == 0xC0 || hi == 0xD0) ? 0 : d[p++];
					Add(s, tick, ch, hi, a, b);
				}
				tick += Vlq(d, ref p);
			}
			if (tick > s.EndTick) s.EndTick = tick;
		}

		static void Add(Song s, int tick, int ch, int hi, int a, int b)
		{
			if (hi == 0xC0 && a == 0x7E) { if (s.LoopTick < 0 || tick < s.LoopTick) s.LoopTick = tick; return; }
			if (hi == 0xB0 && a == 0 && b != 0)                     // контроллер 0 — темп (adlplayer.cpp:672)
			{ s.Events.Add(new Ev { Tick = tick, Kind = 0xF1, Val = 60000000 / Math.Max(1, b * 2) }); return; }
			if (hi == 0x90 && b > 0) s.Notes++;
			s.Events.Add(new Ev { Tick = tick, Ch = (byte)ch, Kind = (byte)hi, A = (byte)a, B = (byte)b });
		}

		static void Finish(Song s)
		{
			// Sort в .NET неустойчив — сортируем по (тик, номер), чтобы порядок событий
			// одного тика (сначала снятие, потом взятие) не зависел от реализации.
			var idx = Enumerable.Range(0, s.Events.Count).ToArray();
			Array.Sort(idx, (x, y) => s.Events[x].Tick != s.Events[y].Tick ? s.Events[x].Tick - s.Events[y].Tick : x - y);
			s.Events = idx.Select(i => s.Events[i]).ToList();
			if (s.LoopTick < 0 && s.EndTick == 0 && s.Events.Count > 0) s.EndTick = s.Events[s.Events.Count - 1].Tick;
		}

		// ------------------------------------------------------------------ разбор SMF (.MID)

		public static Song ParseSmf(byte[] d)
		{
			if (d.Length < 14 || Encoding.ASCII.GetString(d, 0, 4) != "MThd") throw new Exception("не SMF");
			int U16(int o) { return d[o] << 8 | d[o + 1]; }
			int U32(int o) { return d[o] << 24 | d[o + 1] << 16 | d[o + 2] << 8 | d[o + 3]; }
			var s = new Song();
			int ntrk = U16(10), div = U16(12);
			s.Ppqn = div > 0 ? div : 96;
			int p = 14;
			for (int t = 0; t < ntrk && p + 8 <= d.Length; t++)
			{
				if (Encoding.ASCII.GetString(d, p, 4) != "MTrk") break;
				int len = U32(p + 4), q = p + 8, end = Math.Min(q + len, d.Length);
				p = q + len;
				int tick = 0, st = 0;
				while (q < end)
				{
					tick += Vlq(d, ref q);
					if (q >= end) break;
					int op = d[q++];
					if (op == 0xFF)
					{
						int ty = d[q++], ln = Vlq(d, ref q);
						if (ty == 0x51 && ln == 3) s.Events.Add(new Ev { Tick = tick, Kind = 0xF1, Val = d[q] << 16 | d[q + 1] << 8 | d[q + 2] });
						q += ln;
						if (ty == 0x2F) break;
						continue;
					}
					if (op == 0xF0 || op == 0xF7) { int ln = Vlq(d, ref q); q += ln; continue; }
					if (op >= 0x80) st = op; else q--;
					int hi = st & 0xF0, ch = st & 0x0F;
					int a = d[q++], b = (hi == 0xC0 || hi == 0xD0) ? 0 : d[q++];
					Add(s, tick, ch, hi, a, b);
				}
				if (tick > s.EndTick) s.EndTick = tick;
			}
			Finish(s);
			// В .MID темп задаёт meta 0x51; BPM заголовка нет.
			var first = s.Events.FirstOrDefault(e => e.Kind == 0xF1);
			s.Bpm = first != null ? (int)Math.Round(60000000.0 / first.Val) : 120;
			return s;
		}

		// ------------------------------------------------------------------ тембры

		// Патч в формате ADLIB (24 байта, Music.PlayNote): [0]/[1] — AM/VIB/EGT/KSR/MULT
		// модулятора и несущей, [2] — KSL и «уровень» модулятора (63 − TL), [4]/[5] — AR|DR,
		// [6]/[7] — SL|RR, [8]/[9] — форма волны, [10] — обратная связь и связь операторов
		// (в регистр C0 идёт с ^1). Громкость несущей берётся из velocity, не из патча.
		static byte[] P(int mCtl, int mLvl, int mAd, int mSr, int cCtl, int cAd, int cSr, int fb, int add)
		{
			var b = new byte[24];
			b[0] = (byte)mCtl; b[1] = (byte)cCtl; b[2] = (byte)mLvl;
			b[4] = (byte)mAd; b[5] = (byte)cAd; b[6] = (byte)mSr; b[7] = (byte)cSr;
			b[10] = (byte)((fb << 1) | (add ^ 1));
			return b;
		}
		static int Ctl(int egt, int mult) { return (egt != 0 ? 0x20 : 0) | (mult & 0x0F); }
		static int Ad(int ar, int dr) { return ar << 4 | dr; }

		// Шестнадцать семейств GM (программа / 8). Сторонний банк не тащим: два оператора,
		// огибающая по характеру семейства — этого хватает и для огибающей AY (MusicEnv),
		// и для переноса на OPN (MusicFm). Названия — группы General MIDI.
		static readonly byte[][] Family =
		{
			P(Ctl(0, 1), 45, Ad(15, 4), Ad(2, 6), Ctl(0, 1), Ad(15, 4), Ad(0, 6), 6, 0),   // 0 фортепиано
			P(Ctl(0, 4), 49, Ad(15, 7), Ad(1, 7), Ctl(0, 1), Ad(15, 7), Ad(0, 7), 4, 0),   // 1 колокольчики
			P(Ctl(1, 1), 39, Ad(15, 0), Ad(0, 7), Ctl(1, 1), Ad(15, 0), Ad(0, 7), 0, 0),   // 2 орган
			P(Ctl(0, 1), 47, Ad(15, 6), Ad(1, 7), Ctl(0, 1), Ad(15, 5), Ad(0, 7), 5, 0),   // 3 гитара
			P(Ctl(1, 1), 43, Ad(15, 3), Ad(3, 8), Ctl(1, 1), Ad(15, 3), Ad(2, 8), 6, 0),   // 4 бас
			P(Ctl(1, 1), 41, Ad(10, 4), Ad(2, 4), Ctl(1, 1), Ad(11, 3), Ad(1, 4), 4, 0),   // 5 струнные
			P(Ctl(1, 1), 41, Ad(10, 3), Ad(1, 3), Ctl(1, 1), Ad(11, 2), Ad(1, 3), 3, 0),   // 6 ансамбль
			P(Ctl(1, 1), 49, Ad(12, 4), Ad(2, 6), Ctl(1, 1), Ad(13, 4), Ad(1, 6), 5, 0),   // 7 медь
			P(Ctl(1, 1), 45, Ad(13, 3), Ad(1, 6), Ctl(1, 1), Ad(14, 3), Ad(1, 6), 4, 0),   // 8 язычковые
			P(Ctl(1, 1), 33, Ad(13, 2), Ad(0, 5), Ctl(1, 1), Ad(13, 2), Ad(0, 5), 0, 0),   // 9 флейты
			P(Ctl(1, 1), 51, Ad(15, 3), Ad(1, 5), Ctl(1, 1), Ad(15, 3), Ad(1, 5), 6, 0),   // 10 синт-соло
			P(Ctl(1, 1), 39, Ad(9, 3), Ad(2, 2), Ctl(1, 1), Ad(10, 2), Ad(1, 2), 3, 0),    // 11 синт-подклад
			P(Ctl(1, 1), 43, Ad(11, 4), Ad(2, 4), Ctl(1, 1), Ad(12, 3), Ad(2, 4), 4, 0),   // 12 синт-эффекты
			P(Ctl(0, 1), 45, Ad(15, 7), Ad(1, 7), Ctl(0, 1), Ad(15, 6), Ad(0, 7), 5, 0),   // 13 этнические
			P(Ctl(0, 1), 47, Ad(15, 9), Ad(0, 9), Ctl(0, 1), Ad(15, 9), Ad(0, 9), 6, 0),   // 14 ударные тональные
			P(Ctl(0, 1), 43, Ad(15, 5), Ad(1, 5), Ctl(0, 1), Ad(15, 5), Ad(0, 5), 4, 0),   // 15 звуковые эффекты
		};

		// Ударные: четыре характера огибающей (бочка, малый, хэт, тарелка/том).
		static readonly byte[][] Drums =
		{
			P(Ctl(0, 1), 47, Ad(15, 10), Ad(0, 10), Ctl(0, 1), Ad(15, 10), Ad(0, 10), 7, 0), // 0 бочка
			P(Ctl(0, 1), 45, Ad(15, 9), Ad(0, 9), Ctl(0, 1), Ad(15, 9), Ad(0, 9), 7, 0),     // 1 малый
			P(Ctl(0, 1), 43, Ad(15, 12), Ad(0, 12), Ctl(0, 1), Ad(15, 12), Ad(0, 12), 7, 0), // 2 хэт
			P(Ctl(0, 1), 45, Ad(15, 6), Ad(0, 6), Ctl(0, 1), Ad(15, 6), Ad(0, 6), 7, 0),     // 3 тарелка
		};

		// Нота GM-перкуссии -> (характер, частота тона в сотых герца, период шума AY 1..31).
		// Период шума: чем меньше, тем «ярче» (f = 1.7744 МГц / (16 * период)).
		static void DrumMap(int note, out int kind, out int hz100, out int noise)
		{
			switch (note)
			{
			case 35: case 36: kind = 0; hz100 = 5500; noise = 0; return;               // бочка — тоном
			case 41: case 43: kind = 3; hz100 = 9000; noise = 0; return;               // низкие томы
			case 45: case 47: kind = 3; hz100 = 12000; noise = 0; return;
			case 48: case 50: kind = 3; hz100 = 16000; noise = 0; return;              // высокие томы
			case 42: case 44: kind = 2; hz100 = 0; noise = 2; return;                  // закрытый хэт
			case 46: kind = 2; hz100 = 0; noise = 4; return;                           // открытый хэт
			case 49: case 52: case 55: case 57: kind = 3; hz100 = 0; noise = 1; return; // крэш/сплэш
			case 51: case 53: case 59: kind = 3; hz100 = 0; noise = 2; return;         // райд
			case 37: case 39: kind = 1; hz100 = 0; noise = 10; return;                 // римшот, хлопок
			case 38: case 40: kind = 1; hz100 = 0; noise = 8; return;                  // малый
			default: kind = 1; hz100 = 0; noise = 6; return;                           // остальная перкуссия
			}
		}

		// ------------------------------------------------------------------ прогон в кадры

		class Vc
		{
			public int Ch = -1, Note = -1, Vel, Trig, Patch = -1, RelAge = 99999, Hz100, Noise;
			public bool On, Sust, Drum;
		}

		static int NoteHz100(double note)
		{
			return (int)Math.Round(44000.0 * Math.Pow(2.0, (note - 69.0) / 12.0));
		}

		// Из разобранного трека — покадровое состояние 12 голосов, ровно того же вида,
		// что даёт плеер ADLIB (Music.VoiceFrame): сведение на AY и FM не меняется.
		public static Music.Render Render(Song s, double maxSeconds = 1200)
		{
			var r = new Music.Render();
			var patches = new List<byte[]>();
			var patchKey = new Dictionary<int, int>();
			int Patch(int key, byte[] data)
			{
				int i;
				if (patchKey.TryGetValue(key, out i)) return i;
				i = patches.Count; patchKey[key] = i; patches.Add(data);
				return i;
			}

			var vs = Enumerable.Range(0, Voices).Select(_ => new Vc()).ToArray();
			var chVol = Enumerable.Repeat(100, 16).ToArray();   // умолчание контроллера 7 в GM
			var chExp = Enumerable.Repeat(127, 16).ToArray();   // контроллер 11
			var chProg = new int[16];
			var chBend = new int[16];
			var chSust = new bool[16];
			int trig = 0;

			void KeyOff(Vc v) { v.On = false; v.Sust = false; v.RelAge = 0; }

			void NoteOff(int ch, int note)
			{
				foreach (var v in vs)
					if (v.On && v.Ch == ch && v.Note == note)
					{
						if (chSust[ch]) v.Sust = true; else KeyOff(v);
					}
			}

			void NoteOn(int ch, int note, int vel)
			{
				int kind, hz100, noise, pi;
				bool drum = ch == DrumCh;
				if (drum)
				{
					DrumMap(note, out kind, out hz100, out noise);
					pi = Patch(0x1000 + kind, Drums[kind]);
				}
				else
				{
					kind = 0; noise = 0; hz100 = 0;
					pi = Patch(chProg[ch] >> 3, Family[(chProg[ch] >> 3) & 15]);
				}
				// Тот же голос — если эта нота на этом канале уже звучит (перевзятие).
				Vc t = vs.FirstOrDefault(v => v.On && v.Ch == ch && v.Note == note);
				if (t == null)
				{
					// Свободный голос — тот, что дольше всех молчит; если все заняты,
					// забираем самый тихий (у оригинала здесь то же правило по длительности).
					t = vs.Where(v => !v.On).OrderByDescending(v => v.RelAge).FirstOrDefault()
					 ?? vs.OrderBy(v => v.Vel).First();
				}
				t.Ch = ch; t.Note = note; t.Vel = vel; t.Patch = pi; t.On = true; t.Sust = false;
				t.Drum = drum; t.Noise = noise; t.Hz100 = hz100;
				t.Trig = ++trig;
			}

			int endTick = s.LoopTick >= 0 ? s.LoopTick : s.EndTick;
			int usPerQ = 60000000 / Math.Max(20, s.Bpm);
			double tick = 0;
			int ei = 0, maxFrames = (int)(maxSeconds * Music.FrameHz);

			// Кадр 0 — пустой (в потоке AY/FM это кадр начальной установки), музыка идёт с кадра 1:
			// точка повтора у обоих сведений — кадр 1 (22 §10.2).
			r.Voices.Add(new Music.VoiceFrame());

			while (r.Voices.Count < maxFrames)
			{
				int now = (int)Math.Floor(tick);
				while (ei < s.Events.Count && s.Events[ei].Tick <= now)
				{
					var e = s.Events[ei++];
					switch (e.Kind)
					{
					case 0xF1: usPerQ = Math.Max(1000, e.Val); break;
					case 0x90: if (e.B > 0) NoteOn(e.Ch, e.A, e.B); else NoteOff(e.Ch, e.A); break;
					case 0x80: NoteOff(e.Ch, e.A); break;
					case 0xB0:
						if (e.A == 7) chVol[e.Ch] = e.B;
						else if (e.A == 11) chExp[e.Ch] = e.B;
						else if (e.A == 64)
						{
							chSust[e.Ch] = e.B >= 64;
							if (!chSust[e.Ch]) foreach (var v in vs) if (v.Sust && v.Ch == e.Ch) KeyOff(v);
						}
						else if (e.A == 120 || e.A == 123)
							foreach (var v in vs) if (v.Ch == e.Ch) KeyOff(v);
						break;
					case 0xC0: chProg[e.Ch] = e.A; break;
					case 0xE0: chBend[e.Ch] = (e.A | e.B << 7) - 8192; break;
					}
				}

				var vf = new Music.VoiceFrame();
				for (int i = 0; i < Voices; i++)
				{
					var v = vs[i];
					if (!v.On) v.RelAge++;
					vf.Trig[i] = v.Trig;
					vf.Patch[i] = v.Patch;
					vf.Key[i] = v.On;
					vf.Drum[i] = v.Drum;
					vf.Noise[i] = v.Noise;
					if (v.Note >= 0)
					{
						double n = v.Note + (v.Drum ? 0 : chBend[v.Ch] * BendRange / 8192.0);
						vf.Freq[i] = v.Drum ? v.Hz100 : NoteHz100(n);
					}
					// Громкость — velocity, приведённая контроллерами 7 и 11, как у плеера
					// оригинала (v = (velocity * volume) >> 7, adlplayer.cpp:635), и с поправкой
					// на высоту (Tilt).
					vf.Vol[i] = v.On ? Tilt(Math.Min(127, v.Vel * chVol[v.Ch] * chExp[v.Ch] / (127 * 127)), vf.Freq[i]) : 0;
				}
				r.Voices.Add(vf);

				if (tick >= endTick && ei >= s.Events.Count) break;
				if (tick >= endTick) break;
				tick += s.Ppqn * 1000000.0 / usPerQ / Music.FrameHz;
			}

			Normalize(r);
			r.Patches = patches;
			r.Loop = s.LoopTick >= 0;
			r.LoopFrame = r.Loop ? r.Voices.Count : -1;
			return r;
		}

		// Поправка громкости на высоту ноты. Балансировка в MIDI рассчитана на модуль General
		// MIDI с настоящими сэмплами: там бас 37 Гц сам по себе глухой и слабый, а высокие
		// струнные слышно и при контроллере 7 = 15. У нас же меандр AY и синус OPN на 37 Гц
		// идут полной мощностью. Замер `UFO Story`: канал 8 (струнные 1480…5588 Гц) звучит с
		// эффективной громкостью 11 из 127, а басовые каналы 1 и 7 (37 Гц) — 47 и 61; в записи
		// с эмулятора 99 % энергии лежало ниже 200 Гц (22 §11.3). Поэтому даём наклон
		// 3 дБ на октаву от до первой октавы, с ограничением −6…+12 дБ — это та же компенсация,
		// что KSL у OPL, только по абсолютной частоте.
		// 3 дБ на октаву без ограничения сверху перекручивает: у `UFO Story` верхние струнные
		// 1.5…5.6 кГц из неслышимых становились громче всего (33 % энергии записи в полосе
		// 1.6…3.2 кГц против 2 % у потока из ADLIB). 2 дБ на октаву с потолком +7 дБ ставит
		// баланс между ними.
		const double TiltPerOct = 2.0, TiltMin = -5.0, TiltMax = 7.0;

		static int Tilt(int vol, int hz100)
		{
			if (vol <= 0 || hz100 <= 0) return vol;
			double db = TiltPerOct * Math.Log(hz100 / 26163.0, 2);
			if (db > TiltMax) db = TiltMax;
			if (db < TiltMin) db = TiltMin;
			// Шкала громкости 0…127 идёт в TL несущей как 63 − vol/2 при шаге 0.75 дБ,
			// то есть один децибел — это 8/3 единицы громкости.
			int v = vol + (int)Math.Round(db * 8.0 / 3.0);
			return v < 1 ? 1 : v > 127 ? 127 : v;
		}

		// Приведение громкости трека к полной шкале. В MIDI оригинала контроллер 7 у многих
		// партий стоит на 34…80 из 127, и velocity туда же: произведение доходит до 127 редко,
		// а сведение считает от полной шкалы — первый же замер записи с эмулятора показал у
		// потока из MIDI RMS −33.6 dBFS против −25.2 у потока из ADLIB (22 §11.3).
		// Поэтому масштабируем громкости трека так, чтобы 99-й процентиль кадровых максимумов
		// пришёлся на 127 (не максимум: одна случайная нота не должна прижимать весь трек).
		static void Normalize(Music.Render r)
		{
			var peaks = new List<int>();
			foreach (var vf in r.Voices)
			{
				int m = 0;
				for (int i = 0; i < Voices; i++) if (vf.Vol[i] > m) m = vf.Vol[i];
				if (m > 0) peaks.Add(m);
			}
			if (peaks.Count == 0) return;
			peaks.Sort();
			int top = peaks[(int)(peaks.Count * 0.99)];
			if (top <= 0 || top >= 127) return;
			foreach (var vf in r.Voices)
				for (int i = 0; i < Voices; i++)
					if (vf.Vol[i] > 0) vf.Vol[i] = Math.Min(127, vf.Vol[i] * 127 / top);
		}
	}
}
