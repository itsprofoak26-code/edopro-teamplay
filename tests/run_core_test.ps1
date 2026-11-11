# Compiles tests/teamplay_core_test.cpp (32-bit, to match ocgcore.dll) and runs it against run/ocgcore.dll
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$vs = & "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe" -latest -property installationPath
$out = Join-Path $PSScriptRoot 'bin'
New-Item -ItemType Directory -Force $out | Out-Null
$vcvars = Join-Path $vs 'VC\Auxiliary\Build\vcvars32.bat'
$src = Join-Path $PSScriptRoot 'teamplay_core_test.cpp'
$exe = Join-Path $out 'teamplay_core_test.exe'
$inc = Join-Path $root 'ocgcore'
cmd /c "`"$vcvars`" >nul 2>nul && cl /nologo /std:c++17 /EHsc /W3 /I`"$inc`" `"$src`" /Fe`"$exe`" /Fo`"$out/`""
if ($LASTEXITCODE) { throw 'compile failed' }
& $exe (Join-Path $root 'run\ocgcore.dll')
exit $LASTEXITCODE
