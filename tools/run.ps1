# Запуск tmp\build\oxz.spg в эмуляторе unreal\build\oxz\Unreal.exe.
#   powershell -File tools\run.ps1                                   # интерактивно
#   powershell -File tools\run.ps1 -Frames 200 -Shot tmp\shot.png    # N кадров, снимок, выход
#   powershell -File tools\run.ps1 -Script tests\ui_geoscape.oxs -Headless   # сценарий без окна
#   powershell -File tools\run.ps1 -Script tests\ui_geoscape.oxs          # сценарий в окне, потом — руками
#   powershell -File tools\run.ps1 -Boot                             # как на железе: без снапшота,
#       TS-BIOS грузится с карты oxz_card.img (boot.$C -> Wild Commander -> ENTER на OXZTFTD.SPG)
#   powershell -File tools\run.ps1 -Cmd tmp\oxz_cmd.txt                   # окно + канал управления:
#       строки, дописанные в этот файл, эмулятор исполняет на лету и после них не выходит —
#       так ведётся живая игра снаружи: мышь, клавиши, снимки, save/load состояния (07 §…)
# Эмулятор запускается из корня проекта: относительные пути в сценариях — от него.
# Код выхода в тестовом режиме: код программы (#FAAF) / сценария (exit),
# #FD — не прошёл expect, #FE — таймаут waitmark, #EE — авария (RST 0).
# -Set 'ZC.SDHC=1','ZC.SDCARD=' — свои ключи ini эмулятора поверх стандартных; сценарий
# может объявить их сам строками «#@set ZC.SDHC=1» (так их видит и runtests.ps1).
param([int]$Frames = 0, [string]$Shot = '', [string]$Script = '', [string]$Wav = '', [string]$Cmd = '', [switch]$Headless, [switch]$Boot, [string[]]$Set = @())
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$out  = Join-Path $root 'tmp\build'
$emu  = Join-Path $root 'unreal\build\oxz'
$ini  = Join-Path $out 'oxz.ini'
New-Item -ItemType Directory -Force (Join-Path $root 'tmp\shots') | Out-Null

# Просто окно (без сценария/кадров/снимка): консоль эмулятора закрывается
# (MISC.HideConsole) — запись Win+G (Game Bar) видит только окно игры.
$interactive = -not ($Frames -gt 0 -or $Shot -or $Script -or $Cmd -or $Headless)
if ($Script) {
    foreach ($l in [System.IO.File]::ReadAllLines((Join-Path $root $Script))) {
        if ($l -match '^#@set\s+(\S+=\S*)') { $Set += $Matches[1] }
    }
}
# -Boot: снапшот не грузим — машина стартует с карты через TS-BIOS, как настоящая
# (карта должна быть FAT32, иначе TS-BIOS пишет «System Meditation: FAT32 NOT FOUND!»)
& node (Join-Path $root 'tools\mkini.js') (Join-Path $emu 'Unreal.ini') $ini `
    'AUTOLOAD.diskA=' `
    "AUTOLOAD.snapshot=$(if ($Boot) { '' } else { Join-Path $out 'oxz.spg' })" `
    "ZC.SDCARD=$(Join-Path $root 'oxz_card.img')" `
    'INPUT.Wheel=KEMPSTON' `
    "MISC.HideConsole=$(if ($interactive) { 1 } else { 0 })" `
    @Set
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
