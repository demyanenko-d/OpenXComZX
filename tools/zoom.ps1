# Увеличенный фрагмент снимка (без сглаживания) — рассмотреть мелкие детали окна.
#   powershell -File tools\zoom.ps1 -In tmp\shots\ui\54_dogfight.png -X 80 -Y 52 -W 160 -H 96 [-Scale 4] [-Out файл]
param([string]$In, [int]$X = 0, [int]$Y = 0, [int]$W = 320, [int]$H = 200, [int]$Scale = 4, [string]$Out = '')
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
if (-not [System.IO.Path]::IsPathRooted($In)) { $In = Join-Path $root $In }
if (-not $Out) { $Out = [System.IO.Path]::ChangeExtension($In, $null).TrimEnd('.') + "_zoom.png" }
elseif (-not [System.IO.Path]::IsPathRooted($Out)) { $Out = Join-Path $root $Out }
Add-Type -AssemblyName System.Drawing
$src = [System.Drawing.Bitmap]::FromFile($In)
$bmp = New-Object System.Drawing.Bitmap ($W * $Scale), ($H * $Scale)
$g = [System.Drawing.Graphics]::FromImage($bmp)
$g.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::NearestNeighbor
$g.PixelOffsetMode = [System.Drawing.Drawing2D.PixelOffsetMode]::Half
$g.DrawImage($src, (New-Object System.Drawing.Rectangle 0, 0, ($W * $Scale), ($H * $Scale)), (New-Object System.Drawing.Rectangle $X, $Y, $W, $H), [System.Drawing.GraphicsUnit]::Pixel)
$g.Dispose()
$src.Dispose()
$bmp.Save($Out, [System.Drawing.Imaging.ImageFormat]::Png)
$bmp.Dispose()
Write-Output "zoom: $Out"
