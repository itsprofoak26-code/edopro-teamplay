# Compiles the headless team-play tests (32-bit, to match ocgcore.dll) and runs them against run/ocgcore.dll
#   teamplay_core_test.cpp    draws, hand limit, deck-out, card moves, effect scope, duelist choice
#   teamplay_choice_test.cpp  duelist choice for mill costs, Deck-top reveals and per-team discards
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$vs = & "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe" -latest -property installationPath
$out = Join-Path $PSScriptRoot 'bin'
New-Item -ItemType Directory -Force $out | Out-Null
$vcvars = Join-Path $vs 'VC\Auxiliary\Build\vcvars32.bat'
$inc = Join-Path $root 'ocgcore'
$dll = Join-Path $root 'run\ocgcore.dll'
$failed = 0
foreach ($name in 'teamplay_core_test', 'teamplay_choice_test') {
    $src = Join-Path $PSScriptRoot "$name.cpp"
    $exe = Join-Path $out "$name.exe"
    cmd /c "`"$vcvars`" >nul 2>nul && cl /nologo /std:c++17 /EHsc /W3 /I`"$inc`" `"$src`" /Fe`"$exe`" /Fo`"$out/`""
    if ($LASTEXITCODE) { throw "compile failed: $name" }
    Write-Host "== $name"
    & $exe $dll
    if ($LASTEXITCODE) { $failed++ }
}
exit $failed
