# Снимки глобуса на всех зумах при зафиксированном времени (сверка побайтно при переделках рендера).
#   powershell -File tools\globeshots.ps1 -Tag ref ; затем -Tag new и node tools\globeshots_cmp.js ref new
param([string]$Tag = 'new')
$root = Split-Path -Parent $PSScriptRoot
$src = [IO.File]::ReadAllText((Join-Path $root 'tools\globeshots.oxs.tmpl')).Replace('TAG', $Tag)
[IO.File]::WriteAllText((Join-Path $root "tmp\asmshots_$Tag.oxs"), $src)
Push-Location $root
& powershell -File (Join-Path $root 'tools\run.ps1') -Script "tmp\asmshots_$Tag.oxs" -Headless 2>&1 | Select-String -Pattern 'screenshot|PANIC|exit code' | ForEach-Object { $_.Line }
Pop-Location
