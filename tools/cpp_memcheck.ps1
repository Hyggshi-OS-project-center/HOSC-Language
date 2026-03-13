param(
    [Parameter(Mandatory = $true, Position = 0)]
    [string]$Source,

    [Parameter(Position = 1)]
    [string]$OutExe,

    [string]$Compiler = 'g++',

    [string[]]$ExtraFlags = @(),

    [string]$RunArgs = '',

    [switch]$CompileOnly,

    [switch]$NoRun
)

$ErrorActionPreference = 'Continue'
$PSNativeCommandUseErrorActionPreference = $false

function Fail([string]$Message) {
    Write-Error $Message
    exit 1
}

function Build-Args([bool]$CompileOnlyMode, [string[]]$Flags, [string]$OutPath, [string]$SrcPath, [string[]]$UserFlags) {
    $args = @()
    if ($CompileOnlyMode) {
        $args += '-c'
    }
    $args += $Flags
    if ($UserFlags -and $UserFlags.Count -gt 0) {
        $args += $UserFlags
    }
    $args += @('-o', $OutPath, $SrcPath)
    return $args
}

if (-not (Test-Path $Source)) {
    Fail "Source file not found: $Source"
}

$sourcePath = (Resolve-Path $Source).Path
$root = Split-Path -Parent $PSScriptRoot
$outRoot = Join-Path $root 'build\cpp_memcheck'
New-Item -ItemType Directory -Force -Path $outRoot | Out-Null

if ([string]::IsNullOrWhiteSpace($OutExe)) {
    $base = [System.IO.Path]::GetFileNameWithoutExtension($sourcePath)
    if ($CompileOnly) {
        $OutExe = Join-Path $outRoot ($base + '.o')
    } else {
        $OutExe = Join-Path $outRoot ($base + '.exe')
    }
}

$outPath = [System.IO.Path]::GetFullPath($OutExe)
$outDir = Split-Path -Parent $outPath
New-Item -ItemType Directory -Force -Path $outDir | Out-Null

$baseFlags = @(
    '-std=c++17',
    '-Wall',
    '-Wextra',
    '-Wpedantic',
    '-O1',
    '-g',
    '-fno-omit-frame-pointer'
)

$sanitizerFlags = $baseFlags + @('-fsanitize=address,undefined')
$analyzerFlags = $baseFlags + @('-fanalyzer', '-D_GLIBCXX_ASSERTIONS')

$compileErrLog = Join-Path $outRoot 'compile_stderr.log'
if (Test-Path $compileErrLog) { Remove-Item $compileErrLog -Force }

$mode = 'sanitizer'
$cmdArgs = Build-Args -CompileOnlyMode:$CompileOnly -Flags $sanitizerFlags -OutPath $outPath -SrcPath $sourcePath -UserFlags $ExtraFlags

Write-Host "==> Compile ($mode mode)" -ForegroundColor Cyan
Write-Host "$Compiler $($cmdArgs -join ' ')" -ForegroundColor DarkGray
& $Compiler @cmdArgs 2> $compileErrLog
$compileExit = $LASTEXITCODE
$compileStderr = if (Test-Path $compileErrLog) { Get-Content -Raw $compileErrLog } else { '' }

if ($compileExit -ne 0) {
    $missingSanitizer = $compileStderr -match '(?i)(cannot find\s+-lasan|cannot find\s+-lubsan)'
    if ($missingSanitizer) {
        $mode = 'analyzer'
        Write-Host 'Sanitizer runtime not available. Falling back to static analyzer mode.' -ForegroundColor Yellow

        $cmdArgs = Build-Args -CompileOnlyMode:$CompileOnly -Flags $analyzerFlags -OutPath $outPath -SrcPath $sourcePath -UserFlags $ExtraFlags
        Write-Host "==> Compile ($mode mode)" -ForegroundColor Cyan
        Write-Host "$Compiler $($cmdArgs -join ' ')" -ForegroundColor DarkGray

        & $Compiler @cmdArgs 2> $compileErrLog
        $compileExit = $LASTEXITCODE
        $compileStderr = if (Test-Path $compileErrLog) { Get-Content -Raw $compileErrLog } else { '' }
    }
}

if ($compileStderr) {
    Write-Host $compileStderr -ForegroundColor Yellow
}

if ($compileExit -ne 0) {
    Fail "Compilation failed with exit code $compileExit"
}

if ($mode -eq 'analyzer') {
    $analyzerErrorPattern = '(?im)warning:.*(use-after-free|double-free|memory leak|null dereference|out-of-bounds|buffer overflow|free of non-heap)'
    if ($compileStderr -match $analyzerErrorPattern) {
        Fail "Static analyzer reported potential memory issues. Fix warnings before run."
    }
}

if ($CompileOnly -or $NoRun) {
    Write-Host "Compile success ($mode): $outPath" -ForegroundColor Green
    exit 0
}

if (-not (Test-Path $outPath)) {
    Fail "Compiled binary not found: $outPath"
}

if ($mode -eq 'sanitizer') {
    $env:ASAN_OPTIONS = 'detect_leaks=1,halt_on_error=1,strict_string_checks=1'
    $env:UBSAN_OPTIONS = 'print_stacktrace=1,halt_on_error=1'
}

$stdoutLog = Join-Path $outRoot 'run_stdout.log'
$stderrLog = Join-Path $outRoot 'run_stderr.log'
if (Test-Path $stdoutLog) { Remove-Item $stdoutLog -Force }
if (Test-Path $stderrLog) { Remove-Item $stderrLog -Force }

Write-Host "==> Run" -ForegroundColor Cyan
$runCmd = '"{0}" {1}' -f $outPath, $RunArgs
Write-Host $runCmd -ForegroundColor DarkGray

if ([string]::IsNullOrWhiteSpace($RunArgs)) {
    & $outPath 1> $stdoutLog 2> $stderrLog
} else {
    & cmd /c $runCmd 1> $stdoutLog 2> $stderrLog
}
$runExit = $LASTEXITCODE

$stdoutText = if (Test-Path $stdoutLog) { Get-Content -Raw $stdoutLog } else { '' }
$stderrText = if (Test-Path $stderrLog) { Get-Content -Raw $stderrLog } else { '' }

if ($stdoutText) { Write-Host $stdoutText }
if ($stderrText) { Write-Host $stderrText -ForegroundColor Yellow }

$sanitizerPattern = '(?is)(AddressSanitizer|LeakSanitizer|UndefinedBehaviorSanitizer|runtime error:|heap-use-after-free|double free|use-after-free|stack-buffer-overflow|heap-buffer-overflow)'
$hasSanitizerError = $stderrText -match $sanitizerPattern

if ($runExit -ne 0 -or $hasSanitizerError) {
    Fail "Memory check failed (exit=$runExit). See logs: $stdoutLog, $stderrLog"
}

Write-Host "Memory check passed ($mode)." -ForegroundColor Green
Write-Host "Binary: $outPath" -ForegroundColor DarkGray
Write-Host "Logs: $stdoutLog, $stderrLog" -ForegroundColor DarkGray




