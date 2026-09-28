# Runs every Moonrush test that works without the game:
#   1. launcher (Rust): curve, settings clamping, INI output, log parsing
#   2. game module (C++ settings.hpp, compiled with MSVC): same curve vectors + parsing the launcher's INI
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot
Push-Location "$root\launcher\src-tauri"; cargo test; $rust = $LASTEXITCODE; Pop-Location
$vs = & "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe" -latest -property installationPath
$out = Join-Path $env:TEMP 'moonrush-host-test'; New-Item -ItemType Directory -Force $out | Out-Null
cmd /c "`"$vs\VC\Auxiliary\Build\vcvars64.bat`" >nul && cl /nologo /std:c++20 /EHsc /W4 /Fe:`"$out\host_test.exe`" /Fo:`"$out\host_test.obj`" `"$PSScriptRoot\host_test.cpp`""
if ($LASTEXITCODE -ne 0) { Write-Host "C++ test failed to compile"; exit 1 }
& "$out\host_test.exe" $PSScriptRoot; $cpp = $LASTEXITCODE
if ($rust -ne 0 -or $cpp -ne 0) { Write-Host "FAILED (rust=$rust, cpp=$cpp)"; exit 1 }
Write-Host "All Moonrush tests passed."
