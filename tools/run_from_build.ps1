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
