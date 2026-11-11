# Builds ocgcore.dll + the EDOPro client (Win32) and copies them into .\run
# Usage:  .\build.ps1            (Debug build + copy)
#         .\build.ps1 -Release   (Release build + copy)
#         .\build.ps1 -Premake   (regenerate the VS solution first, e.g. after adding source files)
#
# The Team Play version comes from the TEAMPLAY_VERSION file. It is baked into the client
# (shown in the main menu) together with this build's update URL:
#   https://itsprofoak26-code.github.io/teamplay-updates/check/<version>.json
# premake re-runs automatically whenever the version changes.
param([switch]$Premake, [switch]$Release, [switch]$NoCopy)
$ErrorActionPreference = 'Stop'
$root    = $PSScriptRoot
$deps    = Join-Path $root 'deps'
$edopro  = Join-Path $root 'edopro'
$config  = if ($Release) { 'Release' } else { 'Debug' }
$version = (Get-Content (Join-Path $root 'TEAMPLAY_VERSION') -Raw).Trim()
$updateUrl = "https://itsprofoak26-code.github.io/teamplay-updates/check/$version.json"
$stamp   = Join-Path $edopro 'build\.teamplay-version'

$stale = -not (Test-Path $stamp) -or ((Get-Content $stamp -Raw).Trim() -ne $version)
if ($Premake -or $stale -or -not (Test-Path "$edopro\build\ygo.sln")) {
    $env:DXSDK_DIR = "$deps\dxsdk\"
    Push-Location $edopro
    & "$deps\premake\premake5.exe" vs2022 --vcpkg-root=..\deps\vcpkg --vcpkg-triplet=-windows-static `
        --bundled-font=NotoSansJP-Regular.otf --sound=miniaudio --no-joystick=true `
        "--teamplay-version=$version" "--update-url=$updateUrl"
    Pop-Location
    if ($LASTEXITCODE) { throw 'premake failed' }
    Set-Content -Path $stamp -Value $version -NoNewline
}

$msb = & "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe" -latest -find 'MSBuild\**\Bin\MSBuild.exe' | Select-Object -First 1
$v = "$deps\vcpkg\scripts\buildsystems\msbuild"
& $msb "$edopro\build\ygo.sln" -m "-p:Configuration=$config" -p:Platform=Win32 `
    "-p:ForceImportBeforeCppTargets=$v\vcpkg.props" "-p:ForceImportAfterCppTargets=$v\vcpkg.targets" `
    "-p:VcpkgRoot=$deps\vcpkg\" '-t:ocgcoreshared;ygoprodll' -verbosity:minimal -nologo
if ($LASTEXITCODE) { throw 'build failed' }

$bin = "$edopro\bin\$($config.ToLower())"
Write-Host "Built Team Play v$version ($config) in $bin"
if (-not $NoCopy) {
    Copy-Item "$bin\ygoprodll.exe", "$bin\ocgcore.dll" "$root\run\" -Force
    Write-Host "Copied ygoprodll.exe + ocgcore.dll to $root\run"
}
