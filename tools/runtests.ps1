# Прогон всех сценариев tests\*.oxs (кроме ufopaedia_ufo — после сборки -Game UFO).
#   powershell -File tools\runtests.ps1                  # все
#   powershell -File tools\runtests.ps1 campaign craft   # выбранные
# Итог: имя=код(кадров); код выхода — число не прошедших.
#
# Перед каждым сценарием журнал аварий эмулятора (tmp\oxz_crash.log) удаляется, а после —
# читается. Ловушки делятся на две части: аварийные валят сценарий (запись DMA в страницы
# ядра, потеря PC, переполнение стека, устаревшее чтение кэша, разбалансированный стек
# прерывания), предупреждающие только печатаются (просадка стека, запись выше SP). Без этой
# проверки ловушки писали в лог, а набор оставался зелёным (ревизия 2026-09-26).
# Сообщения — латиницей: PS 5.1 читает этот файл как ANSI и портит кириллицу в конвейере.
param([Parameter(ValueFromRemainingArguments = $true)][string[]]$Names)
$root = Split-Path -Parent $PSScriptRoot
if (-not $Names) { $Names = @('selftest', 'ui_geoscape', 'campaign', 'month', 'aliens', 'dogfight', 'dogfight2', 'basedefense', 'ironman', 'lab', 'transfer', 'craft', 'inventory', 'ufopaedia', 'globe', 'intercept', 'bat_render', 'bat_cursor', 'bat_doors', 'bat_levels', 'bat_kneel', 'sdinfo', 'sdinfo_dma', 'sdinfo_sdhc', 'sdinfo_nocard', 'sdinfo_grub', 'sdinfo_nofat', 'intercept_live', 'ufopaedia+nodma', 'bat_render+nodma') }
# Варианты образа карты (MBR от GRUB и т.п.) — из oxz_card.img, если он новее
if ($Names | Where-Object { $_ -match '^sdinfo_(grub|nofat)' }) { & node (Join-Path $root 'tools\cardvariants.js') | Out-Null }
$log = Join-Path $root 'tmp\oxz_crash.log'
$bad = 'OXZ: (DMA INTO KERNEL|PC LOST|STACK OVERFLOW|CACHE STALE|ISR STACK|KERNEL CODE WRITE|IX SLOT)'
$warn = 'OXZ: (STACK LOW|STACK WRITE|POP MISMATCH|POP PATH)'
$fail = 0
$sum = @()
foreach ($n in $Names) {
    Remove-Item $log -ErrorAction SilentlyContinue
    # «имя+nodma» — тот же сценарий на машине без канала DMA SPI (ZC.FaultDmaSpi=1): игра
    # сама переходит на посекторное чтение карты (fat.c dma_check, 23 §5)
    $script = $n; $extra = @()
    if ($n -match '^(.+)\+nodma$') { $script = $Matches[1]; $extra = @('-Set', 'ZC.FaultDmaSpi=1') }
    $o = & powershell -File (Join-Path $root 'tools\run.ps1') -Script "tests\$script.oxs" -Headless @extra 2>&1
    $code = $LASTEXITCODE
    $fr = '?'
    foreach ($l in $o) { if ("$l" -match 'exit code \S+ at frame (\d+)') { $fr = $Matches[1] } }
    $traps = @(); $warns = @()
    if (Test-Path $log) {
        $traps = @(Select-String -Path $log -Pattern $bad | ForEach-Object { $_.Line } | Select-Object -Unique)
        $warns = @(Select-String -Path $log -Pattern $warn | ForEach-Object { $_.Line } | Select-Object -Unique)
    }
    if ($code -ne 0 -or $traps.Count) {
        $fail++
        Write-Output "--- $n (exit $code, traps $($traps.Count)):"
        if ($code -ne 0) { $o | Select-Object -Last 15 | ForEach-Object { Write-Output "  $_" } }
        $traps | Select-Object -First 5 | ForEach-Object { Write-Output "  $_" }
        if ($traps.Count -and $code -eq 0) { $code = 'TRAP' }
    }
    elseif ($warns.Count) {
        Write-Output "--- $n : warnings $($warns.Count)"
        $warns | Select-Object -First 3 | ForEach-Object { Write-Output "  $_" }
    }
    $sum += "$n=$code($fr)"
}
Write-Output ($sum -join ' ')
exit $fail
