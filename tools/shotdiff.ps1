# Сколько пикселей различаются в прямоугольнике двух снимков (проверка анимации: цикл
# палитры, мигание). powershell -File tools\shotdiff.ps1 -A a.png -B b.png [-X -Y -W -H]
param([string]$A, [string]$B, [int]$X = 0, [int]$Y = 0, [int]$W = 320, [int]$H = 200)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
if (-not [System.IO.Path]::IsPathRooted($A)) { $A = Join-Path $root $A }
if (-not [System.IO.Path]::IsPathRooted($B)) { $B = Join-Path $root $B }
Add-Type -AssemblyName System.Drawing
$ba = [System.Drawing.Bitmap]::FromFile($A)
$bb = [System.Drawing.Bitmap]::FromFile($B)
$n = 0
for ($yy = $Y; $yy -lt $Y + $H; $yy++) {
	for ($xx = $X; $xx -lt $X + $W; $xx++) {
		if ($ba.GetPixel($xx, $yy).ToArgb() -ne $bb.GetPixel($xx, $yy).ToArgb()) { $n++ }
	}
}
$ba.Dispose(); $bb.Dispose()
Write-Output "shotdiff: $n of $($W * $H) pixels differ"
