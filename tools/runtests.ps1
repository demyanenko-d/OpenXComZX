# Прогон всех сценариев tests\*.oxs (кроме ufopaedia_ufo — после сборки -Game UFO).
#   powershell -File tools\runtests.ps1                  # все
#   powershell -File tools\runtests.ps1 campaign craft   # выбранные
# Итог: имя=код(кадров); код выхода — число не прошедших.
param([Parameter(ValueFromRemainingArguments = $true)][string[]]$Names)
$root = Split-Path -Parent $PSScriptRoot
if (-not $Names) { $Names = @('selftest', 'ui_geoscape', 'campaign', 'month', 'aliens', 'dogfight', 'dogfight2', 'basedefense', 'ironman', 'lab', 'transfer', 'craft', 'inventory', 'ufopaedia', 'globe', 'bat_render', 'bat_cursor') }
$fail = 0
$sum = @()
foreach ($n in $Names) {
    $o = & powershell -File (Join-Path $root 'tools\run.ps1') -Script "tests\$n.oxs" -Headless 2>&1
    $code = $LASTEXITCODE
    $fr = '?'
    foreach ($l in $o) { if ("$l" -match 'exit code \S+ at frame (\d+)') { $fr = $Matches[1] } }
    if ($code -ne 0) {
        $fail++
        Write-Output "--- $n (код $code):"
        $o | Select-Object -Last 15 | ForEach-Object { Write-Output "  $_" }
    }
    $sum += "$n=$code($fr)"
}
Write-Output ($sum -join ' ')
exit $fail
