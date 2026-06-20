# Windows Build Tree Launcher Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Provide a reliable Windows build-tree launcher for developer-preview editor use and Computer Use smoke testing.

**Architecture:** Add a small PowerShell script that resolves the build directory, computes a child-process runtime PATH from the build tree and CMake cache, and starts the requested executable. Add dry-run JSON output for testability.

**Tech Stack:** PowerShell 7-compatible script, CMake build-tree layout, README documentation, Computer Use desktop verification.

---

## File Structure

- Create: `tools/run_from_build.ps1`
  - Launches selected KiSurf/KiCad executables from a build tree with the right child-process environment.
- Modify: `README.md`
  - Replaces direct build-tree exe launch commands with launcher commands and documents direct exe caveat.
- Create: `docs/superpowers/specs/2026-06-19-windows-build-tree-launcher-design.md`
- Create: `docs/superpowers/plans/2026-06-19-windows-build-tree-launcher-implementation.md`

## Task 1: Red Test for Missing Launcher

**Files:**
- No edits before this task.

- [ ] **Step 1: Run intended dry-run command before implementation**

Run:

```powershell
.\tools\run_from_build.ps1 -BuildDir .\out\build\x64-release -App pcbnew -DryRun -AsJson
```

Expected before implementation: fails because `tools\run_from_build.ps1` does
not exist.

## Task 2: Launcher Script

**Files:**
- Create: `tools/run_from_build.ps1`

- [ ] **Step 1: Create the script**

Create `tools/run_from_build.ps1` with:

```powershell
#!/usr/bin/env pwsh

param(
    [string]$BuildDir = ".\out\build\x64-release",
    [ValidateSet("pcbnew", "eeschema", "kicad", "qa_common")]
    [string]$App = "pcbnew",
    [switch]$DryRun,
    [switch]$AsJson
)

$ErrorActionPreference = "Stop"

function Get-CMakeCacheValue {
    param(
        [string]$CachePath,
        [string]$Name
    )

    if( -not ( Test-Path -LiteralPath $CachePath ) )
    {
        return $null
    }

    $match = Select-String -Path $CachePath -Pattern "^$([regex]::Escape($Name)):[^=]+=(.*)$" |
        Select-Object -First 1

    if( $match )
    {
        return $match.Matches[0].Groups[1].Value
    }

    return $null
}

function Resolve-AppPath {
    param(
        [string]$ResolvedBuildDir,
        [string]$AppName
    )

    $relativePathByApp = @{
        pcbnew = "pcbnew\pcbnew.exe"
        eeschema = "eeschema\eeschema.exe"
        kicad = "kicad\kicad.exe"
        qa_common = "qa\tests\common\qa_common.exe"
    }

    return Join-Path $ResolvedBuildDir $relativePathByApp[$AppName]
}

function Build-RuntimePathDirs {
    param(
        [string]$ResolvedBuildDir
    )

    $dirs = @()
    $cachePath = Join-Path $ResolvedBuildDir "CMakeCache.txt"
    $vcpkgInstalledDir = Get-CMakeCacheValue -CachePath $cachePath -Name "VCPKG_INSTALLED_DIR"
    $triplet = Get-CMakeCacheValue -CachePath $cachePath -Name "VCPKG_TARGET_TRIPLET"
    $buildType = Get-CMakeCacheValue -CachePath $cachePath -Name "CMAKE_BUILD_TYPE"

    if( -not $buildType )
    {
        $buildType = "Release"
    }

    if( $vcpkgInstalledDir -and $triplet )
    {
        $runtimeBin = if( $buildType -eq "Debug" )
        {
            Join-Path $vcpkgInstalledDir "$triplet\debug\bin"
        }
        else
        {
            Join-Path $vcpkgInstalledDir "$triplet\bin"
        }

        if( Test-Path -LiteralPath $runtimeBin )
        {
            $dirs += ( Resolve-Path -LiteralPath $runtimeBin ).Path
        }
    }

    foreach( $relativeDir in @( "kicad", "common", "api", "common\gal", "pcbnew", "eeschema", "cvpcb" ) )
    {
        $path = Join-Path $ResolvedBuildDir $relativeDir

        if( Test-Path -LiteralPath $path )
        {
            $dirs += ( Resolve-Path -LiteralPath $path ).Path
        }
    }

    return $dirs
}

$resolvedBuildDir = ( Resolve-Path -LiteralPath $BuildDir ).Path
$executablePath = Resolve-AppPath -ResolvedBuildDir $resolvedBuildDir -AppName $App

if( -not ( Test-Path -LiteralPath $executablePath ) )
{
    throw "Executable for '$App' was not found at '$executablePath'. Build the target first."
}

$runtimePathDirs = Build-RuntimePathDirs -ResolvedBuildDir $resolvedBuildDir
$previousPath = $env:PATH
$hadPreviousRunFromBuild = Test-Path Env:KICAD_RUN_FROM_BUILD_DIR
$previousRunFromBuild = $env:KICAD_RUN_FROM_BUILD_DIR
$childPath = ( $runtimePathDirs + $previousPath ) -join ";"

$result = [ordered]@{
    app = $App
    build_dir = $resolvedBuildDir
    executable = ( Resolve-Path -LiteralPath $executablePath ).Path
    kicad_run_from_build_dir = "1"
    path_dirs = $runtimePathDirs
}

try
{
    $env:KICAD_RUN_FROM_BUILD_DIR = "1"
    $env:PATH = $childPath

    if( $DryRun )
    {
        if( $AsJson )
        {
            $result | ConvertTo-Json -Depth 4
        }
        else
        {
            $result
        }

        return
    }

    Write-Host "Launching $App from $resolvedBuildDir"
    Start-Process -FilePath $executablePath -WorkingDirectory $resolvedBuildDir
}
finally
{
    $env:PATH = $previousPath

    if( $hadPreviousRunFromBuild )
    {
        $env:KICAD_RUN_FROM_BUILD_DIR = $previousRunFromBuild
    }
    else
    {
        Remove-Item Env:KICAD_RUN_FROM_BUILD_DIR -ErrorAction SilentlyContinue
    }
}
```

- [ ] **Step 2: Run dry-run JSON verification**

Run:

```powershell
$json = .\tools\run_from_build.ps1 -BuildDir .\out\build\x64-release -App pcbnew -DryRun -AsJson | ConvertFrom-Json
if( $json.app -ne 'pcbnew' ) { throw 'wrong app' }
if( -not ( Test-Path -LiteralPath $json.executable ) ) { throw 'missing executable' }
if( $json.kicad_run_from_build_dir -ne '1' ) { throw 'missing build env' }
foreach( $required in @('common','api','common\gal','pcbnew') )
{
    if( -not ( $json.path_dirs | Where-Object { $_ -like "*\$required" } ) )
    {
        throw "missing path dir $required"
    }
}
'DRY_RUN_OK'
```

Expected: prints `DRY_RUN_OK`.

- [ ] **Step 3: Verify dry-run restores caller environment**

Run:

```powershell
$oldPath = $env:PATH
$hadRunFromBuild = Test-Path Env:KICAD_RUN_FROM_BUILD_DIR
$oldRunFromBuild = $env:KICAD_RUN_FROM_BUILD_DIR
Remove-Item Env:KICAD_RUN_FROM_BUILD_DIR -ErrorAction SilentlyContinue
.\tools\run_from_build.ps1 -BuildDir .\out\build\x64-release -App pcbnew -DryRun -AsJson | Out-Null
if( $env:PATH -ne $oldPath ) { throw 'PATH_DIRTY' }
if( Test-Path Env:KICAD_RUN_FROM_BUILD_DIR ) { throw 'KICAD_RUN_FROM_BUILD_DIR_DIRTY' }
if( $hadRunFromBuild ) { $env:KICAD_RUN_FROM_BUILD_DIR = $oldRunFromBuild }
'CALLER_ENV_CLEAN'
```

Expected before restoring the caller environment: fails with `PATH_DIRTY`.
Expected after implementation: prints `CALLER_ENV_CLEAN`.

## Task 3: README Quickstart Update

**Files:**
- Modify: `README.md`

- [ ] **Step 1: Replace direct executable launch commands**

Replace:

```powershell
.\out\build\x64-release\pcbnew\pcbnew.exe
.\out\build\x64-release\eeschema\eeschema.exe
```

with:

```powershell
.\tools\run_from_build.ps1 -BuildDir .\out\build\x64-release -App pcbnew
.\tools\run_from_build.ps1 -BuildDir .\out\build\x64-release -App eeschema
```

Add a short note:

```markdown
Do not launch the build-tree `.exe` files directly unless you have already
synced runtime DLLs into the build tree. On Windows, direct double-click launch
can fail with a missing `kicommon.dll` system-error dialog because sibling build
directories are not on the process PATH.
```

- [ ] **Step 2: Mention the sync alternative**

Add:

```markdown
If you prefer direct executable launch, run
`.\tools\post_build.ps1 -BuildDir .\out\build\x64-release -SyncBuildTree`
after building to copy runtime DLLs into the build output directories.
```

## Task 4: Verification and Commit

**Files:**
- `tools/run_from_build.ps1`
- `README.md`
- spec and plan docs

- [ ] **Step 1: Run script dry-run checks**

Run both dry-run verifications from Task 2.

- [ ] **Step 2: Launch through script and inspect with Computer Use**

Run:

```powershell
.\tools\run_from_build.ps1 -BuildDir .\out\build\x64-release -App pcbnew
```

Then use Computer Use to verify:

- PCB Editor main window opens.
- No missing-DLL/system-error modal is visible.
- AI menu opens and contains `Agent` and `Model Settings...`.
- Agent panel opens and exposes Chat/Preview/Log, Model, Background Agent,
  Send, and Stop controls.

- [ ] **Step 3: Run automated checks**

Run:

```powershell
git diff --check -- tools/run_from_build.ps1 README.md docs/superpowers/specs/2026-06-19-windows-build-tree-launcher-design.md docs/superpowers/plans/2026-06-19-windows-build-tree-launcher-implementation.md
$prefix = 'sk' + '-'
$pattern = $prefix + '[A-Za-z0-9_-]{20,}|OPENAI_API_KEY=.*' + $prefix
rg -n $pattern tools README.md docs/superpowers/specs/2026-06-19-windows-build-tree-launcher-design.md docs/superpowers/plans/2026-06-19-windows-build-tree-launcher-implementation.md
```

Expected: no whitespace errors; secret scan has no output.

- [ ] **Step 4: Commit**

Run:

```powershell
git add tools/run_from_build.ps1 README.md docs/superpowers/specs/2026-06-19-windows-build-tree-launcher-design.md docs/superpowers/plans/2026-06-19-windows-build-tree-launcher-implementation.md
git commit -m "tools: add windows build tree launcher"
```

## Self-Review

- Spec coverage: each launcher requirement maps to a script, README, or
  verification step.
- Placeholder scan: no unfinished marker remains.
- Type consistency: app names and paths match the script validate set.
- Verification coverage: includes red missing-script command, dry-run contract,
  actual GUI launch, Computer Use smoke, whitespace check, and secret scan.
