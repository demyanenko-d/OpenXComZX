# Бенчмарк глобуса: кадры на перерисовку по трём сценариям и каждому зуму (07 §6, globe.md).
#   1. поворот по долготе (X) и по широте (Y) — по 8 шагов, печатается медиана и разброс;
#   2. перерисовка по времени (сменилась «эпоха» солнца, вид тот же) — 4 образца;
#   3. смена масштаба в обе стороны (0->1 … 4->5 и 5->4 … 1->0).
# Вид задаётся поком ctx.globe_lon/lat, время — поком минут (час не меняется, иначе на дальних
# зумах терминатор уходит из окна и тень вырождается). Первый рендер каждого зума отбрасывается.
# Строки вывода — латиницей (PowerShell 5.1 читает скрипт без BOM как ANSI).
#   powershell -File tools\globebench.ps1 [-Hour 18] [-View med|worst] [-Steps 8] [-Out файл]
param([int]$Hour = 18, [string]$View = 'med', [int]$Steps = 8, [string]$Out = '', [switch]$NoPre)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
# виды: медианный и худший по модели (tmp/globe_review2/measure) — lon 16 бит, lat 16384 = 90°
$views = @{ 'med' = @(32768, 4369); 'high' = @(32768, 14564); 'worst' = @(60074, 50972) }
if (-not $views.ContainsKey($View)) { throw "View: med | worst" }
$vl = $views[$View][0]; $vt = $views[$View][1]
$lines = @('waitmark 1 600', 'poke _alien_off 1', 'pokew _cursor_x 110', 'pokew _cursor_y 100', 'click L', 'waitmark 2 300',
	'pokew _cursor_x 119', 'pokew _cursor_y 172', 'click L', 'waitmark 43 3000', 'pokew _cursor_x 120', 'pokew _cursor_y 110',
	'click L', 'waitmark 42 3000', 'type bench', 'key ENTER', 'waitmark 32 3000', 'poke _gl_dbg 1', "poke _gv_off $(if ($NoPre) { 1 } else { 0 })", 'wait 200',
	"poke 06:0038 $Hour", 'poke 06:0037 0', 'poke 06:0036 0', 'wait 250')
function Set-View { param($l, $t, $m) @("pokew _ctx+6 $l", "pokew _ctx+8 $t", "poke 06:0037 $m", "wait 8", "waitmark 32 3000") }
foreach ($z in 0..5) {
	$lines += Set-View $vl $vt 0                                # ставим вид: пок минут меняет эпоху солнца -> перерисовка
	for ($i = 0; $i -lt $Steps; $i++) { $lines += @('key RIGHT', 'wait 8', 'waitmark 32 3000') }
	$lines += Set-View $vl $vt 6
	for ($i = 0; $i -lt $Steps; $i++) { $lines += @('key UP', 'wait 8', 'waitmark 32 3000') }
	foreach ($m in @(12, 24, 36, 48)) { $lines += @("poke 06:0037 $m", 'wait 250') }   # эпоха солнца
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
$rot = @{}; $tim = @{}; $zin = @{}; $zout = @{}
$pz = -1; $plon = -1; $plat = -1; $skip = @{}
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
			# первый рендер после установки вида (сменились обе координаты) — не поворот
			$k = if ($lon -ne $plon -and $lat -eq $plat) { "x$z" } elseif ($lat -ne $plat -and $lon -eq $plon) { "y$z" } else { $null }
			if ($k) { if (-not $rot.ContainsKey($k)) { $rot[$k] = @() }; $rot[$k] += $f }
		}
		$pz = $z; $plon = $lon; $plat = $lat
	}
}
function Stat { param($a)
	if (-not $a -or -not $a.Count) { return '-' }
	$s = $a | Sort-Object
	$med = $s[[int]([Math]::Floor($s.Count / 2))]
	if ($s[0] -eq $s[-1]) { return "$med" }
	return "$med ($($s[0])-$($s[-1]))"
}
$md = @("| zoom | rotate X | rotate Y | shadow (time) | zoom in -> | zoom out <- |", '|---|---:|---:|---:|---:|---:|')
foreach ($z in 0..5) { $md += "| $z | $(Stat $rot["x$z"]) | $(Stat $rot["y$z"]) | $(Stat $tim[$z]) | $(Stat $zin[$z]) | $(Stat $zout[$z]) |" }
$md += ''
$md += "frames of 50 Hz per render; median (min-max) of $Steps steps; view '$View', hour $Hour; log: tmp\globebench_log.txt"
$text = $md -join "`n"
Write-Output $text
if ($Out) { [IO.File]::WriteAllText((Join-Path $root $Out), $text + "`n") }
