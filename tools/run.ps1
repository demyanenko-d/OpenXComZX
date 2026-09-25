# Запуск tmp\build\oxz.spg в эмуляторе unreal\build\oxz\Unreal.exe.
#   powershell -File tools\run.ps1                                   # интерактивно
#   powershell -File tools\run.ps1 -Frames 200 -Shot tmp\shot.png    # N кадров, снимок, выход
#   powershell -File tools\run.ps1 -Script tests\ui_geoscape.oxs -Headless   # сценарий без окна
#   powershell -File tools\run.ps1 -Script tests\ui_geoscape.oxs          # сценарий в окне, потом — руками
#   powershell -File tools\run.ps1 -Cmd tmp\oxz_cmd.txt                   # окно + канал управления:
#       строки, дописанные в этот файл, эмулятор исполняет на лету и после них не выходит —
#       так ведётся живая игра снаружи: мышь, клавиши, снимки, save/load состояния (07 §…)
# Эмулятор запускается из корня проекта: относительные пути в сценариях — от него.
# Код выхода в тестовом режиме: код программы (#FAAF) / сценария (exit),
# #FD — не прошёл expect, #FE — таймаут waitmark, #EE — авария (RST 0).
param([int]$Frames = 0, [string]$Shot = '', [string]$Script = '', [string]$Wav = '', [string]$Cmd = '', [switch]$Headless)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$out  = Join-Path $root 'tmp\build'
$emu  = Join-Path $root 'unreal\build\oxz'
$ini  = Join-Path $out 'oxz.ini'
New-Item -ItemType Directory -Force (Join-Path $root 'tmp\shots') | Out-Null

# Просто окно (без сценария/кадров/снимка): консоль эмулятора закрывается
# (MISC.HideConsole) — запись Win+G (Game Bar) видит только окно игры.
$interactive = -not ($Frames -gt 0 -or $Shot -or $Script -or $Cmd -or $Headless)
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
# -Wav: запись микшированного звука эмулятора в WAV (разбор музыки, 07 §…)
if ($Wav) { $argv += @('-a', [System.IO.Path]::GetFullPath((Join-Path $root $Wav))) }
# Канал управления: файл команд создаётся заранее, эмулятор дочитывает его на ходу
if ($Cmd) {
    $cmdFull = [System.IO.Path]::GetFullPath((Join-Path $root $Cmd))
    New-Item -ItemType Directory -Force (Split-Path $cmdFull) | Out-Null
    if (-not (Test-Path $cmdFull)) { [IO.File]::WriteAllText($cmdFull, '') }
    $argv += @('-p', $cmdFull)
}
if ($Headless) { $argv += '-H' }

Push-Location $root
try { & (Join-Path $emu 'Unreal.exe') @argv; $code = $LASTEXITCODE }
finally { Pop-Location }
exit $code
