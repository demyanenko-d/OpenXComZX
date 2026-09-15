// Каталог игры без учёта регистра (как FileMap в OpenXcom) и определение UFO/TFTD.
using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;

namespace OxzConv
{
	class GameFs
	{
		public readonly string Root;
		readonly Dictionary<string, string> index = new Dictionary<string, string>(StringComparer.Ordinal);

		public GameFs(string root)
		{
			Root = root;
			Scan(root, "");
		}

		void Scan(string dir, string rel)
		{
			foreach (var d in Directory.GetDirectories(dir))
				Scan(d, rel.Length > 0 ? rel + "/" + Path.GetFileName(d) : Path.GetFileName(d));
			foreach (var f in Directory.GetFiles(dir))
				index[(rel.Length > 0 ? rel + "/" + Path.GetFileName(f) : Path.GetFileName(f)).ToUpperInvariant()] = f;
		}

		public bool Has(string rel) => index.ContainsKey(rel.ToUpperInvariant());

		public string PathOf(string rel)
		{
			if (!index.TryGetValue(rel.ToUpperInvariant(), out var p)) throw new FileNotFoundException("file not found in game dir: " + rel);
			return p;
		}

		public byte[] Read(string rel) => File.ReadAllBytes(PathOf(rel));

		// Имена файлов каталога dir с расширением ext, по алфавиту (ordinal).
		public List<string> List(string dir, string ext = null)
		{
			string pre = dir.ToUpperInvariant() + "/", suf = ext != null ? "." + ext.ToUpperInvariant() : "";
			var l = index.Keys.Where(k => k.StartsWith(pre, StringComparison.Ordinal) && k.IndexOf('/', pre.Length) < 0 && k.EndsWith(suf, StringComparison.Ordinal))
				.Select(k => k.Substring(pre.Length)).ToList();
			l.Sort(string.CompareOrdinal);
			return l;
		}

		public string DetectGame()
		{
			if (!Has("GEODATA/PALETTES.DAT")) throw new InvalidDataException("not an X-COM game directory (no GEODATA/PALETTES.DAT)");
			bool tftd = Has("TERRAIN/LOFTEMPS.DAT") || List("UFOGRAPH", "BDY").Count > 0 || Has("GEOGRAPH/UP001.BDY");
			bool ufo = Has("GEOGRAPH/UP001.SPK") || Has("UNITS/SECTOID.PCK");
			if (tftd && !ufo) return "TFTD";
			if (ufo && !tftd) return "UFO";
			throw new InvalidDataException("cannot tell UFO from TFTD");
		}
	}
}
