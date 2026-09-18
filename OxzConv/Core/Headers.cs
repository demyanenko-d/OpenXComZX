// Заголовки C для движка из реестров конвертера (данные игры не нужны):
// res_ids.h, str_ids.h, ui_ids.h, mus_ids.h. Вызывается из tools/build.ps1:
//   OxzConv.exe headers <каталог>
using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text;

namespace OxzConv
{
	static class Headers
	{
		static string CName(string s) => new string(s.Select(ch => char.IsLetterOrDigit(ch) && ch < 128 ? char.ToUpperInvariant(ch) : '_').ToArray());

		static string Header(string guard, string comment, IEnumerable<string> defs)
		{
			var sb = new StringBuilder();
			sb.Append("// Сгенерировано OxzConv (Core/Headers.cs) — не править вручную.\n");
			sb.Append("// ").Append(comment).Append('\n');
			sb.Append($"#ifndef {guard}\n#define {guard}\n\n");
			foreach (var d in defs) sb.Append(d).Append('\n');
			sb.Append("\n#endif\n");
			return sb.ToString();
		}

		static void Write(string path, string text) => File.WriteAllText(path, text, new UTF8Encoding(false));

		public static void Generate(string dir, Action<string> log)
		{
			Directory.CreateDirectory(dir);
			Write(Path.Combine(dir, "res_ids.h"), ResIds.StaticHeader());

			var strs = Registry.Load("engine_strings.txt");
			Write(Path.Combine(dir, "str_ids.h"), Header("STR_IDS_H", "Номера строк, которые использует движок (LANG.PAK, ресурс STRINGS).",
				strs.Select((k, i) => $"#define {k.PadRight(40)} {i}").Concat(new[] { $"#define STR_ENGINE_COUNT {strs.Count}" })));

			var scr = Registry.Load("ui_screens.txt"); var el = Registry.Load("ui_elements.txt");
			Write(Path.Combine(dir, "ui_ids.h"), Header("UI_IDS_H", "Экраны и элементы interfaces.rul (ресурс UI).",
				scr.Select((k, i) => $"#define UI_SCR_{CName(k).PadRight(32)} {i}")
				.Concat(new[] { $"#define UI_SCR_COUNT {scr.Count}", "" })
				.Concat(el.Select((k, i) => $"#define UI_EL_{CName(k).PadRight(33)} {i}"))
				.Concat(new[] { $"#define UI_EL_COUNT {el.Count}" })));

			var mus = Registry.Load("music_types.txt");
			Write(Path.Combine(dir, "mus_ids.h"), Header("MUS_IDS_H", "Типы музыки (MUSIC.PAK): номер ресурса = MUS_BASE + номер.",
				new[] { "#define MUS_BASE 0x0300" }.Concat(mus.Select((k, i) => $"#define MUS_{CName(k).PadRight(20)} {i}"))
				.Concat(new[] { $"#define MUS_COUNT {mus.Count}" })));

			var schema = RulesSchema.Tables();
			Write(Path.Combine(dir, "rules.h"), RuleSet.Header(schema, RulesSchema.ResBase));

			log($"headers: {dir}: res_ids.h, str_ids.h ({strs.Count}), ui_ids.h ({scr.Count} screens, {el.Count} elements), mus_ids.h ({mus.Count}), rules.h ({schema.Count} tables)");
		}
	}
}
