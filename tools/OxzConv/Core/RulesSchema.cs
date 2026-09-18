// Схема таблиц правил: поля, типы, умолчания (из конструкторов OpenXcom —
// project_docs/10_rules_reference.md), порядок записей (sortLists OpenXcom).
// Номер ресурса таблицы = ResBase + номер в списке; RES_RULE_VARS = ResBase - 1.
// Порядок таблиц и полей не менять без пересборки движка (rules.h генерируется отсюда).
using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;

namespace OxzConv
{
	static class RulesSchema
	{
		public const int ResBase = 0x0040;

		static Field F(string name, string kind, object def = null, string cname = null, string comment = null) =>
			new Field { Name = name, Kind = kind, Default = def, CName = cname, Comment = comment };
		static Field Fn(string cname, string kind, FieldFn fn, string comment = null) =>
			new Field { Name = null, Kind = kind, Fn = fn, CName = cname, Comment = comment };

		// Название по умолчанию = ключ записи (name: у предметов, title: у статей и т.п.).
		static Field Name(string field = "name") => Fn("name", "str", (e, k, rs) => Y.Get(e, field) ?? k, "строка названия");

		public static List<TableSchema> Tables()
		{
			var t = new List<TableSchema>();

			t.Add(new TableSchema
			{
				Name = "items", Comment = "предметы",
				Fields =
				{
					Name(),
					F("requires", "refs:research"),
					F("size", "x100", "0"),
					F("costBuy", "u32", "0"), F("costSell", "u32", "0"),
					F("transferTime", "u8", "24"), F("weight", "u8", "3"),
					F("bigSprite", "i16", "-1"), F("floorSprite", "i16", "-1"), F("handSprite", "i16", "120"),
					F("bulletSprite", "i16", "-1", comment: "значение YAML; OpenXcom хранит *35"),
					F("fireSound", "i16", "-1"), F("hitSound", "i16", "-1"), F("hitAnimation", "i16", "-1"),
					F("meleeSound", "i16", "39"), F("meleeHitSound", "i16", "-1"), F("meleeAnimation", "i16", "0"),
					F("power", "u16", "0"), F("meleePower", "u16", "0"),
					F("compatibleAmmo", "refs:items"),
					F("damageType", "u8", "0", comment: "ItemDamageType"), F("battleType", "u8", "0", comment: "BattleType"),
					F("accuracyAuto", "u8", "0"), F("accuracySnap", "u8", "0"), F("accuracyAimed", "u8", "0"), F("accuracyMelee", "u8", "0"),
					F("tuAuto", "u8", "0"), F("tuSnap", "u8", "0"), F("tuAimed", "u8", "0"), F("tuMelee", "u8", "0"), F("tuUse", "u8", "0"),
					F("clipSize", "i16", "0", comment: "-1 — бесконечно"),
					F("waypoints", "u8", "0"), F("invWidth", "u8", "1"), F("invHeight", "u8", "1"),
					F("painKiller", "u8", "0"), F("heal", "u8", "0"), F("stimulant", "u8", "0"),
					F("woundRecovery", "u8", "0"), F("healthRecovery", "u8", "0"), F("stunRecovery", "u8", "0"), F("energyRecovery", "u8", "0"),
					F("recoveryPoints", "i16", "0"), F("armor", "u8", "20"), F("turretType", "i8", "-1"),
					F("blastRadius", "i8", "-1", comment: "-1 — вычислить по power/damageType"),
					F("attraction", "u8", "0"), F("autoShots", "u8", "3"), F("shotgunPellets", "u8", "0"),
					F("maxRange", "u8", "200"), F("aimRange", "u8", "200"), F("snapRange", "u8", "15"), F("autoRange", "u8", "7"),
					F("minRange", "u8", "0"), F("dropoff", "u8", "2"), F("bulletSpeed", "u8", "0"), F("explosionSpeed", "u8", "0"),
					F("specialType", "i8", "-1"), F("vaporColor", "i8", "-1"), F("vaporDensity", "u8", "0"), F("vaporProbability", "u8", "15"),
					F("zombieUnit", "ref:units"),
				},
				Flags = { "twoHanded", "fixedWeapon", "recover=true", "liveAlien", "flatRate", "arcingShot", "strengthApplied",
					"skillApplied=true", "LOSRequired", "underwaterOnly", "landOnly", "ignoreInBaseDefense" },
			});

			t.Add(new TableSchema
			{
				Name = "research", Key = "name", Comment = "исследования",
				Fields =
				{
					Name(),
					F("cost", "u16", "0"), F("points", "u16", "0"),
					F("dependencies", "refs:research"), F("unlocks", "refs:research"), F("getOneFree", "refs:research"),
					F("requires", "refs:research"),
					F("lookup", "ref:research"),
					Fn("item", "ref:items", (e, k, rs) => rs.Index["items"].ContainsKey(k) ? k : null, "предмет с тем же id (needItem)"),
				},
				Flags = { "needItem", "destroyItem", "unlockFinalMission" },
			});

			t.Add(new TableSchema
			{
				Name = "manufacture", Key = "name", Comment = "производство",
				Fields =
				{
					Name(),
					F("category", "str"),
					F("requires", "refs:research"),
					F("space", "u16", "0"), F("time", "u16", "0"), F("cost", "u32", "0"),
					F("requiredItems", "refmap:items|crafts"),
					Fn("producedItems", "refmap:items|crafts", (e, k, rs) => Y.Get(e, "producedItems") ?? new Dictionary<object, object> { [Y.Str(Y.Get(e, "name")) ?? k] = "1" },
						"по умолчанию {name: 1}"),
				},
			});

			t.Add(new TableSchema
			{
				Name = "facilities", Comment = "постройки базы",
				Fields =
				{
					Name(),
					F("requires", "refs:research"),
					F("spriteShape", "i16", "-1"), F("spriteFacility", "i16", "-1"),
					F("size", "u8", "1"),
					F("buildCost", "u32", "0"), F("buildTime", "u16", "0"), F("monthlyCost", "u32", "0"),
					F("storage", "u16", "0"), F("personnel", "u16", "0"), F("aliens", "u16", "0"), F("crafts", "u8", "0"),
					F("labs", "u16", "0"), F("workshops", "u16", "0"), F("psiLabs", "u8", "0"),
					F("radarRange", "u16", "0"), F("radarChance", "u8", "0"),
					F("defense", "u16", "0"), F("hitRatio", "u8", "0"), F("fireSound", "i16", "0"), F("hitSound", "i16", "0"),
				},
				Flags = { "lift", "hyper", "mind", "grav" },
			});

			t.Add(new TableSchema
			{
				Name = "crafts", Comment = "корабли",
				Fields =
				{
					Name(),
					F("requires", "refs:research"),
					F("sprite", "i16", "-1"), F("marker", "i8", "-1"),
					F("fuelMax", "u16", "0"), F("damageMax", "u16", "0"), F("speedMax", "u16", "0"), F("accel", "u8", "0"),
					F("weapons", "u8", "0"), F("soldiers", "u8", "0"), F("vehicles", "u8", "0"),
					F("costBuy", "u32", "0"), F("costRent", "u32", "0"), F("costSell", "u32", "0"),
					F("refuelItem", "ref:items"), F("repairRate", "u8", "1"), F("refuelRate", "u8", "1"),
					F("radarRange", "u16", "672"), F("sightRange", "u16", "1696"),
					F("transferTime", "u16", "0"), F("score", "i16", "0"),
					F("maxAltitude", "i8", "-1", comment: "TFTD: > -1 — корабль «только вода»"), F("maxItems", "u16", "0"),
					F("radarChance", "u8", "100"),
				},
				Flags = { "spacecraft" },
			});

			t.Add(new TableSchema
			{
				Name = "craftWeapons", Comment = "оружие кораблей (порядок — по listOrder launcher)",
				Sort = (rs, l) => l.OrderBy(kv => rs.ListOrder("items", Y.Str(Y.Get(kv.Value, "launcher")))).ToList(),
				Fields =
				{
					Name(),
					F("sprite", "i16", "-1"), F("sound", "i16", "-1"),
					F("damage", "u16", "0"), F("range", "u16", "0"), F("accuracy", "u8", "0"),
					F("reloadCautious", "u8", "0"), F("reloadStandard", "u8", "0"), F("reloadAggressive", "u8", "0"),
					F("ammoMax", "u16", "0"), F("rearmRate", "u8", "1"), F("projectileType", "u8", "2"), F("projectileSpeed", "u8", "0"),
					F("launcher", "ref:items"), F("clip", "ref:items"),
				},
				Flags = { "underwaterOnly" },
			});

			t.Add(new TableSchema
			{
				Name = "armors", Comment = "броня (порядок — по listOrder storeItem, без предмета — первыми)",
				Sort = (rs, l) => l.OrderBy(kv => rs.ListOrder("items", Y.Str(Y.Get(kv.Value, "storeItem")))).ToList(),
				Section = "armors",
				Fields =
				{
					Name("type"),
					Fn("storeItem", "u16", (e, k, rs) => Y.Str(Y.Get(e, "storeItem")) == "STR_NONE" ? "65534"
						: rs.Ref("items", Y.Get(e, "storeItem"), $"armors.{k}.storeItem").ToString(), "предмет; #FFFE — STR_NONE (бесконечно), #FFFF — нет"),
					Fn("corpseGeo", "ref:items", (e, k, rs) => Y.Get(e, "corpseItem") ?? Y.Get(e, "corpseGeo") ?? Y.List(Y.Get(e, "corpseBattle"))?.FirstOrDefault()),
					Fn("corpseBattle", "refs:items", (e, k, rs) => Y.Get(e, "corpseItem") != null ? new List<object> { Y.Get(e, "corpseItem") } : Y.Get(e, "corpseBattle")),
					F("frontArmor", "u8", "0"), F("sideArmor", "u8", "0"), F("rearArmor", "u8", "0"), F("underArmor", "u8", "0"),
					F("drawingRoutine", "u8", "0"), F("movementType", "u8", "0"), F("size", "u8", "1"), F("weight", "u8", "0"),
					F("deathFrames", "u8", "3"), F("forcedTorso", "u8", "0"),
					F("damageModifier", "nums100", comment: "10 x i16 * 100; нет элемента — 100"),
					F("stats", "stats"),
					F("spriteInv", "str", comment: "имя куклы инвентаря (MAN_*), пока через таблицу строк"),
				},
				Flags = { "allowInv=true", "constantAnimation", "drawBubbles" },
			});

			t.Add(new TableSchema
			{
				Name = "soldiers", Comment = "типы солдат",
				Fields =
				{
					Fn("name", "str", (e, k, rs) => k == "XCOM" ? "STR_SOLDIER" : k),
					F("requires", "refs:research"),
					F("minStats", "stats"), F("maxStats", "stats"), F("statCaps", "stats"),
					F("armor", "ref:armors"),
					F("costBuy", "u32", "0"), F("costSalary", "u32", "0"),
					F("standHeight", "u8", "0"), F("kneelHeight", "u8", "0"), F("floatHeight", "u8", "0"),
					F("femaleFrequency", "u8", "50"), F("value", "u8", "20"), F("transferTime", "u8", "0"),
				},
			});

			t.Add(new TableSchema
			{
				Name = "units", Comment = "юниты (порядок — по ключу, std::map)", SortByKey = true,
				Fields =
				{
					Name("type"),
					F("race", "str"), F("rank", "str"),
					F("stats", "stats"),
					F("armor", "ref:armors"),
					F("standHeight", "u8", "0"), F("kneelHeight", "u8", "0"), F("floatHeight", "u8", "0"),
					F("value", "u16", "0"), F("intelligence", "u8", "0"), F("aggression", "u8", "0"),
					F("energyRecovery", "u8", "30"), F("specab", "u8", "0"),
					F("spawnUnit", "ref:units"), F("meleeWeapon", "ref:items"),
				},
				Flags = { "livingWeapon", "capturable=true" },
			});

			t.Add(new TableSchema
			{
				Name = "alienRaces", Key = "id", Comment = "расы: члены по рангам 0..7",
				Fields = { Name("id"), F("members", "refs:units") },
			});

			t.Add(new TableSchema
			{
				Name = "countries", Comment = "страны (двоичные углы; широта < 0 — север)",
				Fields =
				{
					Name("type"),
					F("fundingBase", "u16", "0"), F("fundingCap", "u16", "0"),
					F("labelLon", "lon", "0"), F("labelLat", "lat", "0"),
					F("areas", "areas"),
				},
			});

			t.Add(new TableSchema
			{
				Name = "regions", Comment = "регионы (зоны — двоичные углы)",
				Fields =
				{
					Name("type"),
					F("cost", "u32", "0"), F("regionWeight", "u16", "0"),
					F("areas", "areas"),
					Fn("missionWeights", "nums", (e, k, rs) => Weights(rs, "alienMissions", Y.Get(e, "missionWeights"), $"regions.{k}.missionWeights"),
						"WeightedOptions: пары (alienMissions, вес) по имени миссии"),
					F("missionRegion", "ref:regions"),
					Fn("zones", "nums", (e, k, rs) => ((Dictionary<string, List<int>>)rs.Cache["zoneStarts"])[k].Cast<object>().ToList(),
						"missionZones: начала зон в zoneAreas, последний — конец (зон = n - 1)"),
					Fn("sortRank", "u8", (e, k, rs) => rs.SortRank("regions", k).ToString(), "место имени в std::map (порядок выбора WeightedOptions)"),
				},
			});

			t.Add(new TableSchema
			{
				Name = "ufos", Comment = "НЛО / USO",
				Fields =
				{
					Name("type"),
					F("size", "str", "STR_VERY_SMALL"),
					Fn("radius", "u8", (e, k, rs) => { string s = Y.Str(Y.Get(e, "size")) ?? "STR_VERY_SMALL";
						return s == "STR_VERY_SMALL" ? "2" : s == "STR_SMALL" ? "3" : s == "STR_MEDIUM_UC" ? "4" : s == "STR_LARGE" ? "5" : s == "STR_VERY_LARGE" ? "6" : "0"; },
						"радиус в перехвате"),
					F("sprite", "i16", "-1"), F("marker", "i8", "-1"), F("markerLand", "i8", "-1"), F("markerCrash", "i8", "-1"),
					F("damageMax", "u16", "0"), F("speedMax", "u16", "0"), F("power", "u16", "0"), F("range", "u16", "0"),
					F("score", "i16", "0"), F("reload", "u16", "0"), F("breakOffTime", "u16", "0"),
					F("sightRange", "u16", "268"), F("missionScore", "u16", "1"),
				},
			});

			t.Add(new TableSchema
			{
				Name = "ufopaedia", Key = "id", Comment = "статьи Уфопедии (порядок — раздел, затем listOrder)",
				Sort = (rs, l) =>
				{
					var sec = new Dictionary<string, int>(StringComparer.Ordinal);
					for (int i = 0; i < l.Count; i++) { string s = Y.Str(Y.Get(l[i].Value, "section")) ?? ""; if (!sec.ContainsKey(s)) sec[s] = i; }
					return l.Select((kv, i) => (kv, i)).OrderBy(p => sec[Y.Str(Y.Get(p.kv.Value, "section")) ?? ""]).ThenBy(p => p.i).Select(p => p.kv).ToList();
				},
				Fields =
				{
					Name("title"),
					F("section", "str"),
					F("type_id", "u8", "0", "type", "UFOPAEDIA_TYPE_*: 1..9 UFO, 10..17 TFTD"),
					Fn("image", "res", (e, k, rs) => Y.Get(e, "image_id") ?? ArmorImage(rs, k, Y.Int(Y.Get(e, "type_id"))), "картинка; UFO броня (тип 5) — кукла spriteInv+M0.SPK / .SPK"),
					F("text", "str"),
					F("weapon", "str"),
					Fn("textWidth", "u16", (e, k, rs) => Y.Get(e, "text_width") ?? (Y.Int(Y.Get(e, "type_id")) >= 10 ? "157" : "0")),
					F("requires", "refs:research"),
					Fn("rectStats", "nums", (e, k, rs) => RectList(Y.Get(e, "rect_stats")), "x, y, w, h"),
					Fn("rectText", "nums", (e, k, rs) => RectList(Y.Get(e, "rect_text")), "x, y, w, h"),
					Fn("rule", "u16", (e, k, rs) => ArticleRule(rs, k, Y.Int(Y.Get(e, "type_id"))).ToString(), "номер записи в таблице по типу статьи"),
				},
			});

			// ---- геоскейп: стратегия пришельцев (14_todo §3.17). Списки — «nums» (i16 в хвосте):
			// взвешенные — пары (номер, вес) в порядке имён (WeightedOptions = std::map<std::string>),
			// по месяцам — [месяц, n, (номер, вес) x n] ... по возрастанию месяца.
			t.Add(new TableSchema
			{
				Name = "invs", Key = "id", Comment = "секции инвентаря (inventories.rul): место на экране и сетка слотов",
				Fields =
				{
					Name("id"),
					F("x", "u16", "0"), F("y", "u16", "0"),
					F("type", "u8", "0", comment: "RuleInventory: 0 — сетка slots, 1 — рука (2x3), 2 — земля"),
					Fn("cols", "u8", (e, k, rs) => SlotSize(e, 0), "клеток в ширину (рука и земля — 0)"),
					Fn("rows", "u8", (e, k, rs) => SlotSize(e, 1), "клеток в высоту"),
					Fn("mask", "u16", (e, k, rs) => SlotMask(e), "занятые клетки сетки: бит (y * 4 + x)"),
				},
			});

			t.Add(new TableSchema
			{
				Name = "ufoTrajectories", Key = "id", Comment = "траектории НЛО",
				Fields =
				{
					F("groundTimer", "u16", "5", comment: "на земле groundTimer * 5 с"),
					F("waypoints", "nums", comment: "[зона, высота 0..4, скорость %] x n"),
					Fn("assault", "u8", (e, k, rs) => k == "__RETALIATION_ASSAULT_RUN" ? "1" : "0", "__RETALIATION_ASSAULT_RUN"),
				},
			});

			t.Add(new TableSchema
			{
				Name = "alienDeployments", Comment = "развёртывания: пока поля для геоскейпа (места миссий, базы пришельцев)",
				Fields =
				{
					Name("type"),
					F("markerName", "str", "STR_TERROR_SITE"), F("markerIcon", "i8", "-1"),
					Fn("durationMin", "u8", (e, k, rs) => Y.List(Y.Get(e, "duration"))?.ElementAtOrDefault(0) ?? "0", "часов"),
					Fn("durationMax", "u8", (e, k, rs) => Y.List(Y.Get(e, "duration"))?.ElementAtOrDefault(1) ?? "0"),
					F("despawnPenalty", "i16", "0"), F("points", "i16", "0"),
					Fn("genMission", "nums", (e, k, rs) => Weights(rs, "alienMissions", Y.Get(e, "genMission"), $"alienDeployments.{k}.genMission"),
						"пары (alienMissions, вес)"),
					F("genMissionFreq", "u8", "0"),
					F("alert", "str", "STR_ALIENS_TERRORISE"), F("alertBackground", "res", "BACK03.SCR"),
				},
			});

			t.Add(new TableSchema
			{
				Name = "alienMissions", Comment = "миссии пришельцев",
				Fields =
				{
					Name("type"),
					F("points", "i16", "0"), F("objective", "u8", "0", comment: "0 SCORE, 1 INFILTRATION, 2 BASE, 3 SITE, 4 RETALIATION, 5 SUPPLY"),
					F("spawnUfo", "ref:ufos"), F("spawnZone", "i8", "-1"), F("retaliationOdds", "i8", "-1"),
					F("siteType", "ref:alienDeployments"),
					Fn("waves", "nums", (e, k, rs) => Waves(rs, Y.Get(e, "waves"), $"alienMissions.{k}.waves"),
						"[ufo, count, trajectory, timer (мин), objective] x n; ufo: ufos | #4000 + alienDeployments | -1"),
					Fn("raceWeights", "nums", (e, k, rs) => MonthWeights(rs, "alienRaces", Y.Get(e, "raceWeights"), $"alienMissions.{k}.raceWeights", true),
						"по месяцам: пары (alienRaces, вес)"),
					Fn("missionWeights", "nums", (e, k, rs) =>
					{
						var l = new List<object>();
						var m = Y.Map(Y.Get(e, "missionWeights"));
						if (m != null) foreach (var kv in m.OrderBy(kv => Y.Int(kv.Key))) { l.Add(Y.Int(kv.Key)); l.Add(Y.Int(kv.Value)); }
						return l;
					}, "[месяц, вес] x n (getWeight; нет — вес 1)"),
					Fn("sortRank", "u8", (e, k, rs) => rs.SortRank("alienMissions", k).ToString(), "место имени в std::map"),
				},
			});

			t.Add(new TableSchema
			{
				Name = "missionScripts", Comment = "сценарий миссий по месяцам (determineAlienMissions)",
				Fields =
				{
					F("firstMonth", "i16", "0"), F("lastMonth", "i16", "-1"), F("label", "u8", "0"),
					F("conditionals", "nums"),
					Fn("missionWeights", "nums", (e, k, rs) => MonthWeights(rs, "alienMissions", Y.Get(e, "missionWeights"), $"missionScripts.{k}.missionWeights", false),
						"по месяцам: пары (alienMissions, вес)"),
					F("executionOdds", "u8", "100"), F("targetBaseOdds", "u8", "0"), F("startDelay", "u16", "0", comment: "минут"),
					Fn("raceWeights", "nums", (e, k, rs) => MonthWeights(rs, "alienRaces", Y.Get(e, "raceWeights"), $"missionScripts.{k}.raceWeights", false)),
					Fn("regionWeights", "nums", (e, k, rs) => MonthWeights(rs, "regions", Y.Get(e, "regionWeights"), $"missionScripts.{k}.regionWeights", false)),
					F("minDifficulty", "u8", "0"),
					Fn("researchTriggers", "nums", (e, k, rs) =>
					{
						var l = new List<object>();
						var m = Y.Map(Y.Get(e, "researchTriggers"));
						if (m != null) foreach (var kv in m.OrderBy(kv => Y.Str(kv.Key), StringComparer.Ordinal)) { l.Add(rs.Ref("research", kv.Key, $"missionScripts.{k}")); l.Add(Y.Bool(kv.Value) ? 1 : 0); }
						return l;
					}, "[research, 1 — исследовано / 0 — нет] x n"),
					F("maxRuns", "i16", "-1"), F("avoidRepeats", "u8", "0"),
					Fn("varIndex", "i8", (e, k, rs) => VarIndex(rs, Y.Str(Y.Get(e, "varName"))).ToString(), "varName: номер в отсортированном списке имён, -1 — нет"),
					Fn("siteType", "u8", (e, k, rs) => ScriptSiteType(rs, e) ? "1" : "0", "миссии с objective SITE (Mod: первая по имени)"),
				},
				Flags = { "useTable=true" },
			});

			t.Add(new TableSchema
			{
				Name = "zoneAreas", Comment = "области зон миссий всех регионов подряд (regions.zones — начала)", Source = ZoneAreas,
				Fields =
				{
					F("lonMin", "lon", "0"), F("lonMax", "lon", "0"), F("latMin", "lat", "0"), F("latMax", "lat", "0"),
					F("texture", "i8", "0", comment: "текстура глобуса (TFTD: -1 порт, -2 остров, -3 артефакт, -4 морской путь)"),
					F("name", "str", comment: "город"),
				},
			});

			t.Add(new TableSchema
			{
				Name = "globeTextures", Key = "id", Comment = "текстуры глобуса: развёртывания мест миссий (globe.textures)", Source = GlobeTextures,
				Fields =
				{
					F("id", "i8", "0"),
					Fn("deployments", "nums", (e, k, rs) => Weights(rs, "alienDeployments", Y.Get(e, "deployments"), $"globe.textures.{k}"), "пары (alienDeployments, вес)"),
				},
			});

			return t;
		}

		// WeightedOptions: {ключ: вес} -> пары (номер, вес) по возрастанию ключа, вес 0 выпадает.
		static List<object> Weights(RuleSet rs, string table, object map, string where)
		{
			var l = new List<object>();
			var m = Y.Map(map);
			if (m == null) return l;
			foreach (var p in m.Select(kv => (k: Y.Str(kv.Key), v: Y.Int(kv.Value))).Where(p => p.v > 0).OrderBy(p => p.k, StringComparer.Ordinal))
			{
				l.Add(rs.Ref(table, p.k, where));
				l.Add(p.v);
			}
			return l;
		}

		// {месяц: {ключ: вес}} -> [месяц, n, пары] ...; skipEmpty — как RuleAlienMission (пустые не хранит).
		static List<object> MonthWeights(RuleSet rs, string table, object map, string where, bool skipEmpty)
		{
			var l = new List<object>();
			var m = Y.Map(map);
			if (m == null) return l;
			foreach (var kv in m.OrderBy(kv => Y.Int(kv.Key)))
			{
				var w = Weights(rs, table, kv.Value, where);
				if (skipEmpty && w.Count == 0) continue;
				l.Add(Y.Int(kv.Key)); l.Add(w.Count / 2); l.AddRange(w);
			}
			return l;
		}

		// Волны миссии: ufo — НЛО, или #4000 + развёртывание (место миссии сразу), или -1 («dummy»).
		static List<object> Waves(RuleSet rs, object waves, string where)
		{
			var l = new List<object>();
			foreach (var w in Y.List(waves) ?? new List<object>())
			{
				string ufo = Y.Str(Y.Get(w, "ufo"));
				int u = rs.Find("ufos", ufo), d = rs.Find("alienDeployments", ufo);
				l.Add(u >= 0 ? u : d >= 0 ? 0x4000 + d : -1);
				l.Add(Y.Int(Y.Get(w, "count"), 1));
				int tr = rs.Ref("ufoTrajectories", Y.Get(w, "trajectory"), where);
				l.Add(tr == 0xFFFF ? -1 : tr);
				l.Add(Y.Int(Y.Get(w, "timer")));
				l.Add(Y.Bool(Y.Get(w, "objective")) ? 1 : 0);
			}
			return l;
		}

		static int VarIndex(RuleSet rs, string name)
		{
			if (string.IsNullOrEmpty(name)) return -1;
			var names = rs.Entries["missionScripts"].Select(kv => Y.Str(Y.Get(kv.Value, "varName"))).Where(s => !string.IsNullOrEmpty(s))
				.Distinct().OrderBy(s => s, StringComparer.Ordinal).ToList();
			if (names.Count > 2) rs.Warnings.Add($"missionScripts: {names.Count} varName > NVARS (2, state.h)");
			return names.IndexOf(name);
		}

		// Mod::loadAll: siteType — objective первой (по имени) миссии из missionWeights == OBJECTIVE_SITE (3).
		static bool ScriptSiteType(RuleSet rs, object e)
		{
			var names = new SortedSet<string>(StringComparer.Ordinal);
			foreach (var kv in Y.Map(Y.Get(e, "missionWeights")) ?? new Dictionary<object, object>())
				foreach (var w in Y.Map(kv.Value) ?? new Dictionary<object, object>()) names.Add(Y.Str(w.Key));
			if (names.Count == 0) return false;
			int i = rs.Find("alienMissions", names.Min);
			return i >= 0 && Y.Int(Y.Get(rs.Entries["alienMissions"][i].Value, "objective")) == 3;
		}

		// Области missionZones всех регионов подряд; начала зон — в Cache["zoneStarts"] для regions.zones.
		static List<KeyValuePair<string, object>> ZoneAreas(RuleSet rs)
		{
			var list = new List<KeyValuePair<string, object>>();
			var starts = new Dictionary<string, List<int>>(StringComparer.Ordinal);
			foreach (var reg in rs.Entries["regions"])
			{
				var st = new List<int>();
				foreach (var zone in Y.List(Y.Get(reg.Value, "missionZones")) ?? new List<object>())
				{
					st.Add(list.Count);
					foreach (var a in Y.List(zone) ?? new List<object>())
					{
						var v = Y.List(a);
						if (v == null || v.Count < 4) continue;
						bool swap = Y.Num(v[2]) > Y.Num(v[3]);   // MissionArea: latMin > latMax — поменять
						var d = new Dictionary<object, object>
						{
							["lonMin"] = v[0], ["lonMax"] = v[1],
							["latMin"] = swap ? v[3] : v[2], ["latMax"] = swap ? v[2] : v[3],
							["texture"] = v.Count >= 5 ? v[4] : "0",
							["name"] = v.Count >= 6 ? v[5] : null,
						};
						list.Add(new KeyValuePair<string, object>($"{reg.Key}#{list.Count}", d));
					}
				}
				st.Add(list.Count);
				starts[reg.Key] = st;
			}
			rs.Cache["zoneStarts"] = starts;
			return list;
		}

		static List<KeyValuePair<string, object>> GlobeTextures(RuleSet rs)
		{
			var l = new List<KeyValuePair<string, object>>();
			rs.Sections.TryGetValue("globe", out var g);
			foreach (var t in Y.List(Y.Get(g, "textures")) ?? new List<object>())
			{
				string id = Y.Str(Y.Get(t, "id"));
				if (id != null) l.Add(new KeyValuePair<string, object>(id, t));
			}
			return l;
		}

		// invs.slots: клетки сетки секции — размеры и маска занятых (бит y * 4 + x)
		static IEnumerable<List<object>> SlotCells(object e)
		{
			var l = Y.List(Y.Get(e, "slots"));
			if (l == null) yield break;
			foreach (var c in l) { var p = Y.List(c); if (p != null && p.Count >= 2) yield return p; }
		}

		static string SlotSize(object e, int axis)
		{
			int m = -1;
			foreach (var p in SlotCells(e)) m = Math.Max(m, int.Parse(Y.Str(p[axis])));
			return (m + 1).ToString();
		}

		static string SlotMask(object e)
		{
			int mask = 0;
			foreach (var p in SlotCells(e))
			{
				int x = int.Parse(Y.Str(p[0])), y = int.Parse(Y.Str(p[1]));
				if (x < 4 && y < 4) mask |= 1 << (y * 4 + x);
			}
			return mask.ToString();
		}

		// ArticleStateArmor: кукла spriteInventory + "M0.SPK", иначе + ".SPK" (что есть в пакетах)
		static object ArmorImage(RuleSet rs, string id, int type)
		{
			if (type != 5 || !rs.Index["armors"].TryGetValue(id, out var i)) return null;
			string inv = Y.Str(Y.Get(rs.Entries["armors"][i].Value, "spriteInv"));
			if (string.IsNullOrEmpty(inv)) return null;
			foreach (var n in new[] { inv + "M0.SPK", inv + ".SPK", inv })
				if (rs.ResFind(n) >= 0) return n;
			return null;
		}

		static List<object> RectList(object r) => r == null ? null : new List<object> { Y.Get(r, "x"), Y.Get(r, "y"), Y.Get(r, "width"), Y.Get(r, "height") };

		// id статьи = ключ правила: корабль, оружие, техника (unit), предмет, броня, постройка, НЛО.
		static int ArticleRule(RuleSet rs, string id, int type)
		{
			string table;
			switch (type)
			{
			case 1: case 11: table = "crafts"; break;
			case 2: case 12: table = "craftWeapons"; break;
			case 3: case 13: table = "units"; break;
			case 4: case 14: table = "items"; break;
			case 5: case 15: table = "armors"; break;
			case 6: case 16: table = "facilities"; break;
			case 9: case 17: table = "ufos"; break;
			default: return 0xFFFF;
			}
			return rs.Index[table].TryGetValue(id, out var i) ? i : 0xFFFF;
		}

		// Глобальные значения и стартовая база (ресурс RES_RULE_VARS):
		// u8 sec, min, hour, weekday, day, month; u16 year;
		// u32 initialFunding (тыс.), u32 costEngineer, u32 costScientist, u16 timePersonnel;
		// u16 alienFuel (предмет), u16 alienFuelAmount; u8 difficultyCoefficient[5];
		// i32 defeatScore, i32 defeatFunds (difficulty.rul; поражение, 14_todo §3.13);
		// стартовая база: u16 scientists, u16 engineers, u16 randomSoldiers;
		// u8 nFacilities, {u16 facility, u8 x, u8 y} x n;
		// u8 nCrafts, { u16 craft, u16 id, u16 fuel, u16 damage,
		//   u8 nWeapons, {u16 craftWeapon, u16 ammo} x n,
		//   u8 nItems, {u16 item, u16 qty} x n, u8 nVehicles, {u16 item, i16 ammo} x n } x n;
		// u16 nItems, {u16 item, u16 count} x n.
		public static byte[] Vars(Dictionary<string, object> s, RuleSet rs)
		{
			var o = new MemoryStream();
			void U8(double v) => o.WriteByte((byte)Math.Max(0, Math.Min(255, v)));
			void U16(double v) { int x = (int)Math.Max(0, Math.Min(0xFFFF, v)); o.WriteByte((byte)x); o.WriteByte((byte)(x >> 8)); }
			void U32(double v) { long x = (long)Math.Max(0, v); for (int i = 0; i < 4; i++) o.WriteByte((byte)(x >> (8 * i))); }
			object G(string k) => s.TryGetValue(k, out var v) ? v : null;
			var st = G("startingTime");
			U8(Y.Num(Y.Get(st, "second"))); U8(Y.Num(Y.Get(st, "minute"), 0)); U8(Y.Num(Y.Get(st, "hour"), 12));
			U8(Y.Num(Y.Get(st, "weekday"), 6)); U8(Y.Num(Y.Get(st, "day"), 1)); U8(Y.Num(Y.Get(st, "month"), 1)); U16(Y.Num(Y.Get(st, "year"), 1999));
			U32(Y.Num(G("initialFunding"))); U32(Y.Num(G("costEngineer"))); U32(Y.Num(G("costScientist"))); U16(Y.Num(G("timePersonnel")));
			var fuel = Y.List(G("alienFuel"));
			U16(fuel != null ? rs.Ref("items", fuel[0], "alienFuel") : 0xFFFF); U16(fuel != null && fuel.Count > 1 ? Y.Num(fuel[1]) : 0);
			var dc = Y.List(G("difficultyCoefficient"));
			for (int i = 0; i < 5; i++) U8(dc != null && i < dc.Count ? Y.Num(dc[i]) : i);
			void I32(double v) { long x = (long)v; for (int i = 0; i < 4; i++) o.WriteByte((byte)(x >> (8 * i))); }
			I32(Y.Num(G("defeatScore"))); I32(Y.Num(G("defeatFunds")));
			var sb = G("startingBase");
			U16(Y.Num(Y.Get(sb, "scientists"))); U16(Y.Num(Y.Get(sb, "engineers")));
			var rsol = Y.Get(sb, "randomSoldiers");
			U16(Y.Map(rsol) != null ? Y.Map(rsol).Values.Sum(v => Y.Num(v)) : Y.Num(rsol));
			var fac = Y.List(Y.Get(sb, "facilities")) ?? new List<object>();
			U8(fac.Count);
			foreach (var f in fac) { U16(rs.Ref("facilities", Y.Get(f, "type"), "startingBase.facilities")); U8(Y.Num(Y.Get(f, "x"))); U8(Y.Num(Y.Get(f, "y"))); }
			var cr = Y.List(Y.Get(sb, "crafts")) ?? new List<object>();
			U8(cr.Count);
			foreach (var c in cr)
			{
				U16(rs.Ref("crafts", Y.Get(c, "type"), "startingBase.crafts"));
				U16(Y.Num(Y.Get(c, "id"), 1)); U16(Y.Num(Y.Get(c, "fuel"))); U16(Y.Num(Y.Get(c, "damage")));
				var w = Y.List(Y.Get(c, "weapons")) ?? new List<object>();
				U8(w.Count);
				foreach (var x in w) { U16(rs.Ref("craftWeapons", Y.Get(x, "type"), "startingBase.crafts.weapons")); U16(Y.Num(Y.Get(x, "ammo"))); }
				var it = Y.Map(Y.Get(c, "items")) ?? new Dictionary<object, object>();
				U8(it.Count);
				foreach (var kv in it) { U16(rs.Ref("items", kv.Key, "startingBase.crafts.items")); U16(Y.Num(kv.Value)); }
				var ve = Y.List(Y.Get(c, "vehicles")) ?? new List<object>();
				U8(ve.Count);
				foreach (var x in ve) { U16(rs.Ref("items", Y.Get(x, "type"), "startingBase.crafts.vehicles")); int a = (int)Y.Num(Y.Get(x, "ammo")); o.WriteByte((byte)a); o.WriteByte((byte)(a >> 8)); }
			}
			var items = Y.Map(Y.Get(sb, "items")) ?? new Dictionary<object, object>();
			U16(items.Count);
			foreach (var kv in items) { U16(rs.Ref("items", kv.Key, "startingBase.items")); U16(Y.Num(kv.Value)); }
			return o.ToArray();
		}
	}
}
