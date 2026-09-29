# Готовая карта для настоящего железа: обе игры + загрузчик WC + образ SD.
#   powershell -File tools\dist.ps1            # собрать обе игры и карту
#   powershell -File tools\dist.ps1 -NoBuild   # только разложить и собрать образ (SPG из tmp\dist_spg)
#
# Что делает:
#   1. собирает UFO и TFTD (каждая игра — свой oxz.spg, данные RULES/PAL/LANG/GEO/MCD вшиты внутрь);
#   2. раскладывает tmp\dist — это ровно то, что копируется в корень карты:
#        OXZTFTD.SPG, OXZUFO.SPG        — сами игры, запускаются из WC по ENTER;
#        OXZ\TFTD\*.PAK, OXZ\UFO\*.PAK  — данные, которые игра читает с карты по ходу;
#        boot.$C, WC\, Help.txt         — загрузчик Wild Commander (unreal\soft\WC\exe);
#        README.TXT                     — инструкция (tools\dist_readme.txt);
#   3. делает образ tmp\oxz_card.img (256 МБ, кластер 4 КБ) и кладёт его эмулятору
#      как tmp\sdimg\oxz.img, чтобы играть ровно с той карты, что поедет на железо.
#      Образ — вне tmp\dist: иначе robimg копирует образ внутрь самого себя.
# Сообщения — латиницей: PS 5.1 читает этот файл как ANSI и портит кириллицу в конвейере.
param([switch]$NoBuild)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$dist = Join-Path $root 'tmp\dist'
$spgs = Join-Path $root 'tmp\dist_spg'
$sd = Join-Path $root 'tmp\sd\OXZ'
$wc = Join-Path $root 'unreal\soft\WC\exe'
$img = Join-Path $root 'tmp\oxz_card.img'
$robimg = Join-Path $root 'unreal\tools\robimg\robimg.exe'
# Пакеты, которые игра читает с карты (sdres.c pack_name + music.c + globe_view.s)
$packs = @('BACK', 'UFOP', 'ITEMS', 'CUTS', 'BATUI', 'BATTLE', 'MAPS', 'TILES', 'UNITS',
           'MUSIC', 'MUSICAY', 'MUSICFM', 'GVIEW')

function Build-Game($game, $out) {
    Write-Host "build $game ..."
    & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $root 'tools\build.ps1') -Game $game > $null
    if ($LASTEXITCODE -ne 0) { throw "build $game failed" }
    Copy-Item (Join-Path $root 'tmp\build\oxz.spg') $out -Force
}

New-Item -ItemType Directory -Force $spgs | Out-Null
if (-not $NoBuild) {
    Build-Game 'UFO'  (Join-Path $spgs 'OXZUFO.SPG')
    Build-Game 'TFTD' (Join-Path $spgs 'OXZTFTD.SPG')   # TFTD последней: рабочее дерево остаётся на игре по умолчанию
}

Remove-Item $dist -Recurse -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force $dist | Out-Null
foreach ($n in @('OXZTFTD.SPG', 'OXZUFO.SPG')) {
    $f = Join-Path $spgs $n
    if (-not (Test-Path $f)) { throw "no $f (run without -NoBuild)" }
    Copy-Item $f $dist -Force
}

# Данные обеих игр
foreach ($g in @('TFTD', 'UFO')) {
    $to = Join-Path $dist "OXZ\$g"
    New-Item -ItemType Directory -Force $to | Out-Null
    foreach ($p in $packs) {
        $f = Join-Path $sd "$g\$p.PAK"
        if (Test-Path $f) { Copy-Item $f $to -Force } else { Write-Host "  no $g\$p.PAK" }
    }
}

# Загрузчик WC: без demo (44 МБ чужих демок) и без soft (текстовый процессор в SCL) — карте не нужны
foreach ($n in @('boot.$C', 'Help.txt')) { Copy-Item (Join-Path $wc $n) $dist -Force }
Copy-Item (Join-Path $wc 'WC') $dist -Recurse -Force
Copy-Item (Join-Path $root 'tools\dist_readme.txt') (Join-Path $dist 'README.TXT') -Force

$mb = [int]((Get-ChildItem $dist -Recurse -File | Measure-Object Length -Sum).Sum / 1MB)
Write-Host "dist: $mb MB in $dist"

# Образ карты: 128 МБ, кластер 4 КБ (FAT16: 256 МБ при 4 КБ уже не влезает — robimg
# «Format ... Error e»), -f=0 — раздел в MBR, как на карте, отформатированной из Windows
# (по умолчанию robimg делает том с сектора 0 без MBR; fat.c читает и так, и так).
Remove-Item $img -ErrorAction SilentlyContinue
& $robimg "-p=$img" -s=131072 -o=4096 -f=0 | Out-Null
& $robimg "-p=$img" -a=1 "-C=$dist" | Out-Null
if (-not (Test-Path $img)) { throw 'robimg failed' }

# Эмулятору — та же карта (run.ps1 подставляет tmp\sdimg\oxz.img)
$emuImg = Join-Path $root 'tmp\sdimg\oxz.img'
New-Item -ItemType Directory -Force (Split-Path $emuImg) | Out-Null
Copy-Item $img $emuImg -Force
Write-Host "card image: $img -> $emuImg"
