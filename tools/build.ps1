# Сборка OpenXComZX: SDCC -> Intel HEX -> проверка map -> SPG (+ пакеты данных) + метки.
# Раскладка — src/inc/memmap.h, project_docs/08_ui_port_plan.md §4.
# Все выходные файлы — в tmp\build (правило проекта: артефакты только в tmp\).
#   powershell -File tools\build.ps1 [-Game TFTD|UFO]
param([string]$Game = 'TFTD')
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$src  = Join-Path $root 'src'
$out  = Join-Path $root 'tmp\build'
New-Item -ItemType Directory -Force $out | Out-Null

$KERNEL_PAGE = '0x04'; $DATA_PAGE = '0x05'
$CODE_LOC = '0x0100'; $CODE_END = '0x3800'   # общий код в Win0
$DATA_LOC = '0x4000'; $DATA_END = '0x7C00'   # данные в Win1
$DMABUF_LOC = '0x7C00'; $DMABUF_END = '0x7FE0' # буферы DMA (чётная база); #7FE0-#7FF0 — заглушка входа
$ENTRY = '0x7FE0'; $STACK_TOP = '0x3E00'
$RES_PAGE = 0x50; $RES_LAST = 0xAF          # пакеты данных (memmap.h RES_PAGE..RES_LAST)

# Общий код (Win0): ассемблер ядра и C-модули без банка.
$asm_common = @('kernel\crt0', 'kernel\bank', 'kernel\dmabuf', 'kernel\glyph', 'kernel\mul32', 'kernel\sd', 'kernel\pages', 'kernel\far', 'kernel\res', 'kernel\dbg', 'kernel\input', 'ui\scrutil', 'kernel\text')   # crt0 — первым: порядок областей
$c_common   = @()   # Win0 — только ассемблер (14_todo.md §1.3)

# Банки кода (Win2): номер -> физическая страница и файлы C.
# Страницы: резидентные #30-#3F, оверлей #40-#4F (08 §4.2, §4.3a).
$banks = @(
    @{ n = 1; page = 0x30; files = @('ui\ui', 'ui\screens') }   # ядро интерфейса + диспетчер экранов
    @{ n = 2; page = 0x31; files = @('ui\scr_menu', 'ui\names', 'ui\scr_end', 'ui\globe_ui') }   # меню; имена объектов для всех окон; концовки; точки глобуса
    @{ n = 3; page = 0x32; files = @('ui\scr_geo') }
    @{ n = 4; page = 0x33; files = @('ui\scr_geo2', 'ui\scr_graph') }
    @{ n = 5; page = 0x34; files = @('ui\scr_base') }
    @{ n = 6; page = 0x35; files = @('ui\scr_base2') }
    @{ n = 7; page = 0x36; files = @('game\save', 'game\newgame', 'game\econ', 'kernel\bios') }   # bios — только для save.c
    @{ n = 8; page = 0x37; files = @('game\gtime', 'game\lab') }
    @{ n = 9; page = 0x38; files = @('ui\scr_lab') }
    @{ n = 10; page = 0x39; files = @('ui\scr_craft') }
    @{ n = 11; page = 0x3A; files = @('kernel\gfx', 'kernel\cursor', 'kernel\boot') }   # графика, опрос ввода, запуск
    @{ n = 12; page = 0x3B; files = @('kernel\fat', 'kernel\sdres') }
    @{ n = 13; page = 0x3C; files = @('ui\scr_ufop') }
    @{ n = 14; page = 0x3E; files = @('test\bank_a') }
    @{ n = 15; page = 0x3F; files = @('test\bank_b') }
    @{ n = 16; page = 0x3D; files = @('game\month') }   # конец месяца
    @{ n = 17; page = 0x40; files = @('game\world', 'game\geo') }   # ГСЧ, регионы, сфера, маска суши
    @{ n = 18; page = 0x41; files = @('game\alien') }   # стратегия и миссии пришельцев
    @{ n = 19; page = 0x42; files = @('game\ufo') }     # НЛО, обнаружение, места миссий, базы пришельцев
    @{ n = 20; page = 0x43; files = @('game\craft') }   # полёты кораблей X-COM
    @{ n = 21; page = 0x44; files = @('ui\scr_fly') }   # окна полёта: перехват, корабль, цель
    @{ n = 22; page = 0x45; files = @('game\dogfight') }   # воздушный бой: логика
    @{ n = 23; page = 0x46; files = @('ui\scr_dogf') }     # воздушный бой: окна
    @{ n = 24; page = 0x47; files = @('ui\globe', 'ui\globe_s', 'ui\globe_sq'); tab = 0xBC00 }   # глобус (globe.md): globe_s.s — асм, globe_sq.s — таблица _GTAB с #BC00
    @{ n = 25; page = 0x48; files = @('ui\globe_sh', 'ui\globe_sh_s', 'ui\globe_view') }   # тень глобуса (globe.md §6.4): globe_sh_s.s — асм; globe_view.c — предрасчитанные виды (§12.5)
)

# Пакеты данных игры: выход конвертера OxzConv (tmp\sd\OXZ\<игра>), вшиваются в SPG.
$packs = @('PAL', 'LANG', 'RULES', 'GEO')

function Invoke-Tool([string]$exe, [string[]]$argv) {
    & $exe @argv
    if ($LASTEXITCODE -ne 0) { throw "$exe failed with code $LASTEXITCODE" }
}

# Конвертер OxzConv (C#, tools\OxzConv) пересобирается, если его исходники новее exe.
$conv = Join-Path $root 'tmp\oxzconv\bin\Release\OxzConv.exe'
$convSrc = Get-ChildItem (Join-Path $root 'tools\OxzConv') -Recurse -File -Include *.cs, *.csproj, *.txt, *.json, *.props
if (-not (Test-Path $conv) -or ($convSrc | Where-Object { $_.LastWriteTime -gt (Get-Item $conv).LastWriteTime })) {
    & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $root 'tools\dotnet.ps1') build (Join-Path $root 'tools\OxzConv\OxzConv.csproj') -c Release -v q
    if ($LASTEXITCODE -ne 0) { throw 'OxzConv build failed' }
}

# Данные игры: если нет или конвертер новее — сконвертировать копию из Steam\.
$gameDirs = @{ TFTD = 'Steam\X-COM Terror from the Deep\TFD'; UFO = 'Steam\XCom UFO Defense\XCOM' }
$data = Join-Path $root "tmp\sd\OXZ\$Game"
$stamp = Join-Path $data 'RULES.PAK'
if (-not (Test-Path $stamp) -or (Get-Item $conv).LastWriteTime -gt (Get-Item $stamp).LastWriteTime) {
    Invoke-Tool $conv @('--game', (Join-Path $root $gameDirs[$Game]), '--out', (Join-Path $root 'tmp\sd'))
}

# Образ SD для эмулятора (tmp\sdimg\oxz.img, 64 МБ без таблицы разделов, кластер
# 4 КБ -> FAT16): каталог tmp\sd (OXZ\<игра>\*.PAK) — пакеты, которые игра читает
# с карты (BACK, UFOP, ITEMS). robimg — unreal\tools\robimg (пути — абсолютные;
# -o — размер кластера).
$img = Join-Path $root 'tmp\sdimg\oxz.img'
$robimg = Join-Path $root 'unreal\tools\robimg\robimg.exe'
if (-not (Test-Path $img) -or (Get-Item $stamp).LastWriteTime -gt (Get-Item $img).LastWriteTime) {
    New-Item -ItemType Directory -Force (Split-Path $img) | Out-Null
    Remove-Item $img -ErrorAction SilentlyContinue
    & $robimg "-p=$img" -s=65536 -o=4096 | Out-Null
    & $robimg "-p=$img" -a=1 "-C=$(Join-Path $root 'tmp\sd')" | Out-Null
    if (-not (Test-Path $img)) { throw 'robimg failed' }
    Write-Host "sd image: $img"
}

# Заголовки из реестров конвертера — в tmp\build\gen
$gen = Join-Path $out 'gen'
Invoke-Tool $conv @('headers', $gen)

# --debug: отладочные записи (.adb -> oxz.cdb) с адресами и статических функций —
# для меток отладчика и профилировщика эмулятора (tools/mklabels.js); код не меняется.
$cflags = @('-mz80', '--sdcccall', '1', '--opt-code-size', '--std-sdcc11', '--debug', '-I', (Join-Path $src 'inc'), '-I', (Join-Path $src 'ui'), '-I', (Join-Path $src 'game'), '-I', $gen)
$rels = @()
$utf8 = New-Object System.Text.UTF8Encoding $false

# Таблица «банк -> страница» для set_bank (src/kernel/bank.s)
$maxBank = ($banks | ForEach-Object { $_.n } | Measure-Object -Maximum).Maximum
$tbl = @("`t.module banks", "`t.area _CODE", '_bank_page::', "`t.db 0x02`t; bank 0: Win2 page at SPG start")
for ($n = 1; $n -le $maxBank; $n++) {
    $b = $banks | Where-Object { $_.n -eq $n }
    $p = if ($b) { $b.page } else { 0x02 }
    $tbl += ("`t.db 0x{0:X2}`t; bank {1}" -f $p, $n)
}
$banksAsm = Join-Path $out 'banks.s'
[System.IO.File]::WriteAllText($banksAsm, ($tbl -join "`r`n") + "`r`n", $utf8)

# Раскладка пакетов по страницам и таблица pack_page для src/kernel/res.c
$page = $RES_PAGE
$dataArgs = @()
$ptbl = @("`t.module packs", "`t.area _CODE", '_pack_page::')
foreach ($p in $packs) {
    $f = Join-Path $data "$p.PAK"
    if (-not (Test-Path $f)) { throw "no data pack $f" }
    $pages = [int][Math]::Ceiling((Get-Item $f).Length / 16384)
    if ($page + $pages - 1 -gt $RES_LAST) { throw "packs do not fit into #$('{0:X2}' -f $RES_PAGE)-#$('{0:X2}' -f $RES_LAST)" }
    $dataArgs += @('--data', ('0x{0:X2}:{1}' -f $page, $f))
    $ptbl += ("`t.db 0x{0:X2}`t; {1}.PAK, {2} pages" -f $page, $p, $pages)
    $page += $pages
}
$ptbl += '_pack_count::', ("`t.db {0}" -f $packs.Count)
$packsAsm = Join-Path $out 'packs.s'
[System.IO.File]::WriteAllText($packsAsm, ($ptbl -join "`r`n") + "`r`n", $utf8)

foreach ($a in $asm_common) {
    $rel = Join-Path $out ((Split-Path $a -Leaf) + '.rel')
    Invoke-Tool sdasz80 @('-plosgff', '-o', $rel, (Join-Path $src "$a.s"))
    $rels += $rel
}
foreach ($g in @($banksAsm, $packsAsm)) {
    $rel = Join-Path $out ([System.IO.Path]::GetFileNameWithoutExtension($g) + '.rel')
    Invoke-Tool sdasz80 @('-plosgff', '-o', $rel, $g)
    $rels += $rel
}

foreach ($c in $c_common) {
    $rel = Join-Path $out ((Split-Path $c -Leaf) + '.rel')
    Invoke-Tool sdcc ($cflags + @('-c', (Join-Path $src "$c.c"), '-o', $rel))
    $rels += $rel
}

$bankLink = @(); $bankCheck = @(); $bankMap = @()
foreach ($b in $banks) {
    $n = $b.n
    $seg = "BANK$n"
    foreach ($c in $b.files) {
        $rel = Join-Path $out ((Split-Path $c -Leaf) + '.rel')
        $s = Join-Path $src "$c.s"               # ассемблер банка: область .area _BANKn в самом файле
        if (Test-Path $s) { Invoke-Tool sdasz80 @('-plosgff', '-o', $rel, $s) }
        else { Invoke-Tool sdcc ($cflags + @('--codeseg', $seg, '--constseg', $seg, '-c', (Join-Path $src "$c.c"), '-o', $rel)) }
        $rels += $rel
    }
    $base = $n * 0x10000 + 0x8000
    # --codeseg BANKn даёт область _BANKn; tab — выровненная таблица _GTAB в конце банка
    $bankLink  += ('-Wl-b_{0}=0x{1:X}' -f $seg, $base)
    if ($b.tab) {
        $tabAt = $n * 0x10000 + $b.tab
        $bankLink  += ('-Wl-b_GTAB=0x{0:X}' -f $tabAt)
        $bankCheck += ('_{0}=0x{1:X}-0x{2:X}' -f $seg, $base, $tabAt)
        $bankCheck += ('_GTAB=0x{0:X}-0x{1:X}' -f $tabAt, ($base + 0x4000))
    } else {
        $bankCheck += ('_{0}=0x{1:X}-0x{2:X}' -f $seg, $base, ($base + 0x4000))
    }
    $bankMap   += @('--bank', ('{0}:0x{1:X2}' -f $n, $b.page))
}

# Ошибка кодогенерации SDCC 4.5 (сравнение с памятью через sub) — tools/checkasm.js
$asmFiles = $rels | ForEach-Object { [System.IO.Path]::ChangeExtension($_, '.asm') } | Where-Object { Test-Path $_ }
Invoke-Tool node (@((Join-Path $root 'tools\checkasm.js')) + $asmFiles)

$ihx = Join-Path $out 'oxz.ihx'
Invoke-Tool sdcc (@('-mz80', '--sdcccall', '1', '--no-std-crt0', '--debug',
    '--code-loc', $CODE_LOC, '--data-loc', $DATA_LOC, "-Wl-b_DMABUF=$DMABUF_LOC") + $bankLink + @('-o', $ihx) + $rels)

Invoke-Tool node (@((Join-Path $root 'tools\checkmap.js'), (Join-Path $out 'oxz.map'),
    "_CODE,_HOME,_INITIALIZER,_GSINIT,_GSFINAL=$CODE_LOC-$CODE_END",
    "_DATA,_INITIALIZED,_BSEG,_BSS,_HEAP,_KDATA=$DATA_LOC-$DATA_END",
    "_DMABUF=$DMABUF_LOC-$DMABUF_END") + $bankCheck)

$winmap = @('--win', "0:$KERNEL_PAGE", '--win', "1:$DATA_PAGE") + $bankMap
Invoke-Tool node (@((Join-Path $root 'tools\mkspg.js'), $ihx, (Join-Path $out 'oxz.spg'),
    '--pc', $ENTRY, '--sp', $STACK_TOP) + $winmap + $dataArgs)
Invoke-Tool node (@((Join-Path $root 'tools\mklabels.js'), (Join-Path $out 'oxz.noi'),
    (Join-Path $out 'oxz.labels'), '--cdb', (Join-Path $out 'oxz.cdb')) + $winmap)
