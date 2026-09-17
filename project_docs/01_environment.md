# Рабочее окружение

## Машина

- Windows 11 Pro, рабочая папка `D:\ProjectWork\OpenXComZX` (не git-репозиторий).
- `.vscode/settings.json` указывает CMake на `REF/OpenXcom-master/OpenXcom-master`
  — такого пути нет, исходники лежат в `REF/OpenXcom`. Настройка устарела.

## Тулчейн (проверено 2026-09-14)

| Инструмент | Путь / версия | Роль |
|---|---|---|
| SDCC | `C:\Program Files\SDCC\bin`, **4.5.0** #15242 (MINGW64) | компилятор C для Z80 |
| sdasz80, sdar, makebin | там же | ассемблер, архиватор, ihx→bin |
| Node.js | v22.20.0 | скрипты упаковки, офлайн-конвертер данных |
| CMake | установлен | сборка OpenXcom на ПК (эталон) при необходимости |
| git | установлен | — |
| Python | **нет** (только заглушка Microsoft Store) | не использовать |
| npm | 10.9.3, реестр npmjs.org | пакеты для конвертера (`js-yaml` и т.п.) |
| Visual Studio | Community 2026 (`C:\Program Files\Microsoft Visual Studio\18\Community`) | сборка эмулятора Unreal, при желании — OpenXcom на ПК |
| sjasmplus | нет | — |
| z88dk (zcc) | нет в PATH (есть исходники в REF) | — |

### Ошибка кодогенерации SDCC 4.5.0 #15242 (найдена 2026-09-14)

Сравнение на равенство байта, лежащего в A, с переменной в памяти
компилируется в разрушающее `sub a,(hl)` / `sub a,N (iy)`, после чего A
используется так, будто там осталось исходное значение:

```c
static uint8_t g;
uint8_t f(uint8_t k) { if (k == g) return 0; g = k; return k; }
// _f:  ld hl,#_g / sub a,(hl) / jr NZ,1$ / xor a / ret
//  1$: ld (_g),a / ret        <- записывает и возвращает k - g, а не k
```

Проявляется с `--sdcccall 1` (с `--sdcccall 0` значение перечитывается со
стека), не зависит от `--opt-code-size`, peephole и `--max-allocs-per-node`.
В проекте сломало опрос клавиатуры (`input.c`: коды клавиш «плавали»).
Обход — копия в локальную переменную: `uint8_t p = g; if (k == p)` даёт
`cp a, c`. **`tools/checkasm.js`** (шаг сборки) ищет `sub a,<память>` +
`jr/jp Z|NZ`, после которых A читается раньше, чем перезаписывается, и
останавливает сборку.

Код SDCC в целом медленный (переменные на стеке через `ix`, 19–21 такт на
операцию): горячие места — на ассемблере (`src/kernel/win0/glyph.s`), 32-битной
арифметики в циклах избегать (08 §6.6).

Инструменты вне PATH (найдены при разборе REF):

| Инструмент | Где | Роль |
|---|---|---|
| sjasmplus | `D:\ProjectWork\vdos\introspec\_bin\sjasmplus.exe` | ассемблер для отдельных блобов (inflate и т.п.) |
| robimg.exe | `D:\ProjectWork\SoundSinth\scripts\robimg.exe` | сборка FAT-образа SD для эмулятора |
| `mkhobeta.js`, `mksna.js`, `cutbin.js`, `ihx2wmf.js` | `D:\ProjectWork\SoundSinth\scripts\tools\` | упаковщики (копии в `D:\ProjectWork\Backup\scripts`) |
| `build_sd.bat`, `build_trdos.bat`, `build_z80fw.bat` | `D:\ProjectWork\SoundSinth\scripts\` | образцы скриптов сборки |
| Unreal Speccy | только исходники `REF/Unreal` (VS2022), конфиг `REF/Unreal/cfg/Unreal.ini` уже под TS-Config + MoonSound | эмулятор |
| UnrealRemix | `D:\ProjectWork\UnrealRemix` — похоже, CMake-версия Unreal (есть `out/build/x64-Debug`, но exe не собран) | альтернативный источник эмулятора |
| ZXMAK2 | `D:\ProjectWork\ZXMAK2` | другой эмулятор (поддержку TS-Config не проверял) |

Собранного эмулятора TS-Config на машине нет.

### Эмулятор — `unreal/` в корне проекта (положен пользователем 2026-09-14)

Эмулятором занимается пользователь; сюда же будут добавляться отладочные
средства под проект. Что в нём есть (изучено, не собиралось):

- Отдельный git-репозиторий (`unreal/.git`). Последние коммиты — перевод с
  WinAPI/DirectX на **SDL2 + ImGui**, отладчик рисуется в ImGui, файловые
  диалоги через tinyfiledialogs. Есть незакоммиченные изменения пользователя
  (в т.ч. новый `ui/debugger/dbgzxdos.*`).
- Сборка: CMake + Ninja + Clang 18+ (`unreal/.vscode/README.md`), корневой
  `unreal/CMakeLists.txt` → `unreal/Unreal/CMakeLists.txt`.
  `unreal/Unreal/doc/ARCHITECTURE.md` описывает ещё DX-эпоху — устарел;
  рядом `MIGRATION_PLAN.md`, `REFACTORING_PLAN.md`.
- **Готовая сборка:** `unreal/build/debug/Unreal.exe` (+ `SDL2.dll`,
  `bass.dll`, `rom/`, `Unreal.ini`, образ `wc.img` 100 МБ).
- Инструменты: `unreal/tools/robimg/robimg.exe`, `unreal/tools/7z/`;
  Wild Commander: `unreal/soft/WC/` (`exe/`, `source/`, `wc.zip`);
  образ собирается `unreal/soft/WC/create wcimg.bat`:
  `robimg.exe -p="wc.img" -s=102400 -C="..\..\soft\wc\exe"` (размер в КБ).

**Командная строка** (`unreal/Unreal/app/emul.cpp:276`):
`Unreal.exe -c <ini> -b <файл точек останова> -l <файл меток>`.
Позиционного аргумента для снапшота нет — **SPG грузится через секцию
`[AUTOLOAD] snapshot=<файл>`** в своём ini (`app/config.cpp:840-846`).

**Формат файла меток** (`unreal/Unreal/ui/debugger/dbglabls.cpp:121`):
по строке на метку, `xxxx имя` (адрес 16 бит) или **`bb:xxxx имя`**
(страница ОЗУ + адрес, смещение берётся по модулю 16К). Второй вариант —
ровно то, что нужно для банкованного кода: метки генерируем из `.map`/`.noi`
SDCC Node-скриптом. Файл меток отслеживается на изменения и
перезагружается сам (Windows).

**Важное в `build/debug/Unreal.ini`:** `HIMEM=TSL`, `RAMSize=4096`,
`TS_VDAC=5BIT`, `RESET=SYS`, `ZC=1`, `Mouse=KEMPSTON`, `MoonSound=1`
(ROM `YRW801-M`), `Saa1099=0`, `SPGMemInit=0`, `Frame=71680`, `Line=224`,
`intlen=32`, `[HDD] Image0=wc.img`, **`[ZC] SDCARD=D:\opl3.img` — путь вне
проекта.** Для проекта нужен свой ini (копия с `SDCARD=` на наш образ SD с
WC + данными игры и `snapshot=` на наш SPG), запускаемый через `-c`.

Отладчик: `ui/debugger/` — точки останова, метки, трасса, память,
регистры, **окно регистров TS-Config (`dbgtsconf.cpp`)**.

**Пользователь разрешил менять `unreal/` как угодно (это копия).**

#### Сборка эмулятора на этой машине (проверено 2026-09-14)

Кэш `build/debug` указывает на исходный путь пользователя
(`D:/pub_projects/unreal`), поэтому своя сборка — в `unreal/build/oxz`:

```
cmake -S unreal/Unreal -B unreal/build/oxz -G Ninja -DCMAKE_BUILD_TYPE=Debug ^
  -DCMAKE_C_COMPILER="C:/Program Files/LLVM/bin/clang.exe" ^
  -DCMAKE_CXX_COMPILER="C:/Program Files/LLVM/bin/clang++.exe" ^
  -DCMAKE_MAKE_PROGRAM=C:/tools/ninja.exe ^
  -DCMAKE_CXX_FLAGS=-D_MSVC_STL_USE_ABORT_AS_DOOM_FUNCTION
cmake --build unreal/build/oxz -j 2
```

- Clang 18.1.8 (`C:\Program Files\LLVM`), Ninja (`C:\tools\ninja.exe`),
  CMake 4.3.1; заголовки STL — из VS 2026 (MSVC 14.51).
- `-D_MSVC_STL_USE_ABORT_AS_DOOM_FUNCTION` обязателен: STL 14.51 под clang
  вызывает `__builtin_verbose_trap`, которого нет в clang 18.
- **Не больше 2 потоков** — при `-j` по умолчанию clang падает с
  «LLVM ERROR: out of memory» (упирается в лимит памяти).
- Release не собирается: clang 18 падает внутри оптимизатора на
  `devices/video/draw.cpp`. Пока используем Debug.
- После сборки CMake копирует `cfg/` и DLL и создаёт `wc.img` рядом с exe.

#### Тестовый режим OpenXComZX (добавлен в `unreal/Unreal/app/oxz_test.*`)

- `-t <N>` — выйти после N кадров; `-o <файл.png>` — при выходе сохранить
  PNG активной области экрана (по текущему RRES, для 320x240 — 320x240).
  В тестовом режиме звук выключен и эмуляция идёт без паузы до 50 Гц.
- Отладочные порты (номера регистров TS не заняты, на железе запись
  игнорируется): `#F8AF` — символ в stdout; `#F9AF` — байт hex;
  `#FAAF` — выход с кодом = значению в конце кадра (+ снимок, если `-o`);
  `#FBAF` — снимок `<имя>_NNN.png` в конце кадра.
- Правки в исходниках эмулятора: `app/oxz_test.{h,cpp}` (новые),
  `app/emul.cpp` (ключи), `app/mainloop.cpp` (конец кадра, без паузы),
  `devices/io/io.cpp` (порты), `CMakeLists.txt`.

### Снимок `REF/Unreal` (для справки)

Что выяснилось при попытке собрать снимок `REF/Unreal`:
- проект `Unreal2017.vcxproj`, тулсет v142; на машине есть только v145
  (VS 2026) и Windows SDK 10.0.26100.0;
- параллельная сборка падает с нехваткой памяти для PCH — нужно
  `/p:CL_MPCount=4` или меньше;
- **снимок неполный, не собирается**: нет `dxr.h`; в `hardware/sdcard.cpp` и
  `hardware/sound/dev_moonsound.cpp` не подключён `util.h` (`errmsg`);
  в `input.cpp` нет `readdevice`, `readmouse`, `ReadKeyboard`, `dik_scan`,
  `process_msgs`, `tape_bit`, `set_atm_FF77`, `set_banks`, повторные
  определения `ATM_KBD::processzx/setkey`; в `hardware/clones/tsconf.cpp`
  нет `Zc`, `vdac2::is_interrupt`, ресурсов `IDC_TSUTOGGLE_*`/`IDD_TSUTOGGLE`.
- Моя временная копия с правками заменена пользовательской версией `unreal/`.

Вывод: тулчейн проекта — **SDCC 4.5 + sdasz80 + Node.js**, как в
`REF/src_sdcc` и `REF/frontend` (там требуется SDCC ≥ 4.5.0 и Node.js).

## Состав REF

| Папка | Файлов | МБ | Что это |
|---|---:|---:|---|
| `books/` | 636 | 5.7 | ZXDN (статьи: coding, graphic, hardware, music, software), volkov |
| `frontend/` | 599 | 2.8 | Z80-сторона другого проекта: клиент шины, WC-плагин, TR-DOS-приложение, `z80_fw` (SD, FAT, экран, клавиатура, прерывания) |
| `OpenXcom/` | 1118 | 16.5 | исходники OpenXcom (C++), правила, моды; **без оригинальных данных** |
| `src_sdcc/` | 416 | 2.0 | VGM-плеер на SDCC с OPL3: `tsconf.h`, `wc_api.h`, `opl3.s`, `isr.s`, `inflate.asm`, crt0, скрипты сборки |
| `test_service/` | 105 | 1.5 | сервисный тест (уточнить) |
| `tr-dos/` | 18 | 0.7 | дизассемблированный TR-DOS |
| `tsbios/` | 33 | 1.5 | TS-BIOS: исходники, FAT engine |
| `Unreal/` | 343 | 24.1 | исходники эмулятора Unreal Speccy |
| `WC/` | 117 | 1.1 | исходники Wild Commander и плагинов, описание API плагинов |
| `z88dk/` | 20835 | 36.7 | z88dk (библиотеки, заголовки) |
| корень | — | — | TR-DOS описание (PDF/TXT), «Тайники ZX Spectrum» (PDF/TXT) |
