// OxzConv: точка входа (консольное приложение).
using System;
using System.IO;
using System.Reflection;

namespace OxzConv
{
	static class Program
	{
		static int Main(string[] args)
		{
			// числа в отчёте — с точкой, независимо от языка системы
			System.Threading.Thread.CurrentThread.CurrentCulture = System.Globalization.CultureInfo.InvariantCulture;
			// YamlDotNet вшит в exe ресурсом — подгрузить при первом обращении.
			AppDomain.CurrentDomain.AssemblyResolve += (s, e) =>
			{
				if (!e.Name.StartsWith("YamlDotNet", StringComparison.Ordinal)) return null;
				using (var st = Assembly.GetExecutingAssembly().GetManifestResourceStream("lib/YamlDotNet.dll"))
				using (var ms = new MemoryStream())
				{
					st.CopyTo(ms);
					return Assembly.Load(ms.ToArray());
				}
			};
			return Cli.Run(args);
		}
	}
}
