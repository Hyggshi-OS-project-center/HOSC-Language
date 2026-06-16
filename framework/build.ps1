param(
    [string]$ScriptPath
)

$ErrorActionPreference = "Stop"

$frameworkDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$repoDir = Split-Path -Parent $frameworkDir
$gccCandidates = @(
    "gcc",
    "C:\mingw64\bin\gcc.exe"
)

$gcc = $null
foreach ($candidate in $gccCandidates) {
    if ($candidate -eq "gcc") {
        $cmd = Get-Command gcc -ErrorAction SilentlyContinue
        if ($cmd) {
            $gcc = $cmd.Source
            break
        }
    } elseif (Test-Path $candidate) {
        $gcc = $candidate
        break
    }
}

if (-not $gcc) {
    throw "gcc was not found. Install MinGW or add gcc to PATH."
}

$binDir = Join-Path $frameworkDir "bin"
$buildDir = Join-Path $frameworkDir "build"
$runtimeExe = Join-Path $binDir "hosc_framework.exe"

if (-not (Test-Path $binDir)) {
    New-Item -ItemType Directory -Path $binDir | Out-Null
}

if (-not (Test-Path $buildDir)) {
    New-Item -ItemType Directory -Path $buildDir | Out-Null
}

$compileArgs = @(
    "-Wall",
    "-Wextra",
    "-std=c99",
    "-O2",
    "-Iframework\include",
    "-o",
    "framework\bin\hosc_framework.exe",
    "framework\src\hosc_framework.c",
    "framework\src\hosc_runtime.c",
    "framework\src\hosc_modules.c",
    "-luser32",
    "-lgdi32",
    "-lkernel32",
    "-lgdiplus",
    "-lole32",
    "-lcomdlg32",
    "-lmfplay",
    "-lmfplat",
    "-lmf",
    "-lmfuuid"
)

Push-Location $repoDir
try {
    & $gcc @compileArgs
    if ($LASTEXITCODE -ne 0) {
        throw "gcc failed with exit code $LASTEXITCODE"
    }

    Write-Host "Built runtime:" $runtimeExe

    if ($ScriptPath) {
        $resolvedScript = Resolve-Path $ScriptPath -ErrorAction Stop
        & $runtimeExe build $resolvedScript.Path
        if ($LASTEXITCODE -ne 0) {
            throw "hosc_framework build failed with exit code $LASTEXITCODE"
        }
    } else {
        Write-Host "No .hosc script specified. Runtime build only."
        Write-Host "Example:"
        Write-Host "  .\framework\build.ps1 .\framework\examples\Music.hosc"
    }
}
finally {
    Pop-Location
}
