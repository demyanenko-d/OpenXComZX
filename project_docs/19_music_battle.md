# Музыка: наземный бой и экраны вокруг него

Разбор оригинала (`REF/OpenXcom/src`, правила `REF/OpenXcom/bin/standard/xcom1`
и `xcom2`): **где и когда** играет музыка на стороне тактического боя — от
брифинга до разбора миссии. Парный документ по геоскейпу —
[18_music_events.md](18_music_events.md), плеер и формат потока —
[20_music_player.md](20_music_player.md). Общие решения по звуку —
[11](11_sound.md), поток OPL3 и `MUSIC.PAK` — [09 §7](09_converter.md),
ход боя и экраны — [16](16_battlescape_plan.md).

Все утверждения — со ссылкой `файл:строка` в `REF/`. Номера типов музыки для
порта — строки `OxzConv/Data/music_types.txt`, ресурс = `#0300 + номер`.

Главный вывод: **внутри боя музыка не меняется никогда**. Тема выбирается один
раз при генерации карты и играет до конца этапа; все события боя (ходы,
скрытое перемещение, паника, пси, инвентарь, миникарта, меню) музыку не трогают.

---

## 1. Модель оригинала в двух словах

Движок не имеет «музыкального автомата»: есть только команда
`Mod::playMusic(имя)` (`src/Mod/Mod.cpp:525`), которую вызывают отдельные
экраны. Ключевое:

- **Музыка никогда не останавливается при смене экрана.** Кто не вызвал
  `playMusic`, тот оставляет играть предыдущий трек (`Music::stop()` вызывается
  только из `Game::~Game` — `src/Engine/Game.cpp:117`, из `StartState.cpp:131`
  и `VideoState.cpp:542`).
- **Повторный запрос того же имени игнорируется** —
  `if (!Options::mute && _playingMusic != name)` (`Mod.cpp:527`). Именно так
  музыка «не сбивается» при возврате из инвентаря, миникарты, меню.
- **Смена имени = трек с начала**: `Music::play` сначала делает `stop()`
  (`src/Engine/Music.cpp:86-98`). Позиция нигде не запоминается.
- Всего в движке 12 точек вызова `playMusic`, из них к бою относятся 4:
  `BattlescapeState.cpp:460,464`, `BriefingState.cpp:174`,
  `DebriefingState.cpp:635,639`.

---

## 2. Вход в бой

### 2.1 Цепочка экранов

| Шаг | Код | Музыка |
|---|---|---|
| Геоскейп → «высадиться» | `Geoscape/ConfirmLandingState.cpp:161` (`new BriefingState(_craft)`) | не трогает (играет `GMGEO`) |
| Оборона базы | `Geoscape/GeoscapeState.cpp:2222` (`new BriefingState(0, base)`) | не трогает |
| Сидония/T'leth (старт финала) | `Geoscape/ConfirmCydoniaState.cpp:105` | не трогает |
| Генерация карты | `Battlescape/BattlescapeGenerator.cpp:631` → `setMusic(ruleDeploy, false)` | **запоминает** тему в `SavedBattleGame` |
| Брифинг | `Battlescape/BriefingState.cpp:174` | играет тему брифинга |
| Кнопка OK → бой | `BriefingState.cpp:182-188` (`new BattlescapeState`) | |
| Конструктор боя | `Battlescape/BattlescapeState.cpp:457-465` | играет тактическую тему |

Важно: генератор отрабатывает **до** брифинга, но проигрывается сначала музыка
брифинга, потом тактическая. Тема боя от темы брифинга независима.

### 2.2 Музыка брифинга

`BriefingState` ищет `AlienDeployment` по типу миссии; если его нет (обычные
сбитые/севшие НЛО — типы `STR_UFO_CRASH_RECOVERY`/`STR_UFO_GROUND_ASSAULT` в
`alienDeployments.rul` отсутствуют), берётся развёртывание по типу самого НЛО
(`BriefingState.cpp:64-76`). Далее:

- развёртывания нет совсем → `_musicId = "GMDEFEND"`, фон `BACK16.SCR`
  (`BriefingState.cpp:80-84`);
- развёртывание есть → `_musicId = data.music` из блока `briefing:`
  (`BriefingState.cpp:88-95`), значение по умолчанию структуры
  `BriefingData` — тоже `"GMDEFEND"` (`src/Mod/AlienDeployment.h:49`).
- Если в блоке `briefing:` задан `cutscene:`, то **вместо музыки** брифинга
  запускается ролик, и его музыка приходит из `cutscenes.rul`
  (`BriefingState.cpp:161-176`). В ванили так только у TFTD `STR_TLETH_P1`
  (`xcom2/alienDeployments.rul:2586` → `cutscene: tleth` → `GMSIG`,
  `xcom2/cutscenes.rul`). При повторном заходе в брифинг `_cutsceneId`
  обнуляется, и играет уже `_musicId` (`BriefingState.cpp:170`).

Таблица брифингов ванили:

| Игра | Миссия | Музыка брифинга | Где в правилах |
|---|---|---|---|
| UFO | сбитое/севшее НЛО (все типы) | `GMDEFEND` (умолчание) | `AlienDeployment.h:49` |
| UFO | терроризирование города | `GMENBASE` | `xcom1/alienDeployments.rul:839` |
| UFO | штурм базы пришельцев | `GMDEFEND` (умолчание — блок `briefing:` есть, `music:` нет) | `xcom1/alienDeployments.rul:843+` |
| UFO | оборона базы X-COM | `GMENBASE` | `xcom1/alienDeployments.rul:1167` |
| UFO | Сидония, посадка | `GMNEWMAR` | `xcom1/alienDeployments.rul:1244` |
| UFO | Сидония, финальный штурм | `GMNEWMAR` | `xcom1/alienDeployments.rul:1402` |
| TFTD | все НЛО/корабли/терроры/спасения | `GMISPOSH` | `xcom2/alienDeployments.rul:31,83,…,1660` |
| TFTD | место артефакта, колония (оба этапа) | `GMATTBAS` | `xcom2/alienDeployments.rul:1806,1957,2138,2306` |
| TFTD | оборона базы X-COM | `GMDEFBAS` | `xcom2/alienDeployments.rul:2482` |
| TFTD | T'leth, все три этапа | `GMBIGMAR` (у P1 перебивается роликом `tleth` → `GMSIG`) | `xcom2/alienDeployments.rul:2583,2725,2866` |

### 2.3 Выбор тактической темы

`BattlescapeGenerator::setMusic(AlienDeployment*, bool nextStage)`
(`BattlescapeGenerator.cpp:3053-3066`) — единственное место выбора:

```
1. непустой список deployment->getMusic()  → случайный из него
2. иначе непустой terrain->getMusic()      → случайный из него
3. иначе если nextStage                    → сбросить в "" (пусто)
4. иначе                                   → оставить как было
```

Случайность — `RNG::generate(0, size-1)` (обычный игровой ГСЧ, попадает в
сид сохранения). Результат кладётся в `SavedBattleGame::_music`
(`src/Savegame/SavedBattleGame.cpp:2033`) и **сохраняется в save**
(`SavedBattleGame.cpp:343` чтение, `:473` запись) — при загрузке боя тема та же.

Конструктор боя (`BattlescapeState.cpp:457-465`):

```cpp
if (_save->getMusic().empty())  playMusic("GMTACTIC");
else                            playMusic(_save->getMusic());
```

Где берутся списки:

| Источник | Поле правил | Чтение | Что в ванили |
|---|---|---|---|
| Развёртывание | `music:` (список, 4 пробела отступа) | `src/Mod/AlienDeployment.cpp:169`, `:387` | **не используется ни в UFO, ни в TFTD** (проверено `grep "^    music:"` по обоим `alienDeployments.rul`) |
| Террейн | `music:` (список) | `src/Mod/RuleTerrain.cpp:83-85`, `:249` | UFO — нет ни у одного террейна; TFTD — по одному значению у всех 22 |

Отсюда:

- **UFO**: террейны музыки не задают → `_music` всегда `""` → в бою всегда
  `GMTACTIC` (`BattlescapeState.cpp:460`). Никакой зависимости от местности,
  миссии и этапа нет.
- **TFTD**: тема берётся из террейна — `GMTACWET` или `GMTACDRY`
  (`xcom2/terrains.rul`):

| `GMTACWET` (под водой) | `GMTACDRY` (на суше/на борту) |
|---|---|
| SEABED, PIPES, PLANE, ATLAN, MU, GAL, MSUNK, VOLC, CORAL, ALART, ENTRY, GRUNGE, A_BASE, ALSHIP, LEVEL, CRYPT | PORT, ISLAND, CARGO, XBASES, LINERT, LINERB |

То есть у TFTD «глубина» влияет на музыку **косвенно**: не через
`_save->getDepth()`, а через то, какой террейн выбран для миссии. Прямой
проверки глубины в коде музыки нет (`setDepth` — отдельная функция,
`BattlescapeGenerator.cpp:3030`).

Террейн миссии выбирается либо из `terrains:` развёртывания
(`BattlescapeGenerator.cpp:560-564`), либо по текстуре глобуса для
сбитых/севших НЛО (`:574`). Соответствие «развёртывание → террейн → музыка»
для особых миссий — в §4.

---

## 3. Внутри боя: музыка не меняется

Проверено поиском по всему `src/Battlescape/`: вызовы `playMusic` есть только в
`BattlescapeState.cpp:460,464`, `BriefingState.cpp:174`,
`DebriefingState.cpp:635,639`. Ни одно из перечисленного музыку не трогает:

| Событие/экран | Код | Музыка |
|---|---|---|
| Конец хода, «TURN N / SIDE» | `Battlescape/NextTurnState.cpp` (весь файл, `playMusic` нет) | не меняется |
| Ход пришельцев, ход гражданских | `BattlescapeGame::endTurn`, `SavedBattleGame::endTurn` | не меняется |
| «HIDDEN MOVEMENT» | это просто надпись на поверхности `_message`, `Battlescape/Map.cpp:247` | не меняется |
| Паника/берсерк бойца | `BattlescapeGame::handlePanickingPlayer`, инфобоксы | не меняется |
| Пси-атака (своя и вражеская) | `PsiAttackBState`, `UnitPanicBState` | не меняется |
| Инвентарь в бою | `Battlescape/InventoryState.cpp` | не меняется |
| Миникарта | `Battlescape/MiniMapState.cpp` | не меняется |
| Меню паузы/опции/сохранение/загрузка в бою | `src/Menu/*` (`playMusic` только в `SlideshowState.cpp:75`) | не меняется |
| Возврат в бой из любого окна | `BattlescapeState::init` (`:492`) музыку не ставит — она выставлена только в конструкторе | продолжает играть |

Единственный «фоновый звук», зависящий от миссии, — зацикленный эффект
`ambience` террейна (у TFTD № 67 — шум воды): запускается в
`BattlescapeState::init` (`:494-498`), останавливается в `finishBattle`
(`BattlescapeState.cpp:2020-2023`), задаётся в
`BattlescapeGenerator.cpp:2011` из `RuleTerrain::getAmbience`
(`RuleTerrain.cpp:231`). Это SFX, не музыка, но канал он занимает постоянно —
для порта учесть в режимах 2–4 ([11](11_sound.md)).

---

## 4. Особые миссии

| Миссия | Брифинг | Бой (UFO) | Бой (TFTD) |
|---|---|---|---|
| Сбитое/севшее НЛО | `GMDEFEND` / `GMISPOSH` | `GMTACTIC` | `GMTACWET` (террейн по текстуре глобуса — все «мокрые») |
| Терроризирование города / порта / острова | `GMENBASE` / `GMISPOSH` | `GMTACTIC` (террейн URBAN) | `GMTACDRY` (PORT, ISLAND) |
| Спасение корабля (TFTD, 2 этапа) | `GMISPOSH` | — | `GMTACDRY` (CARGO, LINERT/LINERB) |
| Место артефакта (TFTD, 2 этапа) | `GMATTBAS` | — | `GMTACWET` (ALART, GRUNGE) |
| База пришельцев / колония | `GMDEFEND` (умолчание!) / `GMATTBAS` | `GMTACTIC` (UBASE) | `GMTACWET` (ENTRY, A_BASE) |
| **Оборона базы X-COM** | `GMENBASE` / `GMDEFBAS` | `GMTACTIC` (XBASE) | **`GMTACDRY`** (XBASES) |
| Сидония, посадка (этап 1) | `GMNEWMAR` | `GMTACTIC` (MARS) | — |
| Сидония, финал (этап 2) | `GMNEWMAR` | `GMTACTIC` (UBASE) | — |
| T'leth P1/P2/P3 | ролик `GMSIG` / `GMBIGMAR` / `GMBIGMAR` | — | `GMTACWET` (ALSHIP, LEVEL, CRYPT) |

Списки террейнов — `terrains:` в `alienDeployments.rul` обеих игр
(UFO: `STR_TERROR_MISSION: URBAN`, `STR_ALIEN_BASE_ASSAULT: UBASE`,
`STR_BASE_DEFENSE: XBASE`, `STR_MARS_CYDONIA_LANDING: MARS`,
`STR_MARS_THE_FINAL_ASSAULT: UBASE`).

Заметные «несимметричности» оригинала, которые надо перенести как есть:

- в UFO **музыка боя одна на всё** — `GMTACTIC`; `GMENBASE`, `GMDEFEND`,
  `GMNEWMAR` звучат только на брифинге;
- брифинг штурма базы пришельцев в UFO играет `GMDEFEND`, а не `GMENBASE`
  (в блоке `briefing:` просто нет ключа `music:`);
- у TFTD оборона **своей** базы идёт под «сухую» тему `GMTACDRY`, хотя брифинг
  — `GMDEFBAS`.

### 4.1 Второй этап (`nextStage`)

`BattlescapeState::finishBattle` (`BattlescapeState.cpp:2013`): если у
развёртывания есть `nextStage:` и в зоне выхода кто-то есть
(`:2042-2051`), то бой **не** заканчивается:

```
_save->setMissionType(nextStage);
BattlescapeGenerator::nextStage();      // :193, внутри setMusic(ruleDeploy, true) — :535
popState();                             // убрать BattlescapeState
pushState(new BriefingState(0, 0));     // брифинг второго этапа
```

Дальше брифинг второго этапа → новый `BattlescapeState` → новая тактическая
тема. Отличие от старта: флаг `nextStage = true`, поэтому при отсутствии
музыки и у развёртывания, и у террейна тема **сбрасывается в `""`**
(`BattlescapeGenerator.cpp:3063-3065`) — то есть у UFO второй этап Сидонии
снова получает `GMTACTIC` (а не «донашивает» тему первого этапа).

Развёртывания с `nextStage` в ванили: UFO — `STR_MARS_CYDONIA_LANDING`
(`xcom1/alienDeployments.rul:1237`); TFTD — корабли, место артефакта, колония,
T'leth P1/P2 (`xcom2/alienDeployments.rul:1226,1511,1802,2133,2574,2717`).

Практически музыка между этапами меняется только у TFTD: `ALART`(wet) →
`GRUNGE`(wet) — не меняется; `LINERT`(dry) → `LINERB`(dry) — не меняется;
`ENTRY`(wet) → `A_BASE`(wet) — не меняется. То есть в ванили тема этапа 2
совпадает с темой этапа 1, но **трек перезапускается с начала** — новый
`BattlescapeState` вызывает `playMusic` с тем же именем… и ничего не делает,
потому что `_playingMusic` уже равен ему (`Mod.cpp:527`). А вот брифинг между
этапами тему сменит (`GMISPOSH`/`GMATTBAS`/`GMBIGMAR`), и после него
тактическая тема действительно начнётся заново.

---

## 5. Конец боя

`BattlescapeState::finishBattle(bool abort, int inExitArea)`
(`BattlescapeState.cpp:2013-2096`) — общая точка выхода; её зовут
`AbortMissionState.cpp:205` (отступление), `BattlescapeGame.cpp:485,494,497,503`
(все живы/все мертвы/цель уничтожена/лимит ходов), `NextTurnState.cpp:180`.

Ветка без `nextStage` (`:2053-2095`):

1. `popState()` — убрать бой; `pushState(new DebriefingState)` (`:2058`).
2. Выбрать ролик по исходу (`:2059-2074`): `abortCutscene` при отступлении,
   `loseCutscene` при `inExitArea == 0` (никто не выжил), иначе `winCutscene`.
3. Если ролик задан — `pushState(new CutsceneState(cutscene))` (`:2080`) и,
   для `winGame`/`loseGame`, запись концовки (`:2082-2089`).

Ролики есть только у финальных миссий: UFO — `STR_MARS_CYDONIA_LANDING`
(`loseCutscene/abortCutscene: loseGame`, `xcom1:1239-1240`) и
`STR_MARS_THE_FINAL_ASSAULT` (`winGame/loseGame/loseGame`, `xcom1:1396-1398`);
TFTD — `STR_TLETH_P1..P3` (`xcom2:2577,2719,2859`).

`CutsceneState::init` (`src/Menu/CutsceneState.cpp:51`): сам себя снимает со
стека (`:55`), а для `winGame`/`loseGame` вызывает
`_game->setState(new StatisticsState)` (`:62`) — это **очищает весь стек**,
поэтому подготовленный `DebriefingState` умирает, не показавшись и не сыграв
своей музыки. Затем поверх кладётся слайдшоу (`:95`), которое и играет
`GMWIN` / `GMLOSE` (`src/Menu/SlideshowState.cpp:75`, `musicId` из
`cutscenes.rul` обеих игр).

### 5.1 Разбор миссии (Debriefing)

`DebriefingState::init` (`DebriefingState.cpp:318`, тело выполняется один раз
по флагу `_initDone`) считает итоговый счёт `total`
(`:348` объявление, `:359` накопление), затем (`:618-640`):

```cpp
_positiveScore = (total > 0);
playMusic(_positiveScore ? Mod::DEBRIEF_MUSIC_GOOD : Mod::DEBRIEF_MUSIC_BAD);
```

Константы (`src/Mod/Mod.cpp:174-175`, переопределяются ключами
`goodDebriefingMusic`/`badDebriefingMusic`, `Mod.cpp:1136-1137`):

| Игра | Хороший счёт (`total > 0`) | Плохой счёт | Источник |
|---|---|---|---|
| UFO | `GMMARS` | `GMMARS` | умолчание `Mod.cpp:174-175` (в `xcom1` не переопределено) |
| TFTD | `GMDEBGOO` | `GMDEBPOR` | `xcom2/vars.rul:44-45` |

То есть **в UFO разбор всегда под одну тему** независимо от исхода; разделение
«хорошо/плохо» есть только у TFTD.

### 5.2 Что идёт после разбора

`DebriefingState::btnOkClick` (`:683`) снимает себя и кладёт на стек пачку
экранов (`:692-730`); показываются они в обратном порядке, последним
закрывается `CommendationLateState`:

| Экран | Класс/интерфейс | Музыка |
|---|---|---|
| Автосохранение | `SaveGameState` | нет |
| Продажа/склад, карантин, «не хватает снаряжения» | `SellState`, `ManageAlienContainmentState`, `CannotReequipState` | нет |
| **Повышения бойцов** | `Battlescape/PromotionsState.cpp:51` (`promotions`) | **нет** — продолжает играть тема разбора |
| Награды живым | `CommendationState.cpp:49` (`commendations`) | нет |
| Награды посмертно | `CommendationLateState.cpp:49` (`commendationsLate`) | **`GMLOSE` (UFO) / `GMDEBPOR` (TFTD)** — `xcom1/interfaces.rul:695`, `xcom2/interfaces.rul:726` |
| Возврат на геоскейп | `GeoscapeState::init` | `GMGEO` (см. [18](18_music_events.md)) |

Музыка экрана берётся из правил интерфейса в `State::init`
(`src/Engine/State.cpp:261-263`) — общий механизм: у кого в `interfaces.rul`
есть ключ `music:`, тот ставит её каждый раз, когда становится верхним.
Во всей ванили таких интерфейсов всего три на игру: `mainMenu`,
`commendationsLate`, `soldierMemorial`. Никакой боевой экран (`battlescape`,
`debriefing`, `promotions`, `commendations`) музыку через интерфейс не задаёт.

Смежное (мемориал павших, `Basescape/SoldierMemorialState.cpp:138`): экран
входит под `GMLOSE`/`GMDEBPOR` (интерфейс `soldierMemorial`,
`xcom1/interfaces.rul:1146`), а при выходе **сам** возвращает `GMGEO`.

---

## 6. Сводная таблица «событие → тип музыки → код оригинала»

| Состояние/событие | UFO | TFTD | Где в оригинале |
|---|---|---|---|
| Брифинг: НЛО (сбитое/севшее) | `GMDEFEND` | `GMISPOSH` | `BriefingState.cpp:80-95`, `AlienDeployment.h:49` |
| Брифинг: террор | `GMENBASE` | `GMISPOSH` | `xcom1:839`, `xcom2:946,1087` |
| Брифинг: база пришельцев | `GMDEFEND` | `GMATTBAS` | `xcom1:843+`, `xcom2:2138` |
| Брифинг: оборона базы X-COM | `GMENBASE` | `GMDEFBAS` | `xcom1:1167`, `xcom2:2482` |
| Брифинг: финал (Сидония / T'leth) | `GMNEWMAR` | `GMBIGMAR` (P1 — ролик `GMSIG`) | `xcom1:1244,1402`, `xcom2:2583,2725,2866` |
| Бой: любой обычный | `GMTACTIC` | `GMTACWET`/`GMTACDRY` по террейну | `BattlescapeState.cpp:458-465`, `BattlescapeGenerator.cpp:3053` |
| Бой: смена хода, скрытое перемещение, паника, пси | — без изменений — | | `NextTurnState.cpp`, `Map.cpp:247` |
| Бой: инвентарь, миникарта, меню, сохранение | — без изменений — | | `Mod.cpp:527` (тот же трек не перезапускается) |
| Второй этап миссии | заново выбранная тема после брифинга | то же | `BattlescapeState.cpp:2042-2051`, `BattlescapeGenerator.cpp:535` |
| Разбор: счёт > 0 | `GMMARS` | `GMDEBGOO` | `DebriefingState.cpp:633-636`, `Mod.cpp:174`, `xcom2/vars.rul:44` |
| Разбор: счёт ≤ 0 | `GMMARS` | `GMDEBPOR` | `DebriefingState.cpp:637-640`, `xcom2/vars.rul:45` |
| Повышения, награды живым | — без изменений — | | `PromotionsState.cpp:51`, `CommendationState.cpp:49` |
| Награды посмертно | `GMLOSE` | `GMDEBPOR` | `State.cpp:261-263`, `xcom1/interfaces.rul:695`, `xcom2:726` |
| Победа в финальной миссии | `GMWIN` | `GMWIN` | `BattlescapeState.cpp:2072-2084` → `SlideshowState.cpp:75`, `cutscenes.rul` |
| Провал финальной миссии / отступление с неё | `GMLOSE` | `GMLOSE` | `BattlescapeState.cpp:2064-2068`, `cutscenes.rul` |
| Возврат на геоскейп | `GMGEO` (случайный из `GMGEO1..10`) | то же | `GeoscapeState.cpp:521-525` ([18](18_music_events.md)) |

Все перечисленные типы уже есть в `OxzConv/Data/music_types.txt`:
`GMDEFEND`, `GMENBASE`, `GMNEWMAR`, `GMTACTIC`, `GMMARS`, `GMWIN`, `GMLOSE`
(UFO); `GMISPOSH`, `GMATTBAS`, `GMDEFBAS`, `GMBIGMAR`, `GMSIG`, `GMTACWET`,
`GMTACDRY`, `GMDEBGOO`, `GMDEBPOR` (TFTD).

---

## 7. Механика, которую надо повторить

### 7.1 `Mod::playMusic(name, id)` — `Mod.cpp:525-552`

1. Молчит, если `_playingMusic == name` (`:527`) — **главная защита от
   перезапуска трека** при возврате из подчинённых окон.
2. `loop`: `-1` (бесконечно) для всего, кроме `GMSTORY`, `GMWIN`, `GMLOSE`
   при `musicAlwaysLoop = false` — им `loop = 0` (`:529-534`).
   **Но для Adlib-музыки этот параметр игнорируется**: `AdlibMusic::play(int)`
   (`src/Engine/AdlibMusic.cpp:121`) не принимает его во внимание, а цикл
   задаётся самими данными `ADLIB.CAT` — опкод `0xC0` с аргументом `0x7E`
   («LOOP») перезапускает трек изнутри плеера
   (`src/Engine/Adlib/adlplayer.cpp:683-687` и `:819-822`). Для порта,
   который берёт данные из `ADLIB.CAT`, это значит: **все треки, включая
   `GMWIN`/`GMLOSE`, зацикливаются**.
3. `id == 0` (всегда, кроме `playMusic("GMGEO", 1)` на геоскейпе) → случайный
   трек (`:539`).

### 7.2 `Mod::getRandomMusic(name)` — `Mod.cpp:493-518`

Выбор **по подстроке**: берутся все загруженные треки, чьё имя *содержит*
`name` (`:504`), и из них — `RNG::seedless` (`:515`), то есть вне игрового
сида, результат не воспроизводится при перезагрузке.

- `playMusic("GMTACTIC")` попадает и в `GMTACTIC`, и в `GMTACTIC1..GMTACTIC10`,
  которые объявлены в `xcom1/music.rul` **без `catPos`**. Умолчание
  `catPos = INT_MAX` (`src/Mod/RuleMusic.cpp:33`) означает «из `.CAT` не
  грузить»; такой трек появится только если игрок положит рядом
  `SOUND/gmtactic3.ogg` и т. п. (`Mod::loadMusic`, `Mod.cpp` — ветка
  «digital tracks»), а в `_musics` попадают лишь реально загруженные
  (`Mod.cpp:3400-3402`). **На чистых данных оригинала выбор всегда один —
  `GMTACTIC`.** Для порта это задел для модов, реализовывать не нужно.
- `playMusic("GMTACWET")` совпадает ровно с одним треком.

### 7.3 Когда трека нет

`getRandomMusic` при пустом совпадении возвращает `_muteMusic` (`:511`), а
`Music::play` у него ничего не делает (`_music == 0`, `Music.cpp:88-90`) — и
`_playingMusic` не обновляется (`Mod.cpp:548-550`). Итог: **продолжает играть
предыдущий трек**. Полезно для порта: незнакомый ID музыки = «оставить как
есть», без тишины и без падения.

### 7.4 Пауза, фокус, сохранения

- Паузы музыки в бою нет. `Music::pause/resume` (`Music.cpp:121,136`) в
  движке не вызываются ниоткуда.
- Потеря фокуса окна: при `backgroundMute` просто выставляется громкость 0
  (`src/Engine/Game.cpp:197-220`), трек продолжает играть.
- Позиция трека нигде не сохраняется: смена имени = трек с нуля
  (`Music.cpp:86-98`). В save попадает только **имя** темы боя
  (`SavedBattleGame.cpp:473`).
- Загрузка боя из меню в бою пересоздаёт `BattlescapeState` → `playMusic`
  с темой из save; если имя совпало с играющим — трек не прервётся.

---

## 8. Мелочи поведения (что важно не потерять в порту)

1. **Музыка — «состояние», а не «событие».** Модель порта: глобальный
   `music_play(id)`, который ничего не делает, если `id` уже играет. Экраны
   вызывают его в своём «входе»; кто не вызвал — не трогает музыку.
2. **Брифинг и бой — две разные темы подряд**, обе стартуют с нуля. Между
   ними экрана без музыки нет.
3. **Внутри боя — ни одной смены.** Не нужны хуки на ход, панику, пси,
   скрытое перемещение. Это сильно упрощает порт: трек боя можно держать
   в одном банке/слоте и стримить с SD без перезагрузок.
4. **Инвентарь/миникарта/меню в бою музыку не перезапускают** — в порту
   обязательно проверять «уже играет этот ID», иначе трек будет дёргаться при
   каждом открытии окна (пользовательское «такие мелочи всё испортят»).
5. **Разбор миссии в UFO не зависит от исхода** (`GMMARS` всегда), в TFTD —
   зависит от знака счёта. Не «улучшать».
6. **Экран посмертных наград меняет музыку на `GMLOSE`/`GMDEBPOR`** и
   показывается последним перед геоскейпом — заметный, легко забываемый штрих.
7. **Победа/поражение кампании обходят разбор**: `DebriefingState` уничтожается
   вместе со стеком, играет `GMWIN`/`GMLOSE` слайдшоу.
8. **Зацикливание — из данных трека**, а не из вызова: `GMWIN`/`GMLOSE` на
   ADLIB зациклены так же, как остальные.
9. Фоновый зацикленный SFX (`ambience` 67 в TFTD) идёт параллельно музыке весь
   бой — в режимах 2–4 ([11](11_sound.md)) он будет постоянно отбирать канал,
   надо решать отдельно.

---

## 9. Открытые вопросы

1. **Точка цикла в `MUSIC.PAK`.** Конвертер пишет покадровый поток до повтора
   ([09 §7](09_converter.md): «некоторые треки UFO идут до повтора 8–9 минут»),
   но формат потока не описывает, *куда* прыгать при повторе. Нужно либо
   хранить смещение точки цикла (опкод `0xC0/0x7E` оригинала,
   `adlplayer.cpp:683`), либо всегда начинать трек с нуля. Решить вместе с
   [20_music_player.md](20_music_player.md).
2. **Стриминг с SD во время боя.** Трек боя длинный; держать его целиком в
   памяти вместе с картой нельзя. Нужен ли двойной буфер и как он уживётся
   с загрузкой тайлов/ресурсов ([13](13_sd_card.md)).
3. **Перезапуск темы при переходе брифинг → бой** даёт две «склейки» подряд.
   Оставить как в оригинале или (дёшево) не переключать, если ID совпал?
   По правилам порта — как в оригинале.
4. **Случайный выбор из нескольких треков** (`GMTACTIC1..10`) на данных
   оригинала не срабатывает. Предлагается не реализовывать; хук оставить в
   виде «список из одного элемента».
5. **Поведение при отсутствии типа** (например, `GMTACWET` в сборке UFO):
   повторить оригинал — «оставить играть текущий», а не включать тишину.
6. **Нужны ли брифинг-темы вообще**, если экран брифинга в порту будет
   упрощён — уточнить у пользователя (по умолчанию: нужны, это мелочь
   оригинала).
7. **Громкость/нормализация**: `normalization` из `music.rul` влияет только на
   микшер Adlib в OpenXcom (`RuleMusic.cpp:33`, комментарий там же). Для OPL3
   на железе, вероятно, не нужна — проверить на слух.
8. **Ambience-канал** (п. 8.9): в каком режиме звука он вообще возможен.
