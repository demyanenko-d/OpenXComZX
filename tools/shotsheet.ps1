# Листы снимков: PNG из каталога по 4 на лист (2x2, с подписями) — быстро
# просмотреть все окна после автотеста.
#   powershell -File tools\shotsheet.ps1 [-Dir tmp\shots\ui] [-Out tmp\shots\sheets]
param([string]$Dir = 'tmp\shots\ui', [string]$Out = 'tmp\shots\sheets')
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
if (-not [System.IO.Path]::IsPathRooted($Dir)) { $Dir = Join-Path $root $Dir }
if (-not [System.IO.Path]::IsPathRooted($Out)) { $Out = Join-Path $root $Out }
Add-Type -AssemblyName System.Drawing
$files = Get-ChildItem $Dir -Filter *.png | Sort-Object Name
New-Item -ItemType Directory -Force $Out | Out-Null
$font = New-Object System.Drawing.Font 'Consolas', 9
for ($s = 0; $s -lt $files.Count; $s += 4) {
	$bmp = New-Object System.Drawing.Bitmap 660, 430
	$g = [System.Drawing.Graphics]::FromImage($bmp)
	$g.Clear([System.Drawing.Color]::DimGray)
	for ($k = 0; $k -lt 4 -and $s + $k -lt $files.Count; $k++) {
		$img = [System.Drawing.Bitmap]::FromFile($files[$s + $k].FullName)
		$x = 5 + ($k % 2) * 330
		$y = 14 + [math]::Floor($k / 2) * 214
		$g.DrawImage($img, $x, $y, 320, 200)
		$g.DrawString($files[$s + $k].Name, $font, [System.Drawing.Brushes]::White, $x, $y - 13)
		$img.Dispose()
	}
	$g.Dispose()
	$bmp.Save((Join-Path $Out ('sheet_{0:D2}.png' -f ($s / 4))), [System.Drawing.Imaging.ImageFormat]::Png)
	$bmp.Dispose()
}
Write-Output "shotsheet: $($files.Count) shots -> $Out"
