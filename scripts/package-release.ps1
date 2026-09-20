# SPDX-License-Identifier: MIT
# Copyright (c) 2026 itsloopyo
#
# One ZIP: the installer. There is deliberately no -nexus.zip stage and no
# NEXUS_MODS.md in this repo.
#
# The payload has to land next to the shipping exe, in
# Subliminal\Binaries\Win64\, because that is the only directory Ultimate ASI
# Loader scans. A mod manager cannot put it there: Vortex deploys into the one
# subtree its per-game extension names in queryModPath, and nothing but a
# registered mod type escapes it. A Nexus archive of this mod would install
# without error, deploy into the wrong folder, load nothing, and report success.
# So this mod is installer-only. Do not add a Nexus stage back.

$ErrorActionPreference = 'Stop'

$projectDir = Split-Path -Parent $PSScriptRoot

Import-Module (Join-Path $projectDir "cameraunlock-core\powershell\ReleaseWorkflow.psm1") -Force
$buildDir = Join-Path $projectDir 'build/Release'
$releaseDir = Join-Path $projectDir 'release'

$asi = Join-Path $buildDir 'SubliminalHeadTracking.asi'
if (-not (Test-Path $asi)) {
    throw "Built .asi not found at $asi. Run 'pixi run build' first."
}

# Anchored on the project() call, the same pattern release-nightly.ps1 uses. A
# bare 'VERSION x.y.z' matches the first such line anywhere in the file, so a
# three-component cmake_minimum_required or a pinned FetchContent tag would
# stamp the ZIP and the launcher manifest with a version that is not the mod's.
$version = (Select-String -Path (Join-Path $projectDir 'CMakeLists.txt') -Pattern 'project\(\s*SubliminalHeadTracking\s+VERSION\s+([0-9]+\.[0-9]+\.[0-9]+)' | Select-Object -First 1).Matches[0].Groups[1].Value
if (-not $version) { throw 'Could not read version from CMakeLists.txt' }

if (Test-Path $releaseDir) { Remove-Item $releaseDir -Recurse -Force }
New-Item -ItemType Directory -Path $releaseDir | Out-Null

# Stage installer ZIP contents in a temp folder. In a try/finally, because
# staging can throw - the vendored-loader check below does - and a GUID-named
# directory left in %TEMP% on every failed run is litter nobody goes looking for.
$stage = Join-Path $env:TEMP "subliminal-ht-stage-$([Guid]::NewGuid().ToString('N'))"
New-Item -ItemType Directory -Path $stage | Out-Null
try {

# Launcher manifest (lopari ingests this). Ship it at the ZIP root and stamp
# the real release version (the committed copy stays at 0.0.0).
$modManifest = Join-Path $projectDir 'launcher-manifest.json'
if (-not (Test-Path $modManifest)) { throw "launcher-manifest.json not found at $modManifest" }
$manifest = Get-Content -Raw $modManifest | ConvertFrom-Json
$manifest.mod_info.version = $version
# Written without a BOM. Windows PowerShell 5.1's -Encoding UTF8 means UTF-8
# WITH one, and a JSON parser that does not skip a BOM fails on the first byte.
# Write the bytes directly instead.
[System.IO.File]::WriteAllText(
    (Join-Path $stage 'launcher-manifest.json'),
    ($manifest | ConvertTo-Json -Depth 10),
    (New-Object System.Text.UTF8Encoding $false))

# Plugin payload
$plugins = New-Item -ItemType Directory -Path (Join-Path $stage 'plugins')
Copy-Item -Force $asi (Join-Path $plugins.FullName 'SubliminalHeadTracking.asi')

# Vendor (loader)
$vendorSrc = Join-Path $projectDir 'vendor/ultimate-asi-loader'
$vendorDst = New-Item -ItemType Directory -Path (Join-Path $stage 'vendor/ultimate-asi-loader')
# Throw, not warn. launcher-manifest.json declares
# vendor/ultimate-asi-loader/dinput8.dll as a files[] source and the delivery
# mode is manifest, so a ZIP without it is invalid against its own contract -
# and Write-Warning is not an error under $ErrorActionPreference = 'Stop', so
# the packager went on and built one anyway. CI's only gate is "does a ZIP
# exist", so it stayed green and the user got "the installer ZIP is corrupt".
# The DLL, not the directory. launcher-manifest.json declares
# vendor/ultimate-asi-loader/dinput8.dll as a files[] source, so a directory
# holding only README.md and LICENSE passes a directory check, copies happily,
# and produces the same "the installer ZIP is corrupt" the throw exists to
# prevent - reachable from a partial checkout or a half-finished update-deps.
if (-not (Test-Path (Join-Path $vendorSrc 'dinput8.dll'))) {
    throw "vendor/ultimate-asi-loader/dinput8.dll is missing. Run 'pixi run update-deps' and commit the result."
}
Copy-Item -Force (Join-Path $vendorSrc '*') $vendorDst.FullName -Recurse

# Installer scripts + game-detection shim. Copy-SharedBundle stages the whole
# shim set (find-game.ps1, GamePathDetection.psm1, games.json, the uninstall
# body's cecil-marker-check.ps1); hand-copying only find-game.ps1 + games.json
# shipped an installer that aborted with "Installer ZIP is corrupt" because the
# module find-game.ps1 imports was missing from shared/.
Copy-Item -Force (Join-Path $projectDir 'scripts/install.cmd') $stage
Copy-Item -Force (Join-Path $projectDir 'scripts/uninstall.cmd') $stage
Copy-SharedBundle -StagingDir $stage

# Docs
foreach ($f in @('README.md', 'LICENSE', 'CHANGELOG.md', 'THIRD-PARTY-NOTICES.md')) {
    Copy-Item -Force (Join-Path $projectDir $f) $stage
}

$installerZip = Join-Path $releaseDir "SubliminalHeadTracking-v$version-installer.zip"
Compress-Archive -Path (Join-Path $stage '*') -DestinationPath $installerZip -Force
Write-Host "Built $installerZip" -ForegroundColor Green
} finally {
    Remove-Item -LiteralPath $stage -Recurse -Force -ErrorAction SilentlyContinue
}
