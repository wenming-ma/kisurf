#!/usr/bin/env pwsh

param(
    [string]$BuildDir,
    [string]$Preset,
    [switch]$CopyPdbs,
    [switch]$SyncBuildTree
)

$ErrorActionPreference = "Stop"

function Get-CMakeCacheValue {
    param(
        [string]$CachePath,
        [string]$Name
    )

    $match = Select-String -Path $CachePath -Pattern "^$([regex]::Escape($Name)):[^=]+=(.*)$" |
        Select-Object -First 1

    if( $match )
    {
        return $match.Matches[0].Groups[1].Value
    }

    return $null
}

function Copy-FilesIfPresent {
    param(
        [string]$SourceDir,
        [string]$DestinationDir,
        [string]$Filter = "*",
        [switch]$Recurse
    )

    if( -not ( Test-Path -LiteralPath $SourceDir ) )
    {
        return
    }

    New-Item -ItemType Directory -Path $DestinationDir -Force | Out-Null

    Get-ChildItem -LiteralPath $SourceDir -Filter $Filter | ForEach-Object {
        if( $_.PSIsContainer )
        {
            if( $Recurse )
            {
                Copy-Item -LiteralPath $_.FullName -Destination $DestinationDir -Recurse -Force
            }
        }
        else
        {
            Copy-Item -LiteralPath $_.FullName -Destination $DestinationDir -Force
        }
    }
}

function Copy-LocalDllsToBuildTree {
    param(
        [string]$BuildDir,
        [string[]]$DestinationDirs
    )

    $sourceDirs = @(
        "api",
        "common",
        "common\gal"
    )

    foreach( $relativeDestinationDir in $DestinationDirs )
    {
        $destinationDir = Join-Path $BuildDir $relativeDestinationDir

        if( -not ( Test-Path -LiteralPath $destinationDir ) )
        {
            continue
        }

        foreach( $relativeSourceDir in $sourceDirs )
        {
            $sourceDir = Join-Path $BuildDir $relativeSourceDir

            if( ( Resolve-Path -LiteralPath $sourceDir -ErrorAction SilentlyContinue ).Path -eq
                ( Resolve-Path -LiteralPath $destinationDir -ErrorAction SilentlyContinue ).Path )
            {
                continue
            }

            Copy-FilesIfPresent -SourceDir $sourceDir -DestinationDir $destinationDir -Filter "*.dll"
        }
    }
}

function Resolve-BuildDirectory {
    param(
        [string]$ProjectRoot,
        [string]$BuildDirArgument,
        [string]$PresetArgument
    )

    if( $BuildDirArgument )
    {
        return ( Resolve-Path -LiteralPath $BuildDirArgument ).Path
    }

    if( $PresetArgument )
    {
        $presetPath = Join-Path $ProjectRoot "build\$PresetArgument"
        return ( Resolve-Path -LiteralPath $presetPath ).Path
    }

    $candidates = Get-ChildItem -Path ( Join-Path $ProjectRoot "build" ) -Directory -ErrorAction SilentlyContinue |
        Where-Object { $_.Name -ne "install" } |
        Where-Object { Test-Path -LiteralPath ( Join-Path $_.FullName "CMakeCache.txt" ) } |
        Where-Object { Test-Path -LiteralPath ( Join-Path $_.FullName "cmake_install.cmake" ) } |
        Sort-Object LastWriteTime -Descending

    if( -not $candidates )
    {
        throw "No build directory with a CMakeCache.txt was found under '$ProjectRoot\build'."
    }

    return $candidates[0].FullName
}

$projectRoot = ( git rev-parse --show-toplevel ).Trim()
$resolvedBuildDir = Resolve-BuildDirectory -ProjectRoot $projectRoot -BuildDirArgument $BuildDir -PresetArgument $Preset
$cachePath = Join-Path $resolvedBuildDir "CMakeCache.txt"
$installScriptPath = Join-Path $resolvedBuildDir "cmake_install.cmake"

if( -not ( Test-Path -LiteralPath $cachePath ) )
{
    throw "CMakeCache.txt was not found in '$resolvedBuildDir'."
}

if( -not ( Test-Path -LiteralPath $installScriptPath ) )
{
    throw "cmake_install.cmake was not found in '$resolvedBuildDir'. Build the preset first."
}

$installPrefix = Get-CMakeCacheValue -CachePath $cachePath -Name "CMAKE_INSTALL_PREFIX"
$vcpkgInstalledDir = Get-CMakeCacheValue -CachePath $cachePath -Name "VCPKG_INSTALLED_DIR"
$triplet = Get-CMakeCacheValue -CachePath $cachePath -Name "VCPKG_TARGET_TRIPLET"
$buildType = Get-CMakeCacheValue -CachePath $cachePath -Name "CMAKE_BUILD_TYPE"

if( -not $installPrefix )
{
    throw "CMAKE_INSTALL_PREFIX is missing from '$cachePath'."
}

if( -not $vcpkgInstalledDir -or -not $triplet )
{
    throw "VCPKG settings are missing from '$cachePath'."
}

if( -not $buildType )
{
    $buildType = "RelWithDebInfo"
}

$runtimeBin = if( $buildType -eq "Debug" )
{
    Join-Path $vcpkgInstalledDir "$triplet\debug\bin"
}
else
{
    Join-Path $vcpkgInstalledDir "$triplet\bin"
}

$pythonToolsDir = Join-Path $vcpkgInstalledDir "$triplet\tools\python3"
$installBinDir = Join-Path $installPrefix "bin"

Write-Host "Using build directory: $resolvedBuildDir"
Write-Host "Build type: $buildType"
Write-Host "Install prefix: $installPrefix"

& cmake --install $resolvedBuildDir

if( $LASTEXITCODE -ne 0 )
{
    throw "cmake --install failed for '$resolvedBuildDir'."
}

if( -not ( Test-Path -LiteralPath $installBinDir ) )
{
    throw "Install step did not create '$installBinDir'."
}

Copy-FilesIfPresent -SourceDir $runtimeBin -DestinationDir $installBinDir -Filter "*.dll"
Copy-FilesIfPresent -SourceDir $pythonToolsDir -DestinationDir $installBinDir -Recurse

if( $CopyPdbs )
{
    Get-ChildItem -Path $resolvedBuildDir -Recurse -Filter *.pdb | ForEach-Object {
        Copy-Item -LiteralPath $_.FullName -Destination $installBinDir -Force
    }
}

if( $SyncBuildTree )
{
    $runtimeDirs = @(
        "kicad",
        "common",
        "api",
        "common\gal",
        "pcbnew",
        "eeschema",
        "cvpcb",
        "gerbview",
        "pagelayout_editor",
        "pcb_calculator",
        "bitmap2component"
    )

    foreach( $relativeDir in $runtimeDirs )
    {
        $destinationDir = Join-Path $resolvedBuildDir $relativeDir

        if( Test-Path -LiteralPath $destinationDir )
        {
            Copy-FilesIfPresent -SourceDir $runtimeBin -DestinationDir $destinationDir -Filter "*.dll"
        }
    }

    Copy-LocalDllsToBuildTree -BuildDir $resolvedBuildDir -DestinationDirs $runtimeDirs
}
