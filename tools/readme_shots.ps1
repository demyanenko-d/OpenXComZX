# Снимки для README: сценарии tests\*.oxs с курсором, скрытым «poke _cursor_off 1» (он только
# гасит спрайт — поведение то же), кадры 320x200 -> 640x400 без сглаживания в project_docs\scr.
#   powershell -File tools\readme_shots.ps1             # TFTD: сборка, сценарии, снимки
#   powershell -File tools\readme_shots.ps1 -Game UFO   # UFO (имена с ufo_); потом пересобрать TFTD
#   ... -NoBuild                                        # сборка уже та
# Какие кадры берутся — таблица $pick: сценарий, снимок (путь под tmp\shots), имя в project_docs\scr.
# Сценарии идут в копиях (tmp\readme\<игра>\*.oxs), снимки — в tmp\readme\<игра>\.
# Сообщения — латиницей: PS 5.1 читает этот файл как ANSI.
param([string]$Game = 'TFTD', [switch]$NoBuild)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$Game = $Game.ToUpper()
$pick = @{
    'TFTD' = @(
        @('showcase', 'showcase/02_globe_z0', 'geo_globe'),
        @('showcase', 'showcase/05_globe_z3', 'geo_cities'),
        @('ui_geoscape', 'ui/11_ufo_detected', 'geo_ufo'),
        @('dogfight', 'dogfight/08_fire', 'geo_dogfight'),
        @('showcase', 'showcase/06_base', 'base_view'),
        @('campaign', 'campaign/06a_place_frame', 'base_build'),
        @('lab', 'lab/04_research_info_alloc', 'base_research'),
        @('lab', 'lab/16_manuf_info_alloc', 'base_manufacture'),
        @('campaign', 'campaign/11_soldier_info', 'crew_soldier'),
        @('craft', 'craft/05_equipment', 'crew_equipment'),
        @('inventory', 'inv/04_loaded', 'crew_inventory'),
        @('bat_render', 'bat/00_map0', 'crew_battle'),
        @('showcase', 'showcase/08_article', 'info_craft'),
        @('ui_geoscape', 'ui/22_ufop_article', 'info_weapon'),
        @('campaign', 'campaign/18_monthly_report', 'info_report'),
        @('campaign', 'campaign/18b_graphs_income', 'info_graphs')
    )
    'UFO' = @(
        @('showcase', 'showcase/02_globe_z0', 'ufo_globe'),
        @('showcase', 'showcase/06_base', 'ufo_base'),
        @('showcase', 'showcase/08_article', 'ufo_article'),
        @('inventory', 'inv/01', 'ufo_inventory')
    )
}
if (-not $pick.ContainsKey($Game)) { throw "unknown game $Game" }
$list = $pick[$Game]
if (-not $NoBuild) {
    & powershell -File (Join-Path $root 'tools\build.ps1') -Game $Game | Out-Null
    if ($LASTEXITCODE) { throw "build failed ($LASTEXITCODE)" }
}
$work = Join-Path $root ("tmp\readme\" + $Game.ToLower())
$rel = 'tmp/readme/' + $Game.ToLower() + '/'
New-Item -ItemType Directory -Force $work | Out-Null
$utf8 = New-Object System.Text.UTF8Encoding $false
foreach ($t in ($list | ForEach-Object { $_[0] } | Select-Object -Unique)) {
    $out = New-Object System.Collections.Generic.List[string]
    foreach ($l in [System.IO.File]::ReadAllLines((Join-Path $root "tests\$t.oxs"), $utf8)) {
        if ($l -match '^shot tmp/shots/(.+)$') {
            $l = 'shot ' + $rel + $Matches[1]
            New-Item -ItemType Directory -Force (Split-Path (Join-Path $work $Matches[1])) | Out-Null
        }
        $out.Add($l)
        if ($l -match '^waitmark 1 ') { $out.Add('poke _cursor_off 1') }   # и после возврата в меню
    }
    [System.IO.File]::WriteAllLines((Join-Path $work "$t.oxs"), $out, $utf8)
    & powershell -File (Join-Path $root 'tools\run.ps1') -Script ($rel + "$t.oxs") -Headless | Out-Null   # путь — от корня
    if ($LASTEXITCODE) { throw "scenario $t failed ($LASTEXITCODE)" }
    Write-Output "$t ok"
}
Add-Type -AssemblyName System.Drawing
$dst = Join-Path $root 'project_docs\scr'
New-Item -ItemType Directory -Force $dst | Out-Null
foreach ($p in $list) {
    $src = [System.Drawing.Bitmap]::FromFile((Join-Path $work ($p[1] + '.png')))
    $bmp = New-Object System.Drawing.Bitmap 640, 400
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::NearestNeighbor
    $g.PixelOffsetMode = [System.Drawing.Drawing2D.PixelOffsetMode]::Half
    $g.DrawImage($src, (New-Object System.Drawing.Rectangle 0, 0, 640, 400), (New-Object System.Drawing.Rectangle 0, 0, 320, 200), [System.Drawing.GraphicsUnit]::Pixel)
    $g.Dispose(); $src.Dispose()
    $bmp.Save((Join-Path $dst ($p[2] + '.png')), [System.Drawing.Imaging.ImageFormat]::Png)
    $bmp.Dispose()
    Write-Output ("{0} <- {1}" -f $p[2], $p[1])
}
