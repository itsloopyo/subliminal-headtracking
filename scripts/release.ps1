# SPDX-License-Identifier: MIT
# Copyright (c) 2026 itsloopyo

[CmdletBinding()]
param(
    # NOT Mandatory: PowerShell satisfies a missing mandatory parameter by
    # reading stdin, and `pixi run release` allocates no TTY - that prompt dies
    # with "IOException: The handle is invalid" instead of printing a usage
    # line. Validate it ourselves and fail fast.
    [Parameter(Position=0)][string]$Version,
    # Ship a release even when there are no user-facing commits since the last
    # tag (writes a maintenance changelog entry instead of aborting).
    [switch]$Force,
    # Forwarded to release-nightly.ps1, which declares it but could not be
    # reached: `pixi run release` only ever invokes this script.
    [switch]$AllowDirty
)
$ErrorActionPreference = 'Stop'

$root = Resolve-Path (Join-Path $PSScriptRoot '..')

if (-not $Version) {
    Write-Host "Usage: pixi run release <major|minor|patch|nightly|X.Y.Z> [-Force] [-AllowDirty]" -ForegroundColor Red
    exit 1
}

if ($Version -eq 'nightly') {
    & (Join-Path $PSScriptRoot 'release-nightly.ps1') -AllowDirty:$AllowDirty
    exit $LASTEXITCODE
}

Import-Module (Join-Path $root 'cameraunlock-core/powershell/ReleaseWorkflow.psm1') -Force

# Windows PowerShell 5.1's `-Encoding utf8` means UTF-8 WITH a BOM, and pixi
# rejects a pixi.toml that starts with one ("Missing table in manifest"). The
# release then aborts inside `pixi run package`, after the version has already
# been stamped into four files and before the tag exists - a half-bumped tree
# with no way forward. Write raw text through .NET instead, which also leaves
# the file's existing line endings alone (Get-Content/Set-Content round-trips
# every file to CRLF).
function Set-TextFileNoBom {
    param([string]$Path, [string]$Text)
    [System.IO.File]::WriteAllText($Path, $Text, (New-Object System.Text.UTF8Encoding $false))
}

function Update-VersionInFile {
    param([string]$Path, [string]$Pattern, [string]$Replacement)
    $full = Join-Path $root $Path
    $text = [System.IO.File]::ReadAllText($full)
    $updated = $text -replace $Pattern, $Replacement
    if ($updated -eq $text) { throw "Version stamp did not match anything in $Path" }
    Set-TextFileNoBom -Path $full -Text $updated
}

# Mirrors New-ChangelogFromCommits' insertion so a -Force maintenance entry
# lands in the same place with the same shape.
function Add-MaintenanceChangelogEntry {
    param([string]$Path, [string]$NewVersion)
    $date = Get-Date -Format 'yyyy-MM-dd'
    $entry = "## [$NewVersion] - $date`n`n### Changed`n`n- Maintenance release (no user-facing changes).`n`n"
    $changelog = Get-Content (Join-Path $root $Path) -Raw
    if ($changelog -match '(?s)(# Changelog.*?)(## \[)') {
        $changelog = $changelog -replace '(?s)(# Changelog.*?\n\n)', "`$1$entry"
    } else {
        $changelog = $changelog -replace '(?s)(# Changelog.*?\n)', "`$1$entry"
    }
    $changelog = $changelog.TrimEnd() + "`n"
    Set-TextFileNoBom -Path (Join-Path $root $Path) -Text $changelog
}

Push-Location $root
try {
    # CMakeLists.txt is canonical: release.yml re-reads this exact pattern to
    # check the tag against the built artifact.
    $verLine = Select-String -Path 'CMakeLists.txt' -Pattern 'project\(SubliminalHeadTracking VERSION ([0-9]+\.[0-9]+\.[0-9]+)'
    if (-not $verLine) { throw 'Could not read the project version from CMakeLists.txt' }
    $current = $verLine.Matches[0].Groups[1].Value
    $new = Resolve-ReleaseVersion -Argument $Version -CurrentVersion $current

    # CMake's project(... VERSION) rejects a non-numeric suffix. Every stamp
    # below matches on the version already in the file rather than the new one,
    # so all five apply cleanly and the run dies later, at CMake configure time
    # inside `pixi run package` - leaving five modified files, no commit and no
    # tag. Refuse it here instead, where the tree is still clean.
    if ($new -notmatch '^[0-9]+\.[0-9]+\.[0-9]+$') {
        throw "Version '$new' is not X.Y.Z. CMake's project() cannot carry a pre-release suffix, so this repo does not cut them."
    }

    # New-ReleaseTag pushes to `main`, so releasing from any other branch would
    # push commits the branch does not contain. Gate before anything mutates.
    $branch = (git rev-parse --abbrev-ref HEAD).Trim()
    if ($branch -ne 'main') { throw "Releases cut from 'main' only; currently on '$branch'." }
    if (-not (Test-CleanGitStatus)) { throw 'Working tree is dirty - commit or stash first.' }
    if (Test-GitTagExists -Tag "v$new") { throw "Tag v$new already exists." }

    # Generate CHANGELOG from commits since last tag. This is the gate that
    # aborts when there are no user-facing commits, so run it BEFORE mutating
    # any version files or building - a failure here then leaves a clean tree
    # instead of stranding a half-applied version bump with no tag.
    try {
        New-ChangelogFromCommits -ChangelogPath 'CHANGELOG.md' -Version $new | Out-Null
    } catch {
        if (-not $Force) {
            Write-Host "Error: $($_.Exception.Message)" -ForegroundColor Red
            Write-Host 'No user-facing changes to release. Re-run with -Force for a maintenance release.' -ForegroundColor Yellow
            exit 1
        }
        Write-Host 'No user-facing commits since last tag - writing maintenance entry (-Force).' -ForegroundColor Yellow
        Add-MaintenanceChangelogEntry -Path 'CHANGELOG.md' -NewVersion $new
    }

    # THIRD-PARTY-NOTICES.md names the cameraunlock-core commit compiled into
    # the release ZIPs, and bumping the submodule does not touch it. sync-core-
    # notices.ps1 is the gate that refuses the mismatch, so a bump with no
    # notices edit stopped the release here, or in CI once the tag had already
    # been pushed. Re-sync it and let this release carry the correction.
    #
    # AFTER every gate that can abort, including the changelog one below. Run
    # earlier, it committed to whatever branch happened to be checked out and
    # only then threw "releases cut from main only", leaving an unrequested
    # commit behind on a branch nobody was releasing.
    & (Join-Path $root 'cameraunlock-core\scripts\sync-core-notices.ps1') -Repo $root
    if ($LASTEXITCODE -ne 0) { throw "sync-core-notices.ps1 exited $LASTEXITCODE - fix THIRD-PARTY-NOTICES.md before releasing." }
    & git -C $root diff --quiet -- THIRD-PARTY-NOTICES.md
    if ($LASTEXITCODE -ne 0) {
        & git -C $root commit -q -m 'chore: record the cameraunlock-core commit this build compiles' -- THIRD-PARTY-NOTICES.md
        if ($LASTEXITCODE -ne 0) { throw "Could not commit the re-synced THIRD-PARTY-NOTICES.md." }
        Write-Host 'THIRD-PARTY-NOTICES.md re-synced to the pinned cameraunlock-core commit.' -ForegroundColor Yellow
    }


    # Stamp the new version into every place it lives. CMakeLists.txt is
    # canonical; the rest are hand-kept copies and drift silently if skipped
    # (install.cmd prints MOD_VERSION to the user, the manifest is the
    # launcher's contract).
    $versionFiles = @('CMakeLists.txt','pixi.toml','launcher-manifest.json','scripts/install.cmd','CHANGELOG.md')
    Update-VersionInFile -Path 'CMakeLists.txt' -Pattern 'project\(SubliminalHeadTracking VERSION [0-9.]+' -Replacement "project(SubliminalHeadTracking VERSION $new"
    Update-VersionInFile -Path 'pixi.toml' -Pattern '(?m)^version = "[0-9.]+"' -Replacement "version = `"$new`""
    # Targeted replace, not Update-ManifestVersion: that round-trips through
    # ConvertTo-Json and reformats the whole launcher contract file.
    Update-VersionInFile -Path 'launcher-manifest.json' -Pattern '("version":\s*")\d+\.\d+\.\d+(")' -Replacement "`${1}$new`$2"
    # install.cmd is CRLF-sensitive; -replace on the raw text and a byte write
    # keep the line endings the file already has.
    Update-VersionInFile -Path 'scripts/install.cmd' -Pattern '(?m)^set "MOD_VERSION=[0-9.]+"' -Replacement "set `"MOD_VERSION=$new`""

    # Build and package through the same pixi chain CI runs (setup -> build ->
    # package), not a bare `cmake --build`, which fails outright on a checkout
    # where build/ was never configured.
    & pixi run package
    if ($LASTEXITCODE -ne 0) { throw 'Build/packaging failed' }

    # Not Invoke-VersionCommit: it hardcodes "chore: bump version to X", and
    # build.yml skips the redundant build job on a head commit starting with
    # "Release v". A mismatched subject there costs a full duplicate CI build
    # of the tag that release.yml is already building.
    foreach ($f in $versionFiles) {
        git add -- $f
        if ($LASTEXITCODE -ne 0) { throw "git add failed for $f" }
    }
    if (-not (git diff --cached --name-only)) { throw 'Version stamping produced no staged changes.' }
    git commit -m "Release v$new"
    if ($LASTEXITCODE -ne 0) { throw 'Failed to commit the release' }

    New-ReleaseTag -Version $new -Message "Release v$new"
    Write-Host "Released v$new" -ForegroundColor Green
} finally {
    Pop-Location
}
