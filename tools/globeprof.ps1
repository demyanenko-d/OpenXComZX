# Раскладка времени рендера глобуса по зумам (профилировщик эмулятора, 07 §6).
# Для каждого зума 0..5: новая игра -> база -> геоскейп -> зум -> пробный поворот -> профиль
# одного поворота. Окно профиля — «нажатие -> отметка»: down / wait 1 / up / waitmark 32
# (к waitmark обработка уже идёт, окно кончается на отметке конца обработки). Прежнее окно
# key + wait 8 было фиксированными 14 кадрами, рендер в нём — 4–13 (perf/pipeline.md §0).
# Функции сводятся в группы по регуляркам (первая подошедшая), вывод — таблица кадров
# (71 680 тактов профиля = кадр). Сборка — текущая (tmp\build\oxz.spg).
# Строки вывода — латиницей (PowerShell 5.1 читает скрипт без BOM как ANSI).
#   powershell -File tools\globeprof.ps1 [-Zooms 0,1,2] [-Hour 18] [-Out tmp\globeprof.md] [-From dir]
# -Hour — час игры перед замером (терминатор в окне: 18; по умолчанию время не трогается).
# -From — не запускать эмулятор, а свести профили dir\prof_z<зум>.txt прошлого замера.
param([int[]]$Zooms = @(0, 1, 2, 3, 4, 5), [int]$Hour = -1, [string]$Out = '', [string]$From = '')
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
# Порядок важен: функция попадает в первую группу, чья регулярка подошла
$groups = [ordered]@{
	'raster: GVE2 pieces, runs -> DMA' = '^(eg_|_gl_rows_edg|_gl_rows_vc|_gl_rows_cap|vc_row|cp_row|rp_|r_vc|_gl_rows_pre|_gl_rows$|_gl_edges|rw_|em_run_p|edge1|e_rows|e_fast|hclip|limb_l|lerp|xclip|div32|eslope|bord_ev|e_store|mul8e|mulu16|ael_|ai_cmp|fx_)'
	'shadow (bank 25)'       = '^(_sh_|build$|iv_out|lvl_of|add32|globe_sh\$|em_run_s|blk_eval|set_num|row_rng|_globe_shadow|_globe_sunlon|_globe_rows_pl)'
	'marks'                  = '^(globe_det\$mk_|_globe_marks|_det_marks|_det_unmark|_globe_xy)'
	'details: projection, tables' = '^(_gl_project|vidx|globe\$project|_gl_ktab|kt_|_gl_ztab|globe\$tables)'
	'details: lines, labels' = '^(_memcmp|calc_th|_font_height|layout|copy_s|make_lut|map_font|add_line|dma_idle|tp_lut|tr_done|detail|dt_|_globe_det|_det_|det_|dg_|td_|ly_|char_adv|glyph|_glyph|_text_|text\$|_gl_cull|tables|kmul|kput|proj_v|cu_|line_|_str_copy|draw_glyph|lt_|ta_|globe_det\$)'
	'view from SD'           = '^(\?3B|_sd_|_far_write|fat\$|_fat_|dma_sec|_gview_|gv_|div16|globe_view\$)'
	'C render, math, far mem'   = '^(umul16|mul8$|udiv|ud16|ud32|wpass8|bpass16|bp16_big|neg32|ml_|sl_go|render$|r_shadow|_globe_draw|_globe_sin|scr_geo\$rotate|globe\$render|globe\$rows_init|globe\$bg_restore|___mul|___div|___mod|__mul|__div|__mod|_far_read|_far_fill|_far_copy|_far_byte|_memset|_memcpy)'
	'blit: wait for frame'   = '^(blit|globe\$blit)$'
	'DMA copy wait'          = '^(dma_wait|globe\$dma_wait)$'
	'UI idle (wait_frames)'  = '^ui\$wait_frames$'
	'UI: clock, input, ISR'  = '^(ui\$|_gfx_|gfx\$|gfx_|_ui_|_scr_|_geo_|_input|_cursor|_set_bank|set_bank|_get_bank|_globe_snap|scr_geo\$draw_globe|music\$|_res_game|r_bg|r_rows|___sdcc|_game|_end_event|_mus|mus_|_pg_|far_dma|dma_blk|dma_go|_res_find|_dbg_|input_isr|isr|_isr)'
}
$rows = @()
foreach ($z in $Zooms) {
	$lines = @('waitmark 1 600', 'poke _alien_off 1', 'pokew _cursor_x 110', 'pokew _cursor_y 100', 'click L', 'waitmark 2 300',
		'pokew _cursor_x 119', 'pokew _cursor_y 172', 'click L', 'waitmark 43 3000', 'pokew _cursor_x 120', 'pokew _cursor_y 110',
		'click L', 'waitmark 42 3000', 'type prof', 'key ENTER', 'waitmark 32 3000', 'poke _gl_dbg 1')
	for ($i = 0; $i -lt $z; $i++) { $lines += @('key SS+K', 'wait 8', 'waitmark 32 3000') }
	if ($Hour -ge 0) { $lines += @("poke 06:0038 $Hour", 'wait 300') }   # ST->hour; смена эпохи солнца — перерисовка
	# Время стоит (ST->months = -1, как до первой базы; gtime.c game_advance): иначе каждые ~34
	# кадра на «5 Secs» солнце сдвигается на эпоху, тень перерисовывается, и нажатие в окне ждёт
	# её, а waitmark 32 срабатывает по старой отметке — окно закрывается до поворота
	$lines += @('pokew 06:0030 0xFFFF', 'wait 20', 'key RIGHT', 'wait 8', 'waitmark 32 3000', 'wait 30')   # пробный поворот (вправо: обратно — вид мог попасть в кэш отрезков)
	$lines += @('profile on', 'down RIGHT', 'wait 1', 'up RIGHT', 'waitmark 32 3000', 'profile off 200 ops', 'exit 0')
	if ($From) { $outp = [IO.File]::ReadAllText((Join-Path $root "$From\prof_z$z.txt")) }
	else {
		$scr = "tmp\globeprof_z$z.oxs"                 # run.ps1 ждёт путь от корня проекта
		[IO.File]::WriteAllText((Join-Path $root $scr), ($lines -join "`n") + "`n")
		Push-Location $root
		$outp = & powershell -File (Join-Path $root 'tools\run.ps1') -Script $scr -Headless 2>&1 | Out-String
		Pop-Location
		[IO.File]::WriteAllText((Join-Path $root "tmp\prof_z$z.txt"), $outp)   # для tools\globeprof_sum.js
	}
	$tot = 0; $sum = @{}; foreach ($g in $groups.Keys) { $sum[$g] = 0 }; $other = 0; $oth = @{}
	foreach ($l in ($outp -split "`n")) {
		if ($l -match 'OXZ: profile (\d+) T') { $tot = [double]$Matches[1] }
		elseif ($l -match 'OXZ: prof\s+[\d.]+%\s+(\d+)\s+(\S+)') {
			$t = [double]$Matches[1]; $n = $Matches[2]; $hit = $false
			foreach ($g in $groups.Keys) { if ($n -match $groups[$g]) { $sum[$g] += $t; $hit = $true; break } }
			if (-not $hit) { $other += $t; $oth[$n] = $t }
		}
	}
	$g0 = ($outp -split "`n" | Where-Object { $_ -match '^globe: ' } | Select-Object -Last 1)
	$top = ($oth.GetEnumerator() | Sort-Object Value -Descending | Select-Object -First 6 | ForEach-Object { $_.Key }) -join ' '
	$rows += [pscustomobject]@{ z = $z; total = $tot; sum = $sum; other = $other; info = "$g0".Trim(); top = $top }
}
$f = { param($t) '{0:N2}' -f ($t / 71680) }
$hdr = '| group |' + (($rows | ForEach-Object { " zoom $($_.z) |" }) -join '')
$sep = '|---|' + (($rows | ForEach-Object { '---:|' }) -join '')
$md = @($hdr, $sep)
foreach ($g in $groups.Keys) { $md += "| $g |" + (($rows | ForEach-Object { " $(& $f $_.sum[$g]) |" }) -join '') }
$md += '| other |' + (($rows | ForEach-Object { " $(& $f $_.other) |" }) -join '')
$md += '| **TOTAL (press -> mark), frames** |' + (($rows | ForEach-Object { " **$(& $f $_.total)** |" }) -join '')
$md += '| work (TOTAL - idle - waits) |' + (($rows | ForEach-Object {
	" $(& $f ($_.total - $_.sum['UI idle (wait_frames)'] - $_.sum['blit: wait for frame'] - $_.sum['DMA copy wait'])) |" }) -join '')
$md += ''
foreach ($r in $rows) { $md += "- zoom $($r.z): $($r.info)"; if ($r.top) { $md += "  other: $($r.top)" } }
$text = $md -join "`n"
Write-Output $text
if ($Out) { [IO.File]::WriteAllText((Join-Path $root $Out), $text + "`n") }
