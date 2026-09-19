// Командная строка OxzConv.
//   OxzConv.exe                                   — спросит каталоги в консоли
//   OxzConv.exe <каталог игры>                    — данные рядом с exe (перетаскивание)
//   OxzConv.exe --game DIR --out DIR [--preview DIR] [--oxcom DIR] [--lang en-US]
//                                    [--music adlib|midi]   — источник музыки для AY и FM
//   OxzConv.exe headers <каталог>                 — заголовки C для движка
//   OxzConv.exe dump <OXZ\игра> [таблица [число]]  — проверка: ресурсы / записи правил
using System;
using System.Collections.Generic;
using System.IO;
using System.Reflection;

namespace OxzConv
{
	static class Cli
	{
		public static int Run(string[] args)
		{
			Console.OutputEncoding = System.Text.Encoding.UTF8;
			try
			{
				if (args.Length >= 2 && args[0] == "dump")
					return Dump.Run(args[1], args.Length > 2 ? args[2] : null, args.Length > 3 ? int.Parse(args[3]) : 5, Console.WriteLine);
				if (args.Length >= 1 && args[0] == "headers")
				{
					if (args.Length < 2) { Console.Error.WriteLine("usage: OxzConv headers <dir>"); return 2; }
					Headers.Generate(args[1], Console.WriteLine);
					return 0;
				}
				var opt = new Dictionary<string, string>();
				bool interactive = false;
				if (args.Length == 1 && !args[0].StartsWith("--", StringComparison.Ordinal))
					opt["game"] = args[0];
				else
					for (int i = 0; i + 1 < args.Length; i += 2)
					{
						if (!args[i].StartsWith("--", StringComparison.Ordinal)) return Usage();
						opt[args[i].Substring(2)] = args[i + 1];
					}
				string exeDir = Path.GetDirectoryName(Assembly.GetExecutingAssembly().Location);
				if (!opt.ContainsKey("game"))
				{
					interactive = true;
					Console.WriteLine($"OxzConv {Assembly.GetExecutingAssembly().GetName().Version} — converter of original X-COM data for OpenXComZX (TS-Config)");
					Console.Write("Game folder (with GEODATA, GEOGRAPH, ...): ");
					opt["game"] = (Console.ReadLine() ?? "").Trim().Trim('"');
					Console.Write($"Output folder (Enter = {exeDir}): ");
					string o = (Console.ReadLine() ?? "").Trim().Trim('"');
					if (o.Length > 0) opt["out"] = o;
				}
				if (!opt.ContainsKey("out")) opt["out"] = exeDir;
				opt.TryGetValue("preview", out var prev);
				opt.TryGetValue("oxcom", out var oxDir);
				opt.TryGetValue("lang", out var lang);
				opt.TryGetValue("music", out var music);
				if (music != null && music != "adlib" && music != "midi")
				{
					Console.Error.WriteLine("oxzconv: --music принимает adlib или midi");
					return 2;
				}
				var conv = new Converter(opt["game"], opt["out"], prev, new OxcomData(oxDir), lang ?? "en-US", Console.WriteLine)
				{
					MusicSource = music ?? "adlib",
				};
				conv.Run();
				Console.WriteLine($"Copy the folder {Path.Combine(opt["out"], "OXZ")} to the root of the SD card.");
				if (interactive) { Console.Write("Press Enter to exit..."); Console.ReadLine(); }
				return 0;
			}
			catch (Exception e)
			{
				Console.Error.WriteLine("oxzconv: " + e.Message);
				return 1;
			}
		}

		static int Usage()
		{
			Console.Error.WriteLine("usage: OxzConv [<game dir>] | --game DIR --out DIR [--preview DIR] [--oxcom DIR] [--lang en-US] [--music adlib|midi] | headers DIR");
			return 2;
		}
	}
}
