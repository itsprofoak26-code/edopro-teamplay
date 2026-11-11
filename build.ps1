# Builds ocgcore.dll + the EDOPro client (Debug, Win32) and copies them into .\run
# Usage:  .\build.ps1            (build + copy)
#         .\build.ps1 -Premake   (regenerate the VS solution first, e.g. after adding source files)
#         .\build.ps1 -Release
param([switch]$Premake, [switch]$Release)
$ErrorActionPreference = 'Stop'
$root   = $PSScriptRoot
$deps   = Join-Path $root 'deps'
$edopro = Join-Path $root 'edopro'
$config = if ($Release) { 'Release' } else { 'Debug' }

if ($Premake -or -not (Test-Path "$edopro\build\ygo.sln")) {
    $env:DXSDK_DIR = "$deps\dxsdk\"
    Push-Location $edopro
    & "$deps\premake\premake5.exe" vs2022 --vcpkg-root=..\deps\vcpkg --vcpkg-triplet=-windows-static `
        --bundled-font=NotoSansJP-Regular.otf --sound=miniaudio --no-joystick=true
    Pop-Location
    if ($LASTEXITCODE) { throw 'premake failed' }
}

$msb = & "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe" -latest -find 'MSBuild\**\Bin\MSBuild.exe' | Select-Object -First 1
$v = "$deps\vcpkg\scripts\buildsystems\msbuild"
& $msb "$edopro\build\ygo.sln" -m "-p:Configuration=$config" -p:Platform=Win32 `
    "-p:ForceImportBeforeCppTargets=$v\vcpkg.props" "-p:ForceImportAfterCppTargets=$v\vcpkg.targets" `
    "-p:VcpkgRoot=$deps\vcpkg\" '-t:ocgcoreshared;ygoprodll' -verbosity:minimal -nologo
if ($LASTEXITCODE) { throw 'build failed' }

$bin = "$edopro\bin\$($config.ToLower())"
Copy-Item "$bin\ygoprodll.exe", "$bin\ocgcore.dll" "$root\run\" -Force
Write-Host "Copied ygoprodll.exe + ocgcore.dll to $root\run"
