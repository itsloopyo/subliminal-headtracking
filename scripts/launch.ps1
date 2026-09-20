# SPDX-License-Identifier: MIT
# Copyright (c) 2026 itsloopyo

<#
.SYNOPSIS
    Launches Subliminal for a head-tracking test run.

.DESCRIPTION
    Goes through steam.exe -applaunch, because the install ships no
    steam_appid.txt and the exe exits immediately when started directly.
    Adds -nosplash, and optionally a small windowed resolution. Which map the
    game boots into cannot be set from here - the engine ignores a map name on
    the command line in this build.

.PARAMETER Windowed
    Launch windowed at -ResX by -ResY instead of the saved display mode.
#>
param(
    [switch]$Windowed,
    [int]$ResX = 1280,
    [int]$ResY = 720
)

$ErrorActionPreference = 'Stop'

$root = Resolve-Path (Join-Path $PSScriptRoot '..')
Import-Module (Join-Path $root 'cameraunlock-core/powershell/GamePathDetection.psm1') -Force

# Same resolution order deploy.ps1 and install.cmd use: env var -> registry ->
# Steam libraries from the games.json entry. Two hardcoded Program Files paths
# stood here instead and saw nothing on a second Steam library.
$cfg = Get-GameConfig -GameId 'subliminal'
$gamePath = Find-GamePath -Config $cfg
if (-not $gamePath) {
    throw 'Could not locate Subliminal install. Set $env:SUBLIMINAL_PATH.'
}

$steam = Join-Path ${env:ProgramFiles(x86)} 'Steam\steam.exe'
if (-not (Test-Path $steam)) {
    throw "steam.exe not found: $steam"
}

$gameArgs = @('-nosplash')
if ($Windowed) { $gameArgs += @('-windowed', "-ResX=$ResX", "-ResY=$ResY") }

Write-Host "Launching via Steam: $($gameArgs -join ' ')" -ForegroundColor Cyan
Start-Process -FilePath $steam -ArgumentList (@('-applaunch', $cfg.SteamAppId) + $gameArgs)
