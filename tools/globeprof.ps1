# Раскладка времени рендера глобуса по зумам (профилировщик эмулятора, 07 §6).
# Для каждого зума 0..5: новая игра -> база -> геоскейп -> зум -> профиль одного поворота
# (полный рендер без смены зума/наклона). Функции сводятся в группы, вывод — таблица
# кадров (71 680 тактов профиля = кадр) по группам. Сборка — текущая (tmp\build\oxz.spg).
# Строки вывода — латиницей (PowerShell 5.1 читает скрипт без BOM как ANSI).
#   powershell -File tools\globeprof.ps1 [-Zooms 0,1,2] [-Hour 18] [-Out tmp\globeprof.md]
# -Hour — час игры перед замером (терминатор в окне: 18; по умолчанию время не трогается).
param([int[]]$Zooms = @(0, 1, 2, 3, 4, 5), [int]$Hour = -1, [string]$Out = '')
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$groups = [ordered]@{
	'project (vertices)' = @('_gl_project', 'vidx', 'globe$project')
	'tables (per view)'  = @('_gl_ktab', 'kt_put16', 'kt_lo16', '_gl_ztab', 'globe$tables')
	'edge setup'         = @('_gl_edges', 'edge1', 'edge1s', 'e_rows', 'e_fast', 'hclip', 'limb_l', 'lerpz', 'xclip', 'lerpx', 'div32', 'eslope', 'bord_ev', 'e_store', 'mul8e', 'mulu16')
	'AEL (insert/fix/step/sort)' = @('_gl_rows', 'rw_row', 'ael_ins', 'ai_cmp', 'fx_local', 'ael_fix', 'fx_pair', 'fx_eq', 'ael_step', 'ael_sort')
	'runs -> DMA'        = @('rw_emit', 'rw_band', 'rw_pfill', 'rw_flush', '_gl_rows_pre', 'em_run_p')
	'shadow (bank 25)'   = @('_sh_rows', 'build', 'iv_put', 'iv_dma', 'lvl_of', 'add32', '_sh_ramp', 'globe_sh$globe_shadow', 'globe_sh$tables', 'globe_sh$ramp', 'rw_emit_s', 'em_run_s')
	'cells, C, copies'   = @('globe_view$gview_load', 'globe_view$gview_pick', 'globe$render', 'globe$rows_init', 'globe$bg_restore', '___mulsint2slong', '___muluint2ulong', '_far_read', '_far_fill', '_far_copy', '_far_byte', '_memset', '_memcpy', 'globe$blit')
}
$rows = @()
foreach ($z in $Zooms) {
	$lines = @('waitmark 1 600', 'poke _alien_off 1', 'pokew _cursor_x 110', 'pokew _cursor_y 100', 'click L', 'waitmark 2 300',
		'pokew _cursor_x 119', 'pokew _cursor_y 172', 'click L', 'waitmark 43 3000', 'pokew _cursor_x 120', 'pokew _cursor_y 110',
		'click L', 'waitmark 42 3000', 'type prof', 'key ENTER', 'waitmark 32 3000', 'poke _gl_dbg 1')
	for ($i = 0; $i -lt $z; $i++) { $lines += @('key SS+K', 'wait 8', 'waitmark 32 3000') }
	if ($Hour -ge 0) { $lines += @("poke 06:0038 $Hour", 'wait 300') }   # ST->hour; смена эпохи солнца — перерисовка
	$lines += @('wait 10', 'profile on', 'key RIGHT', 'wait 8', 'waitmark 32 3000', 'profile off 200 ops', 'exit 0')
	$scr = "tmp\globeprof_z$z.oxs"                 # run.ps1 ждёт путь от корня проекта
	[IO.File]::WriteAllText((Join-Path $root $scr), ($lines -join "`n") + "`n")
	Push-Location $root
	$outp = & powershell -File (Join-Path $root 'tools\run.ps1') -Script $scr -Headless 2>&1 | Out-String
	Pop-Location
	[IO.File]::WriteAllText((Join-Path $root "tmp\prof_z$z.txt"), $outp)   # для tools\globeprof_sum.js
	$tot = 0; $sum = @{}; foreach ($g in $groups.Keys) { $sum[$g] = 0 }; $other = 0
	foreach ($l in ($outp -split "`n")) {
		if ($l -match 'OXZ: profile (\d+) T') { $tot = [double]$Matches[1] }
		elseif ($l -match 'OXZ: prof\s+[\d.]+%\s+(\d+)\s+(\S+)') {
			$t = [double]$Matches[1]; $n = $Matches[2]; $hit = $false
			foreach ($g in $groups.Keys) { if ($groups[$g] -contains $n) { $sum[$g] += $t; $hit = $true; break } }
			if (-not $hit) { $other += $t }
		}
	}
	$g0 = ($outp -split "`n" | Where-Object { $_ -match '^globe: ' } | Select-Object -Last 1)
	$rows += [pscustomobject]@{ z = $z; total = $tot; sum = $sum; other = $other; info = "$g0".Trim() }
}
$f = { param($t) '{0:N1}' -f ($t / 71680) }
$hdr = '| group |' + (($rows | ForEach-Object { " zoom $($_.z) |" }) -join '')
$sep = '|---|' + (($rows | ForEach-Object { '---|' }) -join '')
$md = @($hdr, $sep)
foreach ($g in $groups.Keys) { $md += "| $g |" + (($rows | ForEach-Object { " $(& $f $_.sum[$g]) |" }) -join '') }
$md += '| other (UI, frame wait) |' + (($rows | ForEach-Object { " $(& $f $_.other) |" }) -join '')
$md += '| TOTAL, frames |' + (($rows | ForEach-Object { " $(& $f $_.total) |" }) -join '')
$md += ''
foreach ($r in $rows) { $md += "- zoom $($r.z): $($r.info)" }
$text = $md -join "`n"
Write-Output $text
if ($Out) { [IO.File]::WriteAllText((Join-Path $root $Out), $text + "`n") }
