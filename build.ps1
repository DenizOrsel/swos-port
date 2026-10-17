<#
.SYNOPSIS
    Builds swos-port using Meson, Ninja, and MSVC.

.DESCRIPTION
    Automates environment discovery (Visual Studio / Build Tools), Python
    dependency verification, Meson setup, compilation, testing, and demo packaging.

.PARAMETER BuildType
    Build configuration: 'release' (default) or 'debug'.

.PARAMETER Package
    Builds the standalone demo package in dist/swos-port-demo-windows-x64.zip.

.PARAMETER RunTests
    Executes Python and C++ test suites upon successful build.

.PARAMETER Clean
    Removes existing build directory and reconfigures from scratch.

.EXAMPLE
    .\build.ps1
    .\build.ps1 -Package
    .\build.ps1 -BuildType debug -RunTests
#>

[CmdletBinding()]
param(
    [ValidateSet('release', 'debug')]
    [string]$BuildType = 'release',

    [switch]$Package,
    [switch]$RunTests,
    [switch]$Clean
)

$ErrorActionPreference = 'Stop'
$repoRoot = $PSScriptRoot

Write-Host "=== SWOS-Port Build Script ===" -ForegroundColor Cyan
Write-Host "Target: $BuildType" -ForegroundColor Cyan

# 1. Locate Visual Studio / Build Tools vcvarsall.bat
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$vcvars = $null

if (Test-Path $vswhere) {
    $installPath = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if ($installPath -and (Test-Path "$installPath\VC\Auxiliary\Build\vcvarsall.bat")) {
        $vcvars = "$installPath\VC\Auxiliary\Build\vcvarsall.bat"
    }
}

if (-not $vcvars) {
    # Fallback search for BuildTools / VS installations
    $candidates = @(
        "${env:ProgramFiles(x86)}\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvarsall.bat",
        "${env:ProgramFiles}\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvarsall.bat",
        "${env:ProgramFiles}\Microsoft Visual Studio\2022\Professional\VC\Auxiliary\Build\vcvarsall.bat",
        "${env:ProgramFiles}\Microsoft Visual Studio\2022\Enterprise\VC\Auxiliary\Build\vcvarsall.bat",
        "${env:ProgramFiles(x86)}\Microsoft Visual Studio\2019\BuildTools\VC\Auxiliary\Build\vcvarsall.bat"
    )
    foreach ($cand in $candidates) {
        if (Test-Path $cand) {
            $vcvars = $cand
            break
        }
    }
}

if (-not $vcvars) {
    throw "Could not locate vcvarsall.bat. Please ensure Visual Studio C++ build tools are installed."
}

Write-Host "Initializing MSVC x64 environment from: $vcvars" -ForegroundColor Green
cmd.exe /c "call `"$vcvars`" x64 >nul && set" | ForEach-Object {
    if ($_ -match "^(.*?)=(.*)$") {
        Set-Item -Path "env:$($matches[1])" -Value $matches[2]
    }
}

# 2. Add Python User Scripts to PATH
$userPythonScripts = "$env:APPDATA\Python\Python314\Scripts"
if (Test-Path $userPythonScripts) {
    $env:PATH = "$userPythonScripts;$env:PATH"
}
$localPythonScripts = "$env:LOCALAPPDATA\Programs\Python\Python314\Scripts"
if (Test-Path $localPythonScripts) {
    $env:PATH = "$localPythonScripts;$env:PATH"
}

# 3. Check for Meson and Ninja
$meson = Get-Command meson.exe -ErrorAction SilentlyContinue
$ninja = Get-Command ninja.exe -ErrorAction SilentlyContinue

if (-not $meson -or -not $ninja) {
    Write-Host "Installing missing build tools (meson, ninja, pillow, tabulate, ddt)..." -ForegroundColor Yellow
    python -m pip install --quiet meson ninja -r "$repoRoot\assets\requirements.txt" ddt
}

# 4. Meson Setup
$buildDir = "$repoRoot\tmp\meson-$BuildType"

if ($Clean -and (Test-Path $buildDir)) {
    Write-Host "Cleaning $buildDir..." -ForegroundColor Yellow
    Remove-Item -Recurse -Force $buildDir
}

if (-not (Test-Path "$buildDir\build.ninja")) {
    Write-Host "Configuring Meson in $buildDir..." -ForegroundColor Green
    meson setup $buildDir --backend=ninja "--buildtype=$BuildType"
}

# 5. Compile
Write-Host "Compiling target ($BuildType)..." -ForegroundColor Green
meson compile -C $buildDir

$exePath = "$buildDir\swos-port.exe"
if (-not (Test-Path $exePath)) {
    throw "Build completed but executable was not found at $exePath"
}
Write-Host "Build succeeded: $exePath" -ForegroundColor Green

# 6. Run Tests if requested
if ($RunTests) {
    Write-Host "Compiling test targets..." -ForegroundColor Green
    meson compile -C $buildDir tests
    Write-Host "`n=== Running Tests ===" -ForegroundColor Cyan
    
    Write-Host "1. Animation Table Generator Tests..."
    python "$repoRoot\src\game\animation\testGenerateAnimationTables.py"

    Write-Host "2. mnu2h Unit Tests..."
    Push-Location "$repoRoot\mnu2h"
    try {
        python -m unittest discover -s tests -p "Test*.py"
    } finally {
        Pop-Location
    }

    Write-Host "3. C++ Data Layout & Joypad Tests..."
    $env:PATH = "$repoRoot\3rd-party\bin\dll\x64\Release;$repoRoot\3rd-party\bin\dll\x64;$buildDir\tests;$env:PATH"
    $buildtestDir = "$repoRoot\buildtest"
    $swosDir = "$buildtestDir\swos-port"
    if (-not (Test-Path $swosDir) -and (Test-Path "$repoRoot\dist\swos-port-demo-windows-x64.zip")) {
        Write-Host "Extracting demo package to $buildtestDir..." -ForegroundColor Gray
        Expand-Archive -Path "$repoRoot\dist\swos-port-demo-windows-x64.zip" -DestinationPath $buildtestDir -Force
    }
    if (Test-Path $swosDir) {
        & "$buildDir\tests\tests.exe" "--swos-dir=$swosDir" career joypads swos-data-layout
    } else {
        & "$buildDir\tests\tests.exe" swos-data-layout
    }
}

# 7. Package Demo if requested
if ($Package) {
    Write-Host "`n=== Packaging Windows Demo ===" -ForegroundColor Cyan
    $vcRedistDir = $null
    $redistSearch = @(
        "${env:ProgramFiles(x86)}\Microsoft Visual Studio\18\BuildTools\VC\Redist\MSVC",
        "${env:ProgramFiles}\Microsoft Visual Studio\2022\Community\VC\Redist\MSVC",
        "${env:ProgramFiles}\Microsoft Visual Studio\2022\Professional\VC\Redist\MSVC",
        "${env:ProgramFiles}\Microsoft Visual Studio\2022\Enterprise\VC\Redist\MSVC"
    )
    foreach ($dir in $redistSearch) {
        if (Test-Path $dir) {
            $crt = Get-ChildItem $dir -Recurse -Filter "msvcp140.dll" -ErrorAction SilentlyContinue |
                Where-Object { $_.FullName -like "*x64*Microsoft.VC*.CRT*" -and $_.FullName -notlike "*onecore*" } |
                Select-Object -First 1 -ExpandProperty DirectoryName
            if ($crt -and (Test-Path $crt)) {
                $vcRedistDir = $crt
                break
            }
        }
    }

    $packageArgs = @(
        "$repoRoot\tools\packageWindows.py",
        "--demo-data-archive", "$repoRoot\assets\demo-data.zip",
        "--exe", $exePath,
        "--assets-dir", "$repoRoot\tmp\assets",
        "--skip-build",
        "--output", "$repoRoot\dist\swos-port-demo-windows-x64.zip"
    )

    if ($vcRedistDir) {
        Write-Host "Using VC runtime from: $vcRedistDir" -ForegroundColor Gray
        $packageArgs += @("--vc-runtime-dir", $vcRedistDir)
    }

    python @packageArgs
    if ($LASTEXITCODE -ne 0) {
        throw "Packaging failed with exit code $LASTEXITCODE"
    }
    Write-Host "Packaged successfully: $repoRoot\dist\swos-port-demo-windows-x64.zip" -ForegroundColor Green

    # Update binary in buildtest if present
    if (Test-Path "$repoRoot\buildtest\swos-port") {
        Copy-Item $exePath "$repoRoot\buildtest\swos-port\swos-port.exe" -Force
    }
}
