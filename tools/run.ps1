# Запуск tmp\build\oxz.spg в эмуляторе unreal\build\oxz\Unreal.exe.
#   powershell -File tools\run.ps1                                   # интерактивно
#   powershell -File tools\run.ps1 -Frames 200 -Shot tmp\shot.png    # N кадров, снимок, выход
#   powershell -File tools\run.ps1 -Script tests\ui_geoscape.oxs -Headless   # сценарий без окна
#   powershell -File tools\run.ps1 -Script tests\ui_geoscape.oxs          # сценарий в окне, потом — руками
# Эмулятор запускается из корня проекта: относительные пути в сценариях — от него.
# Код выхода в тестовом режиме: код программы (#FAAF) / сценария (exit),
# #FD — не прошёл expect, #FE — таймаут waitmark, #EE — авария (RST 0).
param([int]$Frames = 0, [string]$Shot = '', [string]$Script = '', [switch]$Headless)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$out  = Join-Path $root 'tmp\build'
$emu  = Join-Path $root 'unreal\build\oxz'
$ini  = Join-Path $out 'oxz.ini'
New-Item -ItemType Directory -Force (Join-Path $root 'tmp\shots') | Out-Null

# Просто окно (без сценария/кадров/снимка): консоль эмулятора закрывается
# (MISC.HideConsole) — запись Win+G (Game Bar) видит только окно игры.
$interactive = -not ($Frames -gt 0 -or $Shot -or $Script -or $Headless)
& node (Join-Path $root 'tools\mkini.js') (Join-Path $emu 'Unreal.ini') $ini `
    'AUTOLOAD.diskA=' `
    "AUTOLOAD.snapshot=$(Join-Path $out 'oxz.spg')" `
    "ZC.SDCARD=$(Join-Path $root 'tmp\sdimg\oxz.img')" `
    'INPUT.Wheel=KEMPSTON' `
    "MISC.HideConsole=$(if ($interactive) { 1 } else { 0 })"
if ($LASTEXITCODE -ne 0) { throw 'mkini failed' }

$argv = @('-c', $ini, '-l', (Join-Path $out 'oxz.labels'))
if ($Frames -gt 0) { $argv += @('-t', "$Frames") }
if ($Shot) { $argv += @('-o', [System.IO.Path]::GetFullPath((Join-Path $root $Shot))) }
if ($Script) { $argv += @('-s', [System.IO.Path]::GetFullPath((Join-Path $root $Script))) }
if ($Headless) { $argv += '-H' }

Push-Location $root
try { & (Join-Path $emu 'Unreal.exe') @argv; $code = $LASTEXITCODE }
finally { Pop-Location }
exit $code
