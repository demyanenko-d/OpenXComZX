// Файлы OpenXcom (правила, строки, шрифты): вшитые в exe или из каталога bin (--oxcom).
using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Reflection;
using System.Text;

namespace OxzConv
{
	class OxcomData
	{
		readonly string dir;                    // null — ресурсы exe
		readonly Assembly asm = Assembly.GetExecutingAssembly();

		public OxcomData(string binDir) { dir = binDir; }

		public string Source => dir ?? "embedded";

		// rel — путь относительно bin OpenXcom через '/', например "standard/xcom2/items.rul"
		public byte[] ReadBytes(string rel)
		{
			if (dir != null) return File.ReadAllBytes(Path.Combine(dir, rel.Replace('/', Path.DirectorySeparatorChar)));
			using (var s = asm.GetManifestResourceStream("openxcom/" + rel))
			{
				if (s == null) throw new FileNotFoundException("OpenXcom file not embedded: " + rel);
				using (var ms = new MemoryStream()) { s.CopyTo(ms); return ms.ToArray(); }
			}
		}

		public string ReadText(string rel)
		{
			var b = ReadBytes(rel);
			int start = b.Length >= 3 && b[0] == 0xEF && b[1] == 0xBB && b[2] == 0xBF ? 3 : 0;
			return Encoding.UTF8.GetString(b, start, b.Length - start);
		}

		public bool Exists(string rel)
		{
			if (dir != null) return File.Exists(Path.Combine(dir, rel.Replace('/', Path.DirectorySeparatorChar)));
			return asm.GetManifestResourceInfo("openxcom/" + rel) != null;
		}

		// Имена файлов *.rul раздела standard/<folder> по алфавиту (как sort() в JS).
		public List<string> RuleFiles(string folder)
		{
			IEnumerable<string> names;
			if (dir != null) names = Directory.GetFiles(Path.Combine(dir, "standard", folder), "*.rul").Select(Path.GetFileName);
			else
			{
				string pre = "openxcom/standard/" + folder + "/";
				names = asm.GetManifestResourceNames().Where(n => n.StartsWith(pre, StringComparison.Ordinal) && n.EndsWith(".rul", StringComparison.Ordinal) && n.IndexOf('/', pre.Length) < 0)
					.Select(n => n.Substring(pre.Length));
			}
			var l = names.ToList();
			l.Sort(string.CompareOrdinal);
			return l;
		}

		// Имена файлов каталога rel (например "common/SoldierName") с расширением ext, по алфавиту.
		public List<string> Files(string rel, string ext)
		{
			IEnumerable<string> names;
			if (dir != null) names = Directory.GetFiles(Path.Combine(dir, rel.Replace('/', Path.DirectorySeparatorChar)), "*" + ext).Select(Path.GetFileName);
			else
			{
				string pre = "openxcom/" + rel + "/";
				names = asm.GetManifestResourceNames().Where(n => n.StartsWith(pre, StringComparison.Ordinal) && n.EndsWith(ext, StringComparison.OrdinalIgnoreCase) && n.IndexOf('/', pre.Length) < 0)
					.Select(n => n.Substring(pre.Length));
			}
			var l = names.ToList();
			l.Sort(string.CompareOrdinal);
			return l;
		}

		// Реестры проекта (Data/*.txt, *.json) — всегда из exe.
		public static string ReadDataText(string name)
		{
			using (var s = Assembly.GetExecutingAssembly().GetManifestResourceStream("data/" + name))
			{
				if (s == null) throw new FileNotFoundException("data file not embedded: " + name);
				using (var r = new StreamReader(s, Encoding.UTF8)) return r.ReadToEnd();
			}
		}
	}
}
