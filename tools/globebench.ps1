# Бенчмарк глобуса: кадры на три вида перерисовки по каждому зуму (07 §6, globe.md §6.13).
#   1. перерисовка по времени (сменилась «эпоха» солнца — вид тот же);
#   2. смена масштаба в обе стороны (0->1 … 4->5 и 5->4 … 1->0);
#   3. поворот по долготе (X) и по широте (Y).
# Считает эмулятор по отладочным строкам «globe:» (кадры 50 Гц на рендер). Сборка — текущая.
# Строки вывода — латиницей (PowerShell 5.1 читает скрипт без BOM как ANSI).
#   powershell -File tools\globebench.ps1 [-Hour 18] [-Out tmp\globebench.md]
param([int]$Hour = 18, [string]$Out = '')
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$lines = @('waitmark 1 600', 'poke _alien_off 1', 'pokew _cursor_x 110', 'pokew _cursor_y 100', 'click L', 'waitmark 2 300',
	'pokew _cursor_x 119', 'pokew _cursor_y 172', 'click L', 'waitmark 43 3000', 'pokew _cursor_x 120', 'pokew _cursor_y 110',
	'click L', 'waitmark 42 3000', 'type bench', 'key ENTER', 'waitmark 32 3000', 'wait 200', "poke 06:0038 $Hour", 'wait 250')
$h = $Hour
foreach ($z in 0..5) {
	foreach ($k in @('RIGHT', 'LEFT', 'UP', 'DOWN')) { $lines += @("key $k", 'wait 8', 'waitmark 32 3000') }
	$h = ($h + 1) % 24                                  # смена часа -> новая «эпоха» солнца
	$lines += @("poke 06:0038 $h", 'wait 250')
	if ($z -lt 5) { $lines += @('key SS+K', 'wait 8', 'waitmark 32 3000') }
}
foreach ($z in 1..5) { $lines += @('key SS+J', 'wait 8', 'waitmark 32 3000') }
$lines += 'exit 0'
$scr = 'tmp\globebench.oxs'
[IO.File]::WriteAllText((Join-Path $root $scr), ($lines -join "`n") + "`n")
Push-Location $root
$outp = & powershell -File (Join-Path $root 'tools\run.ps1') -Script $scr -Headless 2>&1 | Out-String
Pop-Location
[IO.File]::WriteAllText((Join-Path $root 'tmp\globebench_log.txt'), $outp)
# разбор: вид (зум, долгота, широта) прошлой строки -> что изменилось в этой
$rot = @{}; $tim = @{}; $zin = @{}; $zout = @{}
$pz = -1; $plon = -1; $plat = -1
foreach ($l in ($outp -split "`n")) {
	if ($l -match 'globe: sun only, zoom (\d+), frames (\d+)') {
		$z = [int]$Matches[1]; $f = [int]$Matches[2]
		if (-not $tim.ContainsKey($z)) { $tim[$z] = @() }
		$tim[$z] += $f
	}
	elseif ($l -match 'globe: zoom (\d+), cells \d+, edges \d+, frames (\d+), view (\d+) (\d+)') {
		$z = [int]$Matches[1]; $f = [int]$Matches[2]; $lon = [int]$Matches[3]; $lat = [int]$Matches[4]
		if ($pz -ge 0 -and $z -ne $pz) {
			if ($z -gt $pz) { if (-not $zin.ContainsKey($z)) { $zin[$z] = @() }; $zin[$z] += $f }
			else { if (-not $zout.ContainsKey($z)) { $zout[$z] = @() }; $zout[$z] += $f }
		}
		elseif ($pz -ge 0) {
			$k = if ($lon -ne $plon) { "x$z" } elseif ($lat -ne $plat) { "y$z" } else { $null }
			if ($k) { if (-not $rot.ContainsKey($k)) { $rot[$k] = @() }; $rot[$k] += $f }
		}
		$pz = $z; $plon = $lon; $plat = $lat
	}
}
$avg = { param($a) if ($a -and $a.Count) { '{0:N1}' -f (($a | Measure-Object -Average).Average) } else { '-' } }
$md = @('| zoom | rotate X | rotate Y | shadow (time) | zoom in -> | zoom out <- |', '|---|---:|---:|---:|---:|---:|')
foreach ($z in 0..5) {
	$md += "| $z | $(& $avg $rot["x$z"]) | $(& $avg $rot["y$z"]) | $(& $avg $tim[$z]) | $(& $avg $zin[$z]) | $(& $avg $zout[$z]) |"
}
$md += ''
$md += "frames of 50 Hz per render; hour $Hour (terminator in view); log: tmp\globebench_log.txt"
$text = $md -join "`n"
Write-Output $text
if ($Out) { [IO.File]::WriteAllText((Join-Path $root $Out), $text + "`n") }
