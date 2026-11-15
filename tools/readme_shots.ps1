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
    @('showcase', 'showcase/02_globe_z0', 'geo_globe'),
    @('dogfight', 'dogfight/08_fire', 'geo_dogfight'),
    # базы
    @('showcase', 'showcase/06_base', 'base_view'),
    @('lab', 'lab/04_research_info_alloc', 'base_research'),
    # экипаж
    @('campaign', 'campaign/11_soldier_info', 'crew_soldier'),
    @('inventory', 'inv/04_loaded', 'crew_inventory'),
    # наземный бой: высадка, разведка, действия, раненые и пси, конец миссии
    @('bat_geo', 'bat_geo/00_briefing', 'bat_briefing'),
    @('bat_render', 'bat/00_map0', 'bat_landing'),
    @('bat_fog', 'bat_fog/01_walked', 'bat_fog'),
    @('bat_fire', 'bat_fire/01_aim', 'bat_aim'),
    @('bat_menu', 'bat_menu/01_menu', 'bat_menu'),
    @('bat_gren', 'bat_gren/1_timer', 'bat_grenade'),
    @('bat_medikit', 'bat_medikit/1_medikit', 'bat_medikit'),
    @('bat_mcamp', 'bat_mcamp/01_selected', 'bat_mindcontrol'),
    @('bat_abort', 'bat_abort/00_window', 'bat_abort'),
    @('bat_deb', 'bat_deb/02_debriefing', 'bat_debriefing'),
    # уфопедия и отчёты
    @('showcase', 'showcase/08_article', 'info_article'),
    @('campaign', 'campaign/18_monthly_report', 'info_report')
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
