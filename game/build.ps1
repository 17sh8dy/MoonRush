# Build the Moonrush game module with devkitPro (installed at D:\devkitPro).
# Output: deploy\subsdk8 and deploy\main.npdm
$ErrorActionPreference = 'Stop'
$env:MSYSTEM = 'MSYS'
$env:MOONRUSH_PYTHON = (Get-Command python).Source
$script = (Join-Path $PSScriptRoot 'build.sh').Replace('\', '/')
& D:\devkitPro\msys2\usr\bin\bash.exe -l $script @args
exit $LASTEXITCODE
