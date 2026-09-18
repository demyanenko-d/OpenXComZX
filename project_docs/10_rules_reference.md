# Справочник по правилам OpenXcom (*.rul): поля, умолчания, семантика

Ванильный OpenXcom (не OXCE). Пути — относительно `REF/OpenXcom/src/`,
`file:line` — конструктор (умолчания) или `load()`. Нужен конвертеру правил
(`OxzConv`) и движку. «Смещение» — индекс спрайта/звука через
`Mod::loadOffsetNode` (Mod/Mod.cpp:682-752): для основного набора число как
есть, кроме `bulletSprite` (хранится ×35); `-1` — «нет».

## 0. Загрузка (Mod/Mod.cpp)

- Разделы: countries, regions, facilities, crafts, craftWeapons, items, ufos,
  `invs`, terrains, armors, soldiers, units, alienRaces, alienDeployments,
  research, manufacture, ufopaedia, startingBase, startingTime, глобальные
  скаляры, ufoTrajectories, alienMissions, alienItemLevels, MCDPatches,
  extraSprites/Sounds/Strings, interfaces, globe, constants, mapScripts,
  missionScripts, cutscenes, `musics`, …
- **Порядок файлов** — по имени (байтовая сортировка), `std::set`.
- Запись с уже существующим ключом **перезагружает тот же объект** (поля без
  значения сохраняют прежнее); `delete: ID` — удалить.
- Ключ записи: обычно `type`; research и manufacture — `name`; invs и
  alienRaces — `id`; terrains — `name`.
- **listOrder**: facilities, crafts, items, research, manufacture, ufopaedia —
  счётчик +100 на запись (первое ненулевое значение остаётся, если
  `listOrder:` не задан явно); invs — +10, перезаписывается при каждой загрузке.
- **sortLists()** (2480-2494): items, crafts, facilities, research,
  manufacture, invs — по listOrder; craftWeapons — по listOrder предмета
  `launcher`; armors — по listOrder `storeItem` (без предмета — первыми);
  ufopaedia — по порядку раздела, затем listOrder. **Без сортировки (порядок
  файла):** countries, regions, ufos, soldiers, alienRaces, deployments,
  missions, terrains. **По ключу (std::map):** units, musics, cutscenes,
  interfaces.
- Производные: `_finalResearch` — research с `unlockFinalMission: true`;
  `_psiRequirements` — `requires` первой постройки с psiLabs>0;
  минимальный радиус радара — наименьший ненулевой `radarRange` построек.

## 1. items (RuleItem.cpp 33-37, 54-144)

| ключ | тип | умолчание | примечание |
|---|---|---|---|
| type | string | — | |
| name | string | = type | STR_ |
| requires | list | [] | исследования; нужны **все**; закрывают покупку, снаряжение, применение |
| size | double | 0.0 | место на складе |
| costBuy / costSell | int | 0 / 0 | costBuy 0 — не покупается |
| transferTime | int | **24** | часы |
| weight | int | **3** | |
| bigSprite / floorSprite / handSprite | смещение | -1 / -1 / **120** | BIGOBS / FLOOROB / HANDOB |
| bulletSprite | смещение ×35 | -1 | «Projectiles» |
| fireSound / hitSound / meleeSound / meleeHitSound | смещение BATTLE.CAT | -1 / -1 / **39** / -1 | |
| hitAnimation / meleeAnimation | смещение | -1 / 0 | SMOKE / HIT |
| power | int | 0 | |
| compatibleAmmo | list | [] | предметы |
| damageType | int | 0 | §18 |
| accuracyAuto/Snap/Aimed/Melee | int | 0 | % |
| tuAuto/Snap/Aimed/Melee, tuUse | int | 0 | % от базовых TU, если не flatRate |
| clipSize | int | 0 | -1 — бесконечно |
| battleType | int | 0 | §18 |
| twoHanded, fixedWeapon | bool | false | fixed — HWP/турель |
| waypoints | int | 0 | |
| invWidth / invHeight | int | **1 / 1** | |
| painKiller, heal, stimulant, woundRecovery, healthRecovery, stunRecovery, energyRecovery | int | 0 | аптечка |
| recoveryPoints | int | 0 | очки при разборе |
| armor | int | **20** | предмет уничтожается, если сила взрыва > armor |
| turretType | int | **-1** | |
| recover | bool | **true** | |
| ignoreInBaseDefense, liveAlien | bool | false | |
| blastRadius | int | **-1** | -1 — вычисляется (ниже) |
| attraction | int | 0 | ИИ (TFTD) |
| flatRate, arcingShot | bool | false | |
| maxRange / aimRange / snapRange / autoRange / minRange / dropoff | int | 200 / 200 / 15 / 7 / 0 / 2 | |
| autoShots | int | **3** | |
| zombieUnit | string | "" | |
| strengthApplied / skillApplied / LOSRequired | bool | false / **true** / false | |
| underwaterOnly / landOnly | bool | false | TFTD: сверка с глубиной боя |
| specialType | int | **-1** | §18 |
| vaporColor / vaporDensity / vaporProbability | int | -1 / 0 / **15** | TFTD |

Радиус взрыва при blastRadius -1: IN — power/30+1; HE/STUN/SMOKE — power/20;
иначе 0; не больше 11. HWP — предмет fixedWeapon, тип которого есть и в
units; занимает armor.size² мест корабля (по умолчанию 4).

## 2. research (ключ `name`; RuleResearch.cpp 25, 34-57)

| ключ | тип | умолч. | примечание |
|---|---|---|---|
| lookup | string | "" | показать статью этого исследования и отметить его завершённым |
| cost | int | 0 | реальная стоимость = cost × RNG(50..150) / 100 |
| points | int | 0 | очки |
| dependencies | list | [] | нужны все, если тема не открыта через `unlocks` уже изученной |
| unlocks / getOneFree | list | [] | |
| requires | list | [] | нужны все; **cost обязан быть 0**; такие темы не видны и завершаются автоматически |
| needItem / destroyItem | bool | false | предмет того же id на складе |
| unlockFinalMission | bool | — | → `_finalResearch` |

Тема с cost 0 без `requires` изучается автоматически, как только доступна.

## 3. manufacture (ключ `name`; RuleManufacture.cpp 27-59)

category (STR_; `STR_CRAFT` — производятся корабли), requires, space, time
(инженеро-часы на штуку), cost, requiredItems (предметы **или корабли**),
producedItems (по умолчанию **{name: 1}**; явная карта заменяет целиком).

## 4. facilities (RuleBaseFacility.cpp 30, 47-84)

spriteShape / spriteFacility (BASEBITS, -1), lift / hyper / mind / grav
(bool; lift нельзя строить из меню), size (**1**), buildCost, buildTime
(дни), monthlyCost, storage, personnel, aliens, crafts, labs, workshops,
psiLabs, radarRange (морские мили), radarChance (%), defense, hitRatio (%),
fireSound / hitSound (GEO.CAT, **0**), mapName, requires.

## 5. crafts (RuleCraft.cpp 31, 51-103)

sprite (-1; BASEBITS 33+s, INTICON 11+s), marker (-1 → 1), fuelMax,
damageMax, speedMax (узлы), accel, weapons, soldiers, vehicles, costBuy,
costRent, costSell, refuelItem ("" — бесплатная заправка), repairRate (**1**),
refuelRate (**1**), radarRange (**672**), radarChance (100, не используется),
sightRange (**1696**), transferTime, score, battlescapeTerrainData, deployment,
spacecraft (финальная миссия), **maxAltitude (-1; TFTD:** корабль «только для
воды» при > -1, атакует НЛО с высотой ≤ maxAltitude и только над сушей… см.
DogfightState.cpp:1667), maxItems (0 — без ограничения), requires.

## 6. craftWeapons (RuleCraftWeapon.cpp 29, 45-72)

sprite (-1; BASEBITS 48+s, INTICON 5+s), sound (GEO.CAT, -1), damage, range
(км), accuracy (%), reloadCautious/Standard/Aggressive, ammoMax, rearmRate
(**1**), projectileType (**2**), projectileSpeed, launcher / clip (предметы),
underwaterOnly (TFTD, кодом не используется).

## 7. armors (Armor.cpp 31-40, 54-110)

spriteSheet, spriteInv, allowInv (**true**), corpseItem (задаёт corpseBattle
= [x] и corpseGeo = x), corpseBattle, corpseGeo, storeItem (`STR_NONE` —
бесконечно), specialWeapon, front/side/rear/underArmor, drawingRoutine,
drawBubbles (TFTD), movementType (0 ходьба, 1 полёт, 2 скольжение), size
(**1**), weight, stats (слияние: ненулевое перекрывает), damageModifier
(**float[10], по 1.0**; короткий список меняет первые N), loftempsSet,
loftemps (заменяет loftempsSet на [x]), deathFrames (**3**),
constantAnimation, forcedTorso, spriteFace/Hair/Rank/UtileGroup и …Color, units.

## 8. soldiers (RuleSoldier.cpp 32, 52-104)

type ("XCOM" → "STR_SOLDIER"), requires (пустые — стартовые типы),
minStats/maxStats/statCaps (слияние), armor, costBuy, costSalary,
standHeight/kneelHeight/floatHeight, femaleFrequency (**50**), value (**20**),
transferTime, deathMale/deathFemale (число или список), soldierNames.
Статы: tu, stamina, health, bravery, reactions, firing, throwing, strength,
psiStrength, psiSkill, melee.

## 9. units (Unit.cpp 30, 47-79)

race, rank (STR_), stats, armor, stand/kneel/floatHeight, value,
intelligence, aggression, energyRecovery (**30**), specab, spawnUnit,
livingWeapon, meleeWeapon, psiWeapon (**"ALIEN_PSI_WEAPON"**), capturable
(**true**), builtInWeaponSets, builtInWeapons (добавляется набором),
deathSound, aggroSound/moveSound (-1). `zombieUnit` в xcom2/units.rul
игнорируется.

## 10. alienRaces (ключ `id`)

`members` — по рангам: 0 командир, 1 лидер, 2 инженер, 3 медик, 4 навигатор,
5 солдат, 6 террорист, 7 террорист 2.

## 11. countries

fundingBase / fundingCap (тысячи; старт = RNG(base, 2·base)·1000),
labelLon / labelLat (градусы → радианы), areas ([lonMin, lonMax, latMin,
latMax], градусы → радианы, дописываются при перезагрузке). Страны без
областей пропускаются; каждой добавляется
`((initialFunding − сумма/1000) / nCountries) · 1000`. **Широта со знаком
минус на севере** (Нью-Йорк −40.75), долгота 0–360.

## 12. regions

cost, areas, missionZones (список зон; зона — список областей
`[lonMin, lonMax, latMin, latMax, (texture), (name)]`; texture без значения
не инициализирована; xcom1 −1 город / −2 остров, xcom2 −3/−4; точка с именем
— город), missionWeights (слияние, 0 удаляет), regionWeight, missionRegion.

## 13. ufos

size (**"STR_VERY_SMALL"**; радиус в перехвате: VERY_SMALL 2, SMALL 3,
MEDIUM_UC 4, LARGE 5, VERY_LARGE 6), sprite (-1), marker/markerLand/
markerCrash (-1 → 2/3/4), damageMax, speedMax, power, range, score, reload,
breakOffTime, sightRange (**268**), missionScore (**1**),
battlescapeTerrainData.

## 14. ufopaedia (ключ `id`)

Базовые ключи: id (заголовок = id), section (`STR_NOT_AVAILABLE` скрывает),
requires (все; пусто — видна всегда), title, listOrder. **id статьи = ключ
правила** (корабль, оружие корабля, предмет, броня, постройка, НЛО;
техника — unit и item). Поиск статьи по исследованию: по id, по id+"_UC",
затем по статье, в `requires` которой есть это исследование.

| type_id | тип | доп. ключи |
|---|---|---|
| 1 | CRAFT | image_id, rect_stats, rect_text, text |
| 2 | CRAFT_WEAPON | image_id, text |
| 3 | VEHICLE | text, weapon |
| 4 / 5 / 6 / 9 / 8 | ITEM / ARMOR / BASE_FACILITY / UFO / TEXT | text |
| 7 | TEXTIMAGE | image_id, text, text_width (0) |
| 10–17 | TFTD, TFTD_CRAFT, _CRAFT_WEAPON, _VEHICLE, _ITEM, _ARMOR, _BASE_FACILITY, _USO | type_id, image_id, text, text_width (**157**), weapon |

xcom1 — типы 1–9, xcom2 — только 10–17. Порядок разделов — по минимальному
listOrder статей раздела (первое появление в файле).

## 15. musics

type, name (файл; по умолчанию type), catPos (позиция в ADLIB.CAT, дальше —
AINTRO.CAT), normalization (0.76). `playMusic(name)` выбирает случайный трек,
ключ которого **содержит** name.

## 16. invs (ключ `id`)

x, y, type (0 слот, 1 рука, 2 земля), slots ([x,y]), costs (карта «секция →
TU», должна содержать все секции). SLOT 16x16, рука 2x3 слота.

## 17. Глобальные ключи

startingBase (слияние по верхним ключам: facilities {type, x, y}, crafts
{type, weapons, items, vehicles…}, items {id: n}, scientists, engineers,
randomSoldiers — число или карта), startingTime (xcom1: 1999, день недели 6;
xcom2: 2040, день недели 1), costEngineer, costScientist, timePersonnel,
initialFunding (тысячи), alienFuel (xcom1 [STR_ELERIUM_115, 50], xcom2
[STR_ZRBITE, 50]), turnAIUseGrenade/Blaster (3/3), defeatScore/Funds,
difficultyCoefficient (0..4), aimAndArmorMultipliers (0.5,1,1,1,1),
statGrowthMultipliers, alienItemLevels, constants (звуки, курсоры 252/144,
damageRange 100, explosiveDamageRange 50, fireDamageRange [5,10], музыка
разбора «GMMARS»).

## 18. Перечисления

- BattleType: 0 NONE, 1 FIREARM, 2 AMMO, 3 MELEE, 4 GRENADE, 5 PROXIMITYGRENADE,
  6 MEDIKIT, 7 SCANNER, 8 MINDPROBE, 9 PSIAMP, 10 FLARE, 11 CORPSE.
- ItemDamageType: 0 NONE, 1 AP, 2 IN, 3 HE, 4 LASER, 5 PLASMA, 6 STUN, 7 MELEE,
  8 ACID, 9 SMOKE (индекс `damageModifier` брони).
- CraftWeaponProjectileType: 0 STINGRAY, 1 AVALANCHE, 2 CANNON, 3 FUSION, 4 LASER, 5 PLASMA.
- MovementType: 0 WALK, 1 FLY, 2 SLIDE. ForcedTorso: 0 по полу, 1 муж., 2 жен.
- SpecialAbility: 0 NONE, 1 EXPLODEONDEATH, 2 BURNFLOOR, 3 BURN_AND_EXPLODE.
- SpecialTileType: 0 TILE … 14 MUST_DESTROY (MapData.h:27-41).

## 19. Отличия TFTD (xcom2)

items: attraction, underwaterOnly, vaporColor, vaporDensity; craftWeapons:
underwaterOnly; crafts: maxAltitude, нет requires; armors: corpseItem,
drawBubbles, loftemps; units: builtInWeapons, meleeWeapon; ufopaedia: только
type_id 10–17 (text_width, weapon); regions: текстуры −3/−4; terrains:
`depth: [min, max]` (глубина 0 запрещает underwaterOnly, >0 — landOnly).
Countries, research, manufacture, facilities, soldiers, invs — одинаковые.
