// Огибающая ноты OPL2, посчитанная офлайн (22 §10.5).
//
// Зачем. У AY громкость канала — одно число, которое держится, пока его не сменят: чип
// сам ничего не затухает (огибающая AY общая на все каналы и для музыки не годится). Если
// брать громкость прямо из velocity плеера, как было сначала, то нота звучит ровной полкой
// до снятия: 76 % кадров уходило на ступени 13-15, динамики не было совсем — отсюда и
// «слишком громко», и каша. На OPL же громкость ноты задаёт огибающая несущей
// (AR/DR/SL/RR из патча ADLIB), и именно она делает разницу между органом и щипком.
//
// Что считаем. Затухание несущей в децибелах по кадрам (48.83 Гц): атака от тишины к нулю,
// спад к уровню удержания, удержание (EGT = 1) или дальнейший спад (EGT = 0 — перкуссионный
// патч), после снятия ноты — затухание с RR. Точная модель регистров OPL тут не нужна:
// шаг кадра 20 мс, всё, что быстрее, всё равно сливается в один отсчёт. Времена взяты по
// описанию YM3812 (спад на 96 дБ при rate 1 — десятки секунд, каждая ступень rate вдвое
// быстрее) и проверяются на слух.
using System;

namespace OxzConv
{
	class MusicEnv
	{
		public const double Silence = 96.0;          // дБ затухания, дальше слышно уже нечего
		// Подгонка на слух (просьба 2026-09-19): у AY три канала, и длинные хвосты снятых нот
		// занимают их дольше, чем нужно, а уровень удержания слишком близок к атаке.
		const double ReleaseFast = 2.0;              // во столько раз быстрее гаснет снятая нота
		const double SustainDrop = 4.0;              // на столько дБ ниже держится нота, дБ

		// Время затухания на 96 дБ и время атаки для скорости 1..15, секунды
		static readonly double[] TDecay = BuildT(60.0), TAttack = BuildT(6.0);

		static double[] BuildT(double t1)
		{
			var t = new double[16];
			t[0] = double.PositiveInfinity;          // rate 0 — огибающая стоит
			for (int r = 1; r < 16; r++) t[r] = t1 / Math.Pow(2, r - 1);
			return t;
		}

		enum Ph { Off, Attack, Decay, Sustain, Release }

		Ph ph = Ph.Off;
		double att = Silence;                        // текущее затухание, дБ
		double ar, dr, sl, rr;                       // дБ за кадр и уровень удержания
		bool hold;                                   // EGT: 1 — держать на уровне удержания

		// Патч ADLIB (24 байта): несущая — байты 1 (AM/VIB/EGT/KSR/MULT), 5 (AR/DR), 7 (SL/RR)
		public void SetPatch(byte[] p, double frameHz)
		{
			hold = (p[1] & 0x20) != 0;
			ar = Silence / Math.Max(TAttack[p[5] >> 4], 1e-6) / frameHz;
			dr = Silence / Math.Max(TDecay[p[5] & 0x0F], 1e-6) / frameHz;
			rr = Silence / Math.Max(TDecay[p[7] & 0x0F], 1e-6) / frameHz * ReleaseFast;
			sl = (p[7] >> 4) * 3.0 + SustainDrop;    // ступень уровня удержания — 3 дБ
		}

		public void KeyOn() { ph = Ph.Attack; }
		public void KeyOff() { if (ph != Ph.Off) ph = Ph.Release; }

		// Шаг на кадр; возвращает затухание в дБ (0 — полная громкость)
		public double Step()
		{
			switch (ph)
			{
			case Ph.Attack:
				att -= ar;
				if (att <= 0) { att = 0; ph = Ph.Decay; }
				break;
			case Ph.Decay:
				att += dr;
				if (att >= sl) { att = sl; ph = hold ? Ph.Sustain : Ph.Release; }
				break;
			case Ph.Sustain:
				break;
			case Ph.Release:
				att += rr;
				if (att >= Silence) { att = Silence; ph = Ph.Off; }
				break;
			}
			return att;
		}

		public bool Off => ph == Ph.Off;
	}
}
