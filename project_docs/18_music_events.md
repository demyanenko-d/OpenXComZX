# Музыка: где и когда играет (геоскейп, базы, меню, заставки)

Разбор оригинала: `REF/OpenXcom/src` и правила `REF/OpenXcom/bin/standard/xcom1`,
`xcom2`. Бой (брифинги, тактика, разбор миссии) — отдельный документ
`19_music_battle.md`; устройство плеера на Z80 — `20_music_player.md`.
Форматы и конвертация — `09_converter.md` §7 (MUSIC), режимы звука — `11_sound.md`.

Всё, что ниже, относится к **типу** музыки (`GMGEO1`, `GMINTER`, …) — имени
записи `musics:` в `music.rul`. Тип ≠ файл: у типа есть `name` (имя файла для
ogg/mp3) и `catPos` (номер трека в `SOUND/ADLIB.CAT`). У нас тип → номер строки
в `OxzConv/Data/music_types.txt`, ресурс `MUSIC` = `#0300 + номер`
(`09_converter.md` §7).

---

## 1. Механика выбора и воспроизведения

### 1.1 Единственная точка входа — `Mod::playMusic(name, id = 0)`

`REF/OpenXcom/src/Mod/Mod.cpp:525-551`:

```
void Mod::playMusic(const std::string &name, int id)
{
    if (!Options::mute && _playingMusic != name)      // :527
    {
        int loop = -1;
        if (!Options::musicAlwaysLoop &&
            (name == "GMSTORY" || name == "GMWIN" || name == "GMLOSE"))
            loop = 0;                                  // :531-534
        Music *music = 0;
        if (id == 0) music = getRandomMusic(name);     // :539
        else { ss << name << id; music = getMusic(ss.str()); }   // :543-544
        music->play(loop);                             // :548
        if (music != _muteMusic) _playingMusic = name; // :549-550
    }
}
```

Важнейшие следствия:

1. **`name` — не трек, а группа.** Запоминается именно `name`
   (`_playingMusic`, `Mod.h:108`). Повторный вызов с тем же `name` —
   **ничего не делает**: музыка продолжает играть с той же позиции.
   Отсюда почти всё поведение «при попапах музыка не сбивается» (§4.1).
2. **`id == 0` → случайный трек группы.** `Mod::getRandomMusic`
   (`Mod.cpp:493-517`) берёт **все** типы, чьё имя *содержит* `name` как
   подстроку (`Mod.cpp:504`: `i->first.find(name) != npos`), и выбирает
   `RNG::seedless(0, size-1)` (`Mod.cpp:515`) — **вне сида сейва**, т.е. не
   воспроизводится при перезагрузке.
3. **`id != 0` → точный тип** `name + id` (`Mod.cpp:542-545`), например
   `playMusic("GMGEO", 1)` → `GMGEO1`.
4. Если трек не загружен — возвращается `_muteMusic` (`Mod.cpp:508-511`), и
   `_playingMusic` **не** обновляется (`Mod.cpp:549`), т.е. движок будет
   пытаться снова при следующем `init()`.

Группы, которые реально возникают (подстроковое совпадение по `music.rul`):

| Группа | UFO (xcom1) | TFTD (xcom2) |
|---|---|---|
| `GMGEO` | `GMGEO1`, `GMGEO2` (только у них есть `catPos`) | `GMGEO1`…`GMGEO6` |
| `GMINTER` | `GMINTER` (один) | `GMINTER0`, `GMINTER1`, `GMINTER2`, `GMINTER3` |
| `GMTACTIC` | `GMTACTIC` (+ `GMTACTIC1..10` без `catPos`) | нет |

`GMGEO3..GMGEO10` и `GMTACTIC1..10` в `xcom1/music.rul` объявлены **без
`catPos`** — `RuleMusic` ставит `_catPos = INT_MAX` (`Mod/RuleMusic.cpp:33`), и
`Mod::loadMusic` их из `ADLIB.CAT` не грузит (`Mod.cpp:3613-3634`: `track <
adlibcat->getAmount()`). Они существуют только чтобы мод/игрок мог подложить
`gmgeo3.ogg`. Поэтому в ванильном UFO геоскейп — это жребий из **двух** тем, в
TFTD — из **шести**, а воздушный бой в UFO — всегда один трек, в TFTD — жребий
из четырёх.

`GMINTER0` в TFTD — это псевдоним: `name: GMISPOSH, catPos: 12`
(`xcom2/music.rul`), т.е. тот же трек, что `GMISPOSH`. В нашем `MUSIC.PAK` для
TFTD 23 типа сведены в 22 потока (`09_converter.md` §8) — дубль уже схлопнут.

### 1.2 Кто вызывает `playMusic`

Всего в `REF/OpenXcom/src` вне боя — шесть мест:

| Файл:строка | Вызов |
|---|---|
| `Engine/State.cpp:263` | `playMusic(_ruleInterface->getMusic())` — из правил (§3) |
| `Geoscape/GeoscapeState.cpp:521` | `playMusic("GMGEO", 1)` |
| `Geoscape/GeoscapeState.cpp:525` | `playMusic("GMGEO")` |
| `Geoscape/GeoscapeState.cpp:530` | `playMusic("GMINTER")` |
| `Geoscape/GeoscapeState.cpp:906` | `playMusic("GMINTER")` |
| `Geoscape/MonthlyReportState.cpp:316` | `playMusic("GMLOSE")` |
| `Basescape/SoldierMemorialState.cpp:138` | `playMusic("GMGEO")` |
| `Menu/SlideshowState.cpp:75` | `playMusic(_slideshowHeader.musicId)` — из правил |

Плюс два места, которые играют трек **мимо** `playMusic` (без обновления
`_playingMusic`): `Menu/VideoState.cpp:342-353` (интро UFO, `GMINTRO1/2/3`
прямо через `Music::play`) и бой (`19_music_battle.md`).

### 1.3 «Музыка экрана» из правил — `State::init()`

`REF/OpenXcom/src/Engine/State.cpp:240-264` — базовый `init()` любого экрана в
конце делает:

```
if (_ruleInterface != 0 && !_ruleInterface->getMusic().empty())
    _game->getMod()->playMusic(_ruleInterface->getMusic());
```

`_ruleInterface` ставится в `State::setInterface(category)`
(`Engine/State.cpp:75-80`), поле `music` читается в
`Mod/RuleInterface.cpp:47`. То есть **любому окну** можно в `interfaces.rul`
приписать `music:`; ванильные правила делают это только для трёх окон (§3.1).

`init()` вызывается не в конструкторе, а каждый раз, когда экран становится
верхним: `Engine/Game.cpp:160-163` (`if (!_init) { _init = true;
_states.back()->init(); }`), а `_init` сбрасывается в `setState`
(`Game.cpp:444`), `pushState` (`Game.cpp:455`) и `popState` (`Game.cpp:468`).
Отсюда: **закрытие любого окна поверх геоскейпа заново прогоняет
`GeoscapeState::init()`** — и, значит, заново решает вопрос о музыке (но
обычно это no-op из-за `_playingMusic`, §1.1).

### 1.4 Громкость, пауза, остановка

| Что | Где | Поведение |
|---|---|---|
| Громкость музыки | `Engine/Game.cpp:386-389` (`Mix_VolumeMusic`), кривая `Game::volumeExponent` | ползунок `STR_MUSIC_VOLUME`, `Menu/OptionsAudioState.cpp:99-101` |
| Сворачивание окна / потеря фокуса | `Engine/Game.cpp:193-219` | при `Options::backgroundMute` — `setVolume(0,0,0)`, при возврате — восстановление. **Трек не останавливается и не ставится на паузу**, только глушится |
| `Music::pause()` / `resume()` | `Engine/Music.cpp:121-146` | в коде вне боя не вызываются — это API «на всякий случай» |
| Полная остановка | `Engine/Music.cpp:106-116` (`Music::stop()`) | вызывается при рестарте загрузки данных (`Menu/StartState.cpp:131`) и в конце видео (`Menu/VideoState.cpp:542`) |
| `Music::play()` | `Engine/Music.cpp:86-101` | **всегда сначала `stop()`** — то есть смена трека это обрыв, без затухания. Затухание (`Mix_FadeOutMusic`) есть только в видеоплеере, `Menu/VideoState.cpp:494` |
| Позиция трека | нигде не сохраняется | при смене группы старый трек убивается; вернувшись, группа стартует **с начала** (и, если групп несколько, — возможно другим треком) |

Игровая пауза геоскейпа (`GeoscapeState::popup` → `_pause = true`,
`GeoscapeState.cpp:1803-1807`) музыку не трогает.

### 1.5 Зацикливание

Аргумент `loop` из `Mod::playMusic` доходит до `Music::play(loop)`
(`Engine/Music.cpp:86`, SDL_mixer) — но **для формата ADLIB он игнорируется**:
`AdlibMusic::play(int)` принимает параметр и не использует его
(`Engine/AdlibMusic.cpp:120-129`). Значит «хак» `loop = 0` для
`GMSTORY`/`GMWIN`/`GMLOSE` (`Mod.cpp:531`) действует только на ogg/mp3/MIDI.

В ADLIB зацикливание — свойство самих данных трека:

- опкод `0xC0` с аргументом `0x7E` → `another_loop = 1`
  (`Engine/Adlib/adlplayer.cpp:683-686`), и `func_play_tick` делает
  `init_music(); clear_channels();` и продолжает — т.е. **возврат к началу
  трека** (`adlplayer.cpp:806-822`);
- опкод `0xFF` → `adl_gv_music_playing = false` — **конец трека, тишина**
  (`adlplayer.cpp:597-601`);
- при `Options::musicAlwaysLoop` SDL-колбэк сам перезапускает доигравший трек
  (`Engine/AdlibMusic.cpp:145-150`); по умолчанию опция выключена
  (`Engine/Options.cpp:142`).

**Факт по нашим данным:** конвертер прогоняет тот же плеер офлайн и помечает
точку повтора (`OxzConv/Core/Music.cs:350-366`, поле `loop_offset` в заголовке
`MUSIC`, `OxzConv/Core/Music.cs:10`). Прогон `tmp/sd/OXZ/*/MUSIC.PAK`
показывает: **у всех 14 треков UFO и всех 22 треков TFTD `loop_offset`
задан** — ни один не заканчивается `0xFF`. Точка повтора — кадр 1 (кадр 0
хранит разовую инициализацию OPL3). Длина одного прохода:

| | UFO | TFTD |
|---|---|---|
| `GMGEO1` | 23 574 кадра ≈ 8 мин 3 с | 6 046 ≈ 2:04 |
| `GMGEO2` | 25 828 ≈ 8:49 | 6 218 ≈ 2:07 |
| `GMGEO3..6` | — | 8 044 / 4 681 / 4 193 / 4 681 ≈ 1:26…2:45 |
| `GMINTER` | 2 706 ≈ 0:55 | `GMINTER0..3`: 1 561 / 1 058 / 2 675 / 2 006 ≈ 0:22…0:55 |
| `GMSTORY` (меню UFO) | 5 720 ≈ 1:57 | — |
| `GMWAITLO` (меню TFTD) | — | 1 171 ≈ 0:24 |
| `GMWIN` / `GMLOSE` | 3 164 ≈ 1:05 / 5 688 ≈ 1:56 | 8 264 ≈ 2:49 / 3 443 ≈ 1:11 |

(кадр = 1/48.83 с, `09_converter.md` §7; данные получены разбором заголовков
`MUSIC.PAK`, скрипт `tmp/musloop.js`)

---

## 2. Таблица «состояние / событие → музыка»

### 2.1 Меню и служебные экраны

| Состояние / событие | Музыка | Где в оригинале | Что делается |
|---|---|---|---|
| Загрузка данных при старте | тишина | `Menu/StartState.cpp:125-135` (`Music::stop()`) | остановить всё |
| Интро (первый запуск нового мастер-мода) | UFO: `GMINTRO1` → `GMINTRO2` → `GMINTRO3` по кадрам FLI (0 / 211 / 378); TFTD-слайды: `GMNEWINT` | `Menu/StartState.cpp:171-175`; `Menu/VideoState.cpp:180-222` (таблица `introSoundTrack`), `:340-353`; `xcom2/cutscenes.rul` (`intro` → `musicId: GMNEWINT`) | три трека подряд, каждый `play(1)` — мимо `_playingMusic` |
| **Главное меню** | UFO `GMSTORY`, TFTD `GMWAITLO` | `Menu/MainMenuState.cpp:63` (`setInterface("mainMenu")`) → `Engine/State.cpp:263`; `xcom1/interfaces.rul:3`, `xcom2/interfaces.rul:3` | смена трека (из правил) |
| Выбор сложности (New Game) | — (продолжает музыка меню) | `Menu/NewGameState.cpp` — `playMusic` нет | ничего |
| **Нажато OK в выборе сложности** | `GMGEO1` | `Menu/NewGameState.cpp:121-153` (`btnOkClick`, `gs->init()` вызывается явно на `:151`) → `Geoscape/GeoscapeState.cpp:518-521` | смена: новая игра всегда стартует с `GMGEO1` (§2.2) |
| Список сохранений / загрузок, удаление, «Options», настройки (видео/звук/игра/бой/управление/моды) | — | `Menu/ListGamesState.cpp`, `ListSaveState.cpp`, `ListLoadState.cpp`, `DeleteGameState.cpp`, `PauseState.cpp`, `Options*State.cpp`, `ModListState.cpp` — `playMusic` нет | музыка продолжается |
| **Загрузка сейва из меню** | `GMGEO` (случайная) | `Menu/LoadGameState.cpp:173` (`setState(new GeoscapeState)`) → `GeoscapeState.cpp:525` | смена |
| Загрузка сейва с уже оконченной кампанией | — (остаётся музыка меню) | `Menu/LoadGameState.cpp:160-167` → `StatisticsState` (у `endGameStatistics` нет `music`) | ничего |
| Выход в меню (Abandon Game) | UFO `GMSTORY` / TFTD `GMWAITLO` | `Menu/AbandonGameState.cpp` → `MainMenuState::init` | смена |
| Сохранение/автосохранение/ironman | — | `Menu/SaveGameState.cpp` | ничего |
| Ошибка (`ErrorMessageState`) | — | `Menu/ErrorMessageState.cpp` | ничего |

### 2.2 Геоскейп

| Состояние / событие | Музыка | Где в оригинале | Что делается |
|---|---|---|---|
| Геоскейп, **первый месяц** (`getMonthsPassed() == -1`, т.е. база ещё не поставлена / первый вход) | ровно `GMGEO1` | `Geoscape/GeoscapeState.cpp:518-521` | `playMusic("GMGEO", 1)` |
| Геоскейп, обычная игра | случайный из группы `GMGEO` | `Geoscape/GeoscapeState.cpp:522-526` | `playMusic("GMGEO")` — no-op, если уже `GMGEO` |
| Геоскейп, когда идёт воздушный бой (или запущен таймер его старта) | `GMINTER` | `Geoscape/GeoscapeState.cpp:527-531` — ветка `else` условия `_dogfights.empty() && !_dogfightStartTimer->isRunning()` | `playMusic("GMINTER")` |
| **Перехватчик догнал НЛО — начался воздушный бой** | `GMINTER` | `Geoscape/GeoscapeState.cpp:884-906` (в `time10Minutes`, при создании `DogfightState`) | `playMusic("GMINTER")` сразу после постановки боя в очередь |
| Ход воздушного боя, попадания, уклонение, минимизация окна | — | `Geoscape/DogfightState.cpp` — упоминаний музыки **нет** | ничего |
| Победа / поражение / бегство в воздушном бою | — | `Geoscape/DogfightState.cpp`; конец ловится в `GeoscapeState::handleDogfights`, `GeoscapeState.cpp:2077-2131` | ничего, `GMINTER` продолжает |
| **Все воздушные бои закончились → отъезд камеры** | снова `GMGEO` (жребий заново) | `GeoscapeState.cpp:2126-2130` (`_zoomOutEffectTimer->start()`) → `GeoscapeState::zoomOutEffect`, `:2064-2072` (`init()`) → `:522-526` | смена группы `GMINTER` → `GMGEO`, трек выбирается **заново** |
| Обнаружение НЛО (`UfoDetectedState`), потеря НЛО, обнаружение терроракта, база пришельцев | — | `Geoscape/UfoDetectedState.cpp`, `UfoLostState.cpp`, `MissionDetectedState.cpp`, `AlienBaseState.cpp` | ничего (это звуковой эффект, не музыка) |
| Окно перехвата, выбор цели, патруль, мало топлива, «прибыли грузы» | — | `Geoscape/InterceptState.cpp`, `SelectDestinationState.cpp`, `CraftPatrolState.cpp`, `LowFuelState.cpp`, `ItemsArrivingState.cpp` | ничего |
| Возвращение корабля на базу | — | — | ничего (`GMGEO` уже играет) |
| Подтверждение высадки (`ConfirmLandingState`) | — | `Geoscape/ConfirmLandingState.cpp` | ничего; трек боя ставит уже брифинг (`19_music_battle.md`) |
| **Оборона базы** (`BaseDefenseState`) | — | `Geoscape/BaseDefenseState.cpp` — музыки нет | `GMGEO` продолжает до брифинга; трек боя — из `alienDeployments.rul`, `STR_BASE_DEFENSE`: UFO `GMENBASE` (`xcom1/alienDeployments.rul:1167`), TFTD `GMDEFBAS` (`xcom2/alienDeployments.rul:2482`) |
| База уничтожена (`BaseDestroyedState`) | — | `Geoscape/BaseDestroyedState.cpp` | ничего |
| Возврат из боя (после `DebriefingState`) | `GMGEO` | `Battlescape/DebriefingState.cpp:635-639` ставит `GMMARS`/`GMDEBGOO`/`GMDEBPOR`, затем `popState` → `GeoscapeState::init()` → `:525` | смена обратно на `GMGEO` |
| Графики, финансирование, статистика стран | — | `Geoscape/GraphsState.cpp`, `FundingState.cpp` | ничего |
| Исследование готово, новые темы, производство готово, пси-тренировка | — | `Geoscape/ResearchCompleteState.cpp`, `NewPossibleResearchState.cpp`, `ProductionCompleteState.cpp`, `PsiTrainingState.cpp`, `AllocatePsiTrainingState.cpp` | ничего |
| Заставка по завершении исследования (моды) | из `cutscenes.rul` | `GeoscapeState.cpp:1605-1613` (`popup(new CutsceneState(...))`) | см. §2.5 |
| Полёт на Сидонию / Т'лет (`ConfirmCydoniaState`) | — | `Geoscape/ConfirmCydoniaState.cpp` | ничего; трек ставит брифинг (`GMNEWMAR` / `GMBIGMAR`) |

### 2.3 Базы, экономика, снаряжение

`playMusic` нет **ни в одном** файле `REF/OpenXcom/src/Basescape/`, кроме
`SoldierMemorialState.cpp`. Значит вид базы, склад, закупки, продажи,
переводы, исследования, производство, корабли, экипировка, броня, досье
бойцов, постройка и снос сооружений — **все идут под ту же `GMGEO`**, что
играла в геоскейпе (или под `GMINTER`, если игрок ушёл в базу во время
воздушного боя).

| Состояние / событие | Музыка | Где в оригинале |
|---|---|---|
| `BasescapeState`, `BaseInfoState`, `StoresState`, `MonthlyCostsState`, `BuildFacilitiesState`, `PlaceFacilityState`, `DismantleFacilityState`, `PurchaseState`, `SellState`, `TransferBaseState`/`TransferItemsState`/`TransferConfirmState`/`TransfersState`, `ResearchState`, `NewResearchListState`, `ResearchInfoState`, `ManufactureState`, `NewManufactureListState`, `ManufactureStartState`, `ManufactureInfoState`, `CraftsState`, `CraftInfoState`, `CraftSoldiersState`, `CraftWeaponsState`, `CraftEquipmentState`, `CraftArmorState`, `SoldiersState`, `SoldierInfoState`, `SoldierArmorState`, `SackSoldierState`, `SoldierDiary*`, `ManageAlienContainmentState`, `PlaceLiftState`, `SelectStartFacilityState` | — (продолжается) | в этих файлах `playMusic` отсутствует |
| **Мемориал павших** (`SoldierMemorialState`) | UFO `GMLOSE`, TFTD `GMDEBPOR` | `Basescape/SoldierMemorialState.cpp:59` (`setInterface("soldierMemorial")`) → `Engine/State.cpp:263`; `xcom1/interfaces.rul:1146`, `xcom2/interfaces.rul:1182` |
| Выход из мемориала (OK) | `GMGEO` (жребий заново) | `Basescape/SoldierMemorialState.cpp:136-139` — сначала `popState()`, потом `playMusic("GMGEO")` |
| «Statistics» из мемориала | — (остаётся `GMLOSE`/`GMDEBPOR`) | `Basescape/SoldierMemorialState.cpp:145-148` → `Menu/StatisticsState.cpp:61` (`endGameStatistics`, без `music`) |

### 2.4 Конец месяца, финансирование, поражение, победа

| Состояние / событие | Музыка | Где в оригинале | Что делается |
|---|---|---|---|
| Наступил новый месяц → отчёт (`MonthlyReportState`) | — (`GMGEO` продолжает) | `Geoscape/GeoscapeState.cpp:1737-1768` (`time1Month` → `popup(new MonthlyReportState(...))`) | ничего |
| Рост/падение финансирования, смена рейтинга, пакты стран | — | `Geoscape/MonthlyReportState.cpp:320+` (`calculateChanges`) | ничего; отдельной «плохой» темы нет |
| Медали (`CommendationState`), пси-тренировка после отчёта | — | `Geoscape/MonthlyReportState.cpp:275-281` | ничего |
| **Поражение по итогам месяца → экран «You have failed»** (рейтинг ниже порога два месяца подряд, `MonthlyReportState.cpp:187-193`; либо долг ниже порога после предупреждения, `:198-210`) | `GMLOSE` | `Geoscape/MonthlyReportState.cpp:305-316` (ветка `else`, `_txtFailure->setVisible(true)`) | смена трека |
| OK на экране «You have failed» → заставка поражения | `GMLOSE` (тот же, **не перезапускается**) | `Geoscape/MonthlyReportState.cpp:297` (`CutsceneState(LOSE_GAME)`) → `Menu/SlideshowState.cpp:75` `playMusic("GMLOSE")` — no-op по `_playingMusic` | продолжает играть |
| Потеряны все базы → поражение | `GMLOSE` (через заставку) | `Geoscape/GeoscapeState.cpp:691-706` (`time5Seconds`: `END_LOSE` → `CutsceneState(LOSE_GAME)`) → §2.5 | смена |
| Победа (последняя миссия Сидонии / Т'лета) | `GMWIN` | из боя: `Battlescape/BattlescapeState.cpp:2060-2084` (`ruleDeploy->getWinCutscene()`; `xcom1/alienDeployments.rul:1396`, `xcom2/alienDeployments.rul:2859` — `winCutscene: winGame`) → `CutsceneState` → §2.5 | смена |
| Провал/отступление на финальной миссии | `GMLOSE` | `loseCutscene`/`abortCutscene: loseGame` — `xcom1/alienDeployments.rul:1239-1240, 1397-1398`, `xcom2/alienDeployments.rul:2577-2578, 2719-2720, 2860-2861` | смена |
| После заставки конца игры → `StatisticsState` | — (остаётся `GMWIN`/`GMLOSE`) | `Menu/CutsceneState.cpp:58-68`, `Menu/StatisticsState.cpp:61` | ничего |
| OK в `StatisticsState` → главное меню | `GMSTORY` / `GMWAITLO` | `Menu/StatisticsState.cpp:331` → `MainMenuState::init` | смена |

### 2.5 Заставки (`CutsceneState` → видео или слайд-шоу)

`Menu/CutsceneState.cpp:51-101`: состояние само себя снимает со стека и
запускает либо `VideoState` (FLI/VID), либо `SlideshowState` — в зависимости
от наличия файлов и `Options::preferredVideo`.

| Заставка | Музыка (слайды) | Где |
|---|---|---|
| `intro` (UFO) | `GMINTRO1`/`2`/`3` по кадрам видео; слайдов нет | `xcom1/cutscenes.rul` (только `videos:`), `Menu/VideoState.cpp:180-222`, `:340-353` |
| `intro` (TFTD) | `GMNEWINT` | `xcom2/cutscenes.rul` (`slideshow: musicId: GMNEWINT`) |
| `tleth` (TFTD, брифинг развёртывания) | `GMSIG` | `xcom2/cutscenes.rul`; вызывается из `alienDeployments.rul:2586` (`cutscene: tleth`) |
| `winGame` | `GMWIN` | `xcom1/cutscenes.rul`, `xcom2/cutscenes.rul` |
| `loseGame` | `GMLOSE` | `xcom1/cutscenes.rul`, `xcom2/cutscenes.rul` |

Слайд-шоу ставит музыку **один раз** в конструкторе (`Menu/SlideshowState.cpp:75`),
не на каждый слайд. Для видео громкость музыки и эффектов временно
выравнивается по максимуму (`Menu/VideoState.cpp:402-421`:
`Options::musicVolume = Options::soundVolume = max(prev…)`) и восстанавливается
в конце (`:533-537`), а трек глушится/останавливается (`:492-505`, `:542`).

### 2.6 Уфопедия

`playMusic` нет **ни в одном** файле `REF/OpenXcom/src/Ufopaedia/`
(`Ufopaedia.cpp`, `UfopaediaStartState.cpp`, `UfopaediaSelectState.cpp`,
`ArticleState*.cpp`). Уфопедия — что из меню геоскейпа, что из базы — идёт под
текущую `GMGEO`; при заходе и выходе ничего не переключается.

---

## 3. Что задают правила, а что зашито в код

### 3.1 Из правил (`*.rul`) — можно менять модом

| Поле | Файл правил | Что задаёт | Ванильные значения |
|---|---|---|---|
| `musics: [type, name, catPos, normalization]` | `music.rul` | реестр типов: имя файла, номер трека в `ADLIB.CAT`, нормализация громкости (только для ADLIB, `Mod/RuleMusic.cpp:33`, `:45-50`, `:76-79`) | UFO — 32 типа, из них 14 с `catPos` (`xcom1/music.rul`); TFTD — 23 типа, все с `catPos` (`xcom2/music.rul`) |
| `interfaces: - type: X / music: Y` | `interfaces.rul` | музыка при показе окна `X` (`Engine/State.cpp:261-264`) | **только три записи**: `mainMenu` (`GMSTORY`/`GMWAITLO`), `commendationsLate` и `soldierMemorial` (`GMLOSE`/`GMDEBPOR`) |
| `slideshow: musicId:` | `cutscenes.rul` | музыка слайд-шоу (`Menu/SlideshowState.cpp:75`) | см. §2.5 |
| `briefing: music:` | `alienDeployments.rul` | музыка брифинга/боя (`Mod/AlienDeployment.h:49`, умолчание `GMDEFEND`) | подробно — `19_music_battle.md` |
| `goodDebriefingMusic` / `badDebriefingMusic` | `vars.rul` | разбор миссии (`Mod/Mod.cpp:1136-1137`) | UFO — не задано, умолчание `GMMARS`/`GMMARS` (`Mod/Mod.cpp:174-175`); TFTD — `GMDEBGOO`/`GMDEBPOR` (`xcom2/vars.rul:44-45`) |

### 3.2 Зашито в код (мод не изменит без правки исходника)

| Что | Где |
|---|---|
| Имена групп `"GMGEO"` и `"GMINTER"` для геоскейпа и воздушного боя | `Geoscape/GeoscapeState.cpp:521, 525, 530, 906` |
| Правило «первый месяц → строго `GMGEO1`» | `Geoscape/GeoscapeState.cpp:518-521` |
| `"GMLOSE"` на экране «You have failed» | `Geoscape/MonthlyReportState.cpp:316` |
| `"GMGEO"` при выходе из мемориала | `Basescape/SoldierMemorialState.cpp:138` |
| `GMINTRO1/2/3` и кадры их запуска (0, 211, 378) в интро UFO | `Menu/VideoState.cpp:182, 187, 222, 340-353` |
| Список «не зацикливать»: `GMSTORY`, `GMWIN`, `GMLOSE` | `Mod/Mod.cpp:531` |
| Правило «группа = подстрока имени типа» | `Mod/Mod.cpp:504` |
| То, что смена музыки вообще привязана к `State::init()` | `Engine/State.cpp:261-264`, `Engine/Game.cpp:160-163` |

---

## 4. Мелочи поведения (их надо перенести)

### 4.1 Попапы музыку не сбивают

Любое окно поверх геоскейпа (обнаружено НЛО, отчёт месяца, исследование
готово, база, Уфопедия, настройки, сохранение) **не меняет музыку**: у их
интерфейсов нет `music:`, а `State::init()` без него ничего не делает
(`Engine/State.cpp:261`). Когда окно закрывается, `GeoscapeState::init()`
вызывает `playMusic("GMGEO")` — но это **no-op**, потому что `_playingMusic`
уже `"GMGEO"` (`Mod/Mod.cpp:527`). Трек **не перезапускается и не
перевыбирается**. То же и для главного меню: перезаход в него из опций не
перезапускает `GMSTORY`.

### 4.2 Смена группы = обрыв и старт с нуля

`Music::play` всегда начинается с `stop()` (`Engine/Music.cpp:91-97`) —
никаких кроссфейдов. Позиция старого трека нигде не запоминается. Поэтому
цикл «геоскейп → воздушный бой → геоскейп» каждый раз даёт **новый случайный
`GMGEO` с самого начала** (`GeoscapeState.cpp:2064-2072` → `:525`).

### 4.3 Новая случайная тема — только при возврате в группу

Внутри группы трек живёт до смены группы: `GMGEO` может играть часами (в UFO
один проход `GMGEO1` — 8 минут, потом повтор). Перевыбор происходит ровно в
трёх случаях: вход в геоскейп из меню/боя, конец воздушного боя, выход из
мемориала.

### 4.4 Тишины в геоскейпе не бывает

Все треки `ADLIB.CAT` зациклены (§1.5), громкость не затухает, пауза игры
музыку не трогает. Единственные места тишины: экран загрузки данных
(`Menu/StartState.cpp:131`) и конец видеозаставки (`Menu/VideoState.cpp:542`).

### 4.5 Музыка не зависит от сида кампании

`RNG::seedless` (`Mod/Mod.cpp:515`) — выбор трека не воспроизводится при
загрузке того же сейва и не влияет на игровой RNG. В сейве музыка не
сохраняется (для боя — сохраняется, `Battlescape/BattlescapeState.cpp:464`,
`_save->getMusic()`; см. `19_music_battle.md`).

### 4.6 Заглушение при потере фокуса окна

`Options::backgroundMute` (`Engine/Game.cpp:193-219`) — `Mix_VolumeMusic(0)`,
трек продолжает идти «в тишине». Для нас неактуально, но объясняет, почему
`AdlibMusic::player` первым делом проверяет `Mix_VolumeMusic(-1) == 0`
(`Engine/AdlibMusic.cpp:141-143`).

---

## 5. Отображение на экраны порта

Экраны — `src/common/screens.h`, типы музыки — `OxzConv/Data/music_types.txt`
(ресурс `#0300 + номер`).

| Экран порта | Музыка |
|---|---|
| `SCR_MAIN_MENU` | UFO `GMSTORY`, TFTD `GMWAITLO` |
| `SCR_NEW_GAME` (выбор сложности) | без смены; по OK → `GMGEO1` |
| `SCR_GEOSCAPE`, `SCR_BASESCAPE` и все окна базы, `SCR_UFOPAEDIA`, `SCR_PAUSE`, `SCR_OPTIONS`, `SCR_LOAD`, `SCR_SAVE`, графики, отчёт месяца, все всплывающие окна | без смены — группа `GMGEO` |
| воздушный бой (окно перехвата НЛО) | группа `GMINTER`, ставится в момент создания боя; возврат на `GMGEO` — после отъезда камеры |
| `SCR_REPORT_FAILED` («You have failed») | `GMLOSE` |
| `SCR_SLIDESHOW` (`CUT_INTRO`/`CUT_WIN`/`CUT_LOSE`/`CUT_TLETH`) | поле «музыка» записи `CUTSCENES` (`09_converter.md` §7) |
| `SCR_STATISTICS` | без смены |
| мемориал (окно ещё не сделано) | UFO `GMLOSE` / TFTD `GMDEBPOR`; по OK — `GMGEO` |

Минимальный API движка, достаточный для всего вышеперечисленного:

- `mus_play_group(группа)` — «играть группу, если сейчас играет не она»
  (аналог `Mod::playMusic` с запоминанием текущей группы);
- `mus_play_track(тип)` — точный трек (для `GMGEO1` в начале игры и для
  заставок);
- таблица «группа → список типов для текущей игры» (UFO/TFTD различаются);
- всё остальное — «ничего не делать»: это поведение по умолчанию.

---

## 6. Открытые вопросы

1. **Зацикливать ли `GMWIN`/`GMLOSE`/`GMSTORY`.** В оригинальном ADLIB они
   зациклены (наши потоки имеют `loop_offset`, §1.5), но OpenXcom для
   цифровых форматов явно просит `loop = 0` (`Mod/Mod.cpp:531`) — т.е. замысел
   «сыграть один раз». Решить: повторяем железное поведение DOS-версии
   (цикл) или замысел OpenXcom (один проход + тишина).
2. **Группы `GMGEO`/`GMINTER` в порту.** Подстроковый поиск на Z80 не нужен —
   нужна зашитая таблица. Но её состав зависит от игры (UFO: `GMGEO1-2`,
   `GMINTER`; TFTD: `GMGEO1-6`, `GMINTER0-3`). Где хранить — в `RULES.PAK`
   (тогда мод сможет менять) или в коде? Конвертер сейчас таблицу групп не
   выдаёт — надо добавить.
3. **Дубль `GMINTER0` = `GMISPOSH`.** В `MUSIC.PAK` TFTD дубль уже схлопнут
   (22 потока на 23 типа), но нужно проверить, что оба id указывают на один
   ресурс, иначе жребий `GMINTER` промахнётся.
4. **Случайность выбора.** `RNG::seedless` — отдельный от игрового RNG
   генератор. У нас — брать кадровый счётчик? Надо, чтобы выбор не влиял на
   воспроизводимость кампании (важно для автотестов).
5. **Память под поток.** `GMGEO1` UFO — 118 КБ, `GMGEO2` — 125 КБ; это
   больше страницы. Значит потоковое чтение с SD или отдельные страницы под
   музыку — вопрос к `20_music_player.md` и `13_sd_card.md`.
6. **Интро UFO** — это видео FLI с покадровой раскладкой звука
   (`Menu/VideoState.cpp:180-222`); видео мы не переносим (`09_converter.md`
   §7: «только слайд-шоу»). Значит `GMINTRO1/2/3` в UFO играть негде —
   выбрать один из них для нашего слайд-шоу или не делать интро для UFO.
7. **Мемориал павших** — окна ещё нет в порту (`14_todo.md`); при его
   появлении не забыть `GMLOSE`/`GMDEBPOR` и возврат на `GMGEO`.
8. **Нормализация громкости не перенесена.** В `music.rul` у каждого типа есть
   `normalization` (0.74…1.19), в ADLIB-плеере это множитель общей громкости:
   `AdlibMusic.cpp:42` (`_volume = volume`) и `:128`
   (`func_set_music_volume(127 * _volume)`). Наш конвертер всегда берёт
   `vol = 127` (`OxzConv/Core/Music.cs:63`) — поле `normalization` игнорируется,
   треки будут разной громкости. Решить: учитывать при конвертации (проще) или
   хранить множитель у ресурса.
9. **Смена трека без щелчка.** `Music::play` = «оборвать и начать». На OPL3
   резкий обрыв даст щелчок — нужен ли быстрый сброс огибающих (запись в
   `B0-B8`, KEY-OFF) перед стартом нового потока.
