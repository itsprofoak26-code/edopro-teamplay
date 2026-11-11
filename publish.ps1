# Publishes a new EDOPro Team Play version for friends.
#   .\publish.ps1 -Notes "What changed"
# Steps:
#   1. bumps TEAMPLAY_VERSION (unless -Rebuild: republish the current number, e.g. the first release)
#   2. builds the client with that version and its update URL baked in (Release; -DebugBuild to use Debug)
#   3. packages update-v<N>.zip and EDOPro-TeamPlay-v<N>.zip (tools/package.py)
#   4. uploads both as GitHub release v<N> of itsprofoak26-code/teamplay-updates
#   5. pushes the updated check files to that repo's GitHub Pages site, so older clients
#      offer the update next time they start
#   6. commits the version bump in this repo
param([string]$Notes = "", [switch]$Rebuild, [switch]$DebugBuild)
$ErrorActionPreference = 'Stop'
$root  = $PSScriptRoot
$gh    = 'C:\Program Files\GitHub CLI\gh.exe'
$repo  = 'itsprofoak26-code/teamplay-updates'
$site  = Join-Path $root 'updates'           # checkout of the Pages repo (ignored by this repo)
$out   = Join-Path $root 'dist'

$verFile = Join-Path $root 'TEAMPLAY_VERSION'
$version = [int](Get-Content $verFile -Raw).Trim()
if (-not $Rebuild) {
    $version++
    Set-Content -Path $verFile -Value $version -NoNewline
}
Write-Host "== Publishing Team Play v$version"

# 2. build
$cfg = if ($DebugBuild) { 'debug' } else { 'release' }
if ($DebugBuild) { & "$root\build.ps1" -NoCopy } else { & "$root\build.ps1" -Release -NoCopy }

# 3. package
if (-not (Test-Path $site)) { & $gh repo clone $repo $site }
git -C $site pull --ff-only
Remove-Item -Recurse -Force $out -ErrorAction SilentlyContinue
python -I "$root\tools\package.py" --version $version --bin "$root\edopro\bin\$cfg" --run "$root\run" `
    --cardscripts "$root\CardScripts" --out $out --site $site --notes $Notes
if ($LASTEXITCODE) { throw 'packaging failed' }

# 4. release assets
$title = "Team Play v$version"
& $gh release create "v$version" --repo $repo --title $title --notes ($(if ($Notes) { $Notes } else { $title })) `
    "$out\update-v$version.zip" "$out\EDOPro-TeamPlay-v$version.zip"
if ($LASTEXITCODE) { throw 'release upload failed' }

# 5. Pages site
git -C $site add -A
git -C $site commit -m "Team Play v$version"
git -C $site push
if ($LASTEXITCODE) { throw 'site push failed' }

# 6. record the version in this repo
git -C $root add TEAMPLAY_VERSION
git -C $root commit -m "Publish Team Play v$version" 2>$null
Write-Host "== Published v$version"
Write-Host "   Download page: https://itsprofoak26-code.github.io/teamplay-updates/"
