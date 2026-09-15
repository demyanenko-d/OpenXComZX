# Запуск dotnet с каталогами кэша/настроек внутри проекта (правило: писать
# только в рабочее дерево). Все аргументы передаются dotnet как есть:
#   powershell -File tools\dotnet.ps1 build tools\OxzConv\OxzConv.csproj
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$t = Join-Path $root 'tmp\dotnet'
foreach ($d in 'home', 'appdata', 'localappdata', 'temp', 'nuget\packages', 'nuget\http', 'nuget\plugins') {
    New-Item -ItemType Directory -Force (Join-Path $t $d) | Out-Null
}
$env:DOTNET_CLI_HOME = Join-Path $t 'home'
$env:APPDATA = Join-Path $t 'appdata'
$env:LOCALAPPDATA = Join-Path $t 'localappdata'
$env:TEMP = Join-Path $t 'temp'
$env:TMP = Join-Path $t 'temp'
$env:NUGET_PACKAGES = Join-Path $t 'nuget\packages'
$env:NUGET_HTTP_CACHE_PATH = Join-Path $t 'nuget\http'
$env:NUGET_PLUGINS_CACHE_PATH = Join-Path $t 'nuget\plugins'
$env:DOTNET_CLI_TELEMETRY_OPTOUT = '1'
$env:DOTNET_NOLOGO = '1'
$env:DOTNET_SKIP_FIRST_TIME_EXPERIENCE = '1'
$env:DOTNET_ADD_GLOBAL_TOOLS_TO_PATH = '0'
$env:MSBUILDDISABLENODEREUSE = '1'
& dotnet @args
exit $LASTEXITCODE
