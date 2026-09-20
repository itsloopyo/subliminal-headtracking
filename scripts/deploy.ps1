# SPDX-License-Identifier: MIT
# Copyright (c) 2026 itsloopyo

[CmdletBinding()]
param([Parameter(Position = 0)][string]$GamePath)
$ErrorActionPreference = 'Stop'
$root = Resolve-Path (Join-Path $PSScriptRoot '..')

Import-Module (Join-Path $root 'cameraunlock-core/powershell/GamePathDetection.psm1') -Force

$asi = Join-Path $root 'build/Release/SubliminalHeadTracking.asi'
if (-not (Test-Path $asi)) {
    Write-Host "ERROR: build output not found at $asi. Run 'pixi run build' first." -ForegroundColor Red
    exit 1
}

# Same resolution order install.cmd uses: an explicitly supplied path wins,
# otherwise Find-GamePath walks env var -> registry -> Steam libraries from the
# games.json entry.
if ($GamePath) {
    if (-not (Test-Path -LiteralPath $GamePath -PathType Container)) {
        Write-Host "ERROR: supplied game path is not a directory: $GamePath" -ForegroundColor Red
        exit 1
    }
} else {
    $GamePath = Find-GamePath -GameId 'subliminal'
}
if (-not $GamePath) {
    Write-Host "ERROR: Subliminal install not found. Set SUBLIMINAL_PATH or pass the path as the first argument." -ForegroundColor Red
    exit 1
}

$cfg = Get-GameConfig -GameId 'subliminal'
$exe = Join-Path $GamePath $cfg.Executable
if (-not (Test-Path $exe)) {
    Write-Host "ERROR: game exe not found at $exe." -ForegroundColor Red
    exit 1
}
$exeDir = Split-Path $exe

Write-Host "Deploying to $exeDir" -ForegroundColor Cyan

# The game never loads dinput8.dll from its own directory (only from System32,
# via restricted-search LoadLibrary), so the loader must proxy a static EXE
# import instead. winmm.dll is statically imported and free (dwmapi is taken by
# UE4SS). A dinput8.dll left by an older deploy never loads and only confuses
# the next diagnosis.
# Only when it is OUR artifact. Unqualified, this removed any dinput8.dll in the
# game folder. Another mod's loader in the dinput8 slot is not ours to delete,
# and the line printed about it was false for that file.
$vendored = Join-Path $root 'vendor/ultimate-asi-loader/dinput8.dll'
if (-not (Test-Path -LiteralPath $vendored)) {
    throw "vendor/ultimate-asi-loader/dinput8.dll is missing. Run 'pixi run update-deps'."
}
$staleLoader = Join-Path $exeDir 'dinput8.dll'
if (Test-Path -LiteralPath $staleLoader) {
    if ((Get-FileHash -Algorithm SHA256 $staleLoader).Hash -eq (Get-FileHash -Algorithm SHA256 $vendored).Hash) {
        Remove-Item -Force -LiteralPath $staleLoader
        Write-Host '  Removed stale dinput8.dll (never loaded by this game)' -ForegroundColor Yellow
    } else {
        Write-Host '  Left a dinput8.dll this mod did not place - it belongs to something else' -ForegroundColor Yellow
    }
}

# ASI_LOADER_NAME in install.cmd is winmm.dll; the vendored artifact ships as
# dinput8.dll and is renamed on deploy, exactly as the installer does it.
$loader = Join-Path $exeDir 'winmm.dll'
if (-not (Test-Path -LiteralPath $loader)) {
    Copy-Item -LiteralPath $vendored -Destination $loader -Force
    Write-Host '  Deployed Ultimate ASI Loader -> winmm.dll' -ForegroundColor Green
}

Copy-Item $asi (Join-Path $exeDir 'SubliminalHeadTracking.asi') -Force
Write-Host "Deployed SubliminalHeadTracking.asi to $exeDir" -ForegroundColor Green
