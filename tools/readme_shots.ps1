# Снимки для README: сценарии tests\*.oxs с курсором, скрытым «poke _cursor_off 1» (он только
# гасит спрайт — поведение то же), кадры 320x200 -> 640x400 без сглаживания в project_docs\scr.
# В README кадры парами: слева UFO Defense (ufo_<имя>), справа Terror from the Deep (tftd_<имя>) —
# один и тот же сценарий и кадр на сборке каждой игры.
#   powershell -File tools\readme_shots.ps1 -Game UFO   # UFO: сборка, сценарии, снимки
#   powershell -File tools\readme_shots.ps1             # TFTD (и вернуть сборку TFTD — после UFO)
#   ... -NoBuild                                        # сборка уже та
# Какие кадры берутся — таблица $pick: сценарий, снимок (путь под tmp\shots), имя в project_docs\scr.
# Сценарии идут в копиях (tmp\readme\<игра>\*.oxs), снимки — в tmp\readme\<игра>\. Проверки expect
# в сценариях — числа TFTD: для UFO они выбрасываются, а сценарий может кончиться с ошибкой после
# нужных кадров — тогда важно лишь, что кадры есть.
# Сообщения — латиницей: PS 5.1 читает этот файл как ANSI.
param([string]$Game = 'TFTD', [switch]$NoBuild)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$Game = $Game.ToUpper()
if ($Game -ne 'TFTD' -and $Game -ne 'UFO') { throw "unknown game $Game" }
$list = @(
    # геоскейп
    @('showcase', 'showcase/01_main', 'geo_menu'),
    @('ui_geoscape', 'ui/02_newgame', 'geo_newgame'),
    @('showcase', 'showcase/02_globe_z0', 'geo_globe'),
    @('showcase', 'showcase/05_globe_z3', 'geo_cities'),
    @('campaign', 'campaign/01_first_base_site', 'geo_first_base'),
    @('campaign', 'campaign/16_construction_done', 'geo_message'),
    @('ui_geoscape', 'ui/11_ufo_detected', 'geo_ufo'),
    @('ui_geoscape', 'ui/07_intercept', 'geo_intercept'),
    @('dogfight', 'dogfight/08_fire', 'geo_dogfight'),
    @('basedefense', 'basedefense/01_defense', 'geo_basedefense'),
    # базы
    @('showcase', 'showcase/06_base', 'base_view'),
    @('campaign', 'campaign/14_items_arriving', 'base_arriving'),
    @('campaign', 'campaign/05_build_list', 'base_build_list'),
    @('campaign', 'campaign/06a_place_frame', 'base_build'),
    @('lab', 'lab/06_research_list', 'base_research_list'),
    @('lab', 'lab/04_research_info_alloc', 'base_research'),
    @('lab', 'lab/11_new_manuf', 'base_manuf_list'),
    @('lab', 'lab/14_manuf_start', 'base_manufacture'),
    @('campaign', 'campaign/08_purchase', 'base_purchase'),
    @('transfer', 'transfer/03_transfer_items', 'base_transfer'),
    # экипаж
    @('campaign', 'campaign/10_soldiers', 'crew_list'),
    @('campaign', 'campaign/11_soldier_info', 'crew_soldier'),
    @('craft', 'craft/02_craft_info', 'crew_craft'),
    @('craft', 'craft/12_weapons', 'crew_craft_weapons'),
    @('craft', 'craft/05_equipment', 'crew_equipment'),
    @('inventory', 'inv/04_loaded', 'crew_inventory'),
    # наземный бой — по ходу миссии
    @('bat_geo', 'bat_geo/00_briefing', 'bat_briefing'),
    @('bat_render', 'bat/00_map0', 'bat_landing'),
    @('bat_render', 'bat/01_level', 'bat_craft'),
    @('bat_doors', 'doors/04_open_west', 'bat_door'),
    @('bat_fog', 'bat_fog/01_walked', 'bat_fog'),
    @('bat_fire', 'bat_fire/01_aim', 'bat_aim'),
    @('bat_menu', 'bat_menu/01_menu', 'bat_menu'),
    @('bat_melee', 'bat_melee/00_menu', 'bat_melee'),
    @('bat_gren', 'bat_gren/1_timer', 'bat_grenade'),
    @('bat_gren', 'bat_gren/4_target', 'bat_throw'),
    @('bat_info', 'bat_info/00_stats', 'bat_stats'),
    @('bat_inv', 'bat_inv/0_open', 'bat_inventory'),
    @('bat_medikit', 'bat_medikit/1_medikit', 'bat_medikit'),
    @('bat_mcamp', 'bat_mcamp/01_selected', 'bat_mindcontrol'),
    @('bat_ai', 'bat_ai/01_aliens', 'bat_turn'),
    @('bat_pause', 'bat_pause/00_pause', 'bat_pause'),
    @('bat_warn', 'bat_warn/00_no_tu', 'bat_warn'),
    @('bat_kneel', 'kneel/01_kneel', 'bat_kneel'),
    @('bat_abort', 'bat_abort/00_window', 'bat_abort'),
    @('bat_deb', 'bat_deb/02_debriefing', 'bat_debriefing'),
    # уфопедия и отчёты
    @('showcase', 'showcase/07_ufopaedia', 'info_ufopaedia'),
    @('showcase', 'showcase/08_article', 'info_article'),
    @('ui_geoscape', 'ui/22_ufop_article', 'info_weapon'),
    @('ufopaedia', 'ufopaedia/s3_0', 'info_weapon2'),
    @('ufopaedia', 'ufopaedia/s5_0', 'info_alien'),
    @('ufopaedia', 'ufopaedia/s7_0', 'info_tech'),
    @('campaign', 'campaign/18_monthly_report', 'info_report'),
    @('campaign', 'campaign/18c_funding', 'info_funding'),
    @('ui_geoscape', 'ui/16_graphs', 'info_graphs'),
    @('campaign', 'campaign/18a_graphs_finance', 'info_finance')
)
if (-not $NoBuild) {
    & powershell -File (Join-Path $root 'tools\build.ps1') -Game $Game | Out-Null
    if ($LASTEXITCODE) { throw "build failed ($LASTEXITCODE)" }
}
$work = Join-Path $root ("tmp\readme\" + $Game.ToLower())
$rel = 'tmp/readme/' + $Game.ToLower() + '/'
if (Test-Path $work) { Remove-Item -Recurse -Force $work }   # прошлые кадры не должны подменить недоснятые
New-Item -ItemType Directory -Force $work | Out-Null
$utf8 = New-Object System.Text.UTF8Encoding $false
foreach ($t in ($list | ForEach-Object { $_[0] } | Select-Object -Unique)) {
    $out = New-Object System.Collections.Generic.List[string]
    foreach ($l in [System.IO.File]::ReadAllLines((Join-Path $root "tests\$t.oxs"), $utf8)) {
        if ($Game -eq 'UFO' -and $l -match '^expect') { continue }
        if ($l -match '^shot tmp/shots/(.+)$') {
            $l = 'shot ' + $rel + $Matches[1]
            New-Item -ItemType Directory -Force (Split-Path (Join-Path $work $Matches[1])) | Out-Null
        }
        $out.Add($l)
        if ($l -match '^waitmark 1 ') { $out.Add('poke _cursor_off 1') }   # и после возврата в меню
    }
    [System.IO.File]::WriteAllLines((Join-Path $work "$t.oxs"), $out, $utf8)
    & powershell -File (Join-Path $root 'tools\run.ps1') -Script ($rel + "$t.oxs") -Headless | Out-Null   # путь — от корня
    if ($LASTEXITCODE -and $Game -ne 'UFO') { throw "scenario $t failed ($LASTEXITCODE)" }
    Write-Output "$t = $LASTEXITCODE"
}
Add-Type -AssemblyName System.Drawing
$dst = Join-Path $root 'project_docs\scr'
New-Item -ItemType Directory -Force $dst | Out-Null
foreach ($p in $list) {
    $f = Join-Path $work ($p[1] + '.png')
    if (-not (Test-Path $f)) { throw "no shot $($p[1]) for $Game" }
    $src = [System.Drawing.Bitmap]::FromFile($f)
    $bmp = New-Object System.Drawing.Bitmap 640, 400
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::NearestNeighbor
    $g.PixelOffsetMode = [System.Drawing.Drawing2D.PixelOffsetMode]::Half
    $g.DrawImage($src, (New-Object System.Drawing.Rectangle 0, 0, 640, 400), (New-Object System.Drawing.Rectangle 0, 0, 320, 200), [System.Drawing.GraphicsUnit]::Pixel)
    $g.Dispose(); $src.Dispose()
    $name = $Game.ToLower() + '_' + $p[2]
    $bmp.Save((Join-Path $dst ($name + '.png')), [System.Drawing.Imaging.ImageFormat]::Png)
    $bmp.Dispose()
    Write-Output ("{0} <- {1}" -f $name, $p[1])
}
