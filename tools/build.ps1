param(
    [ValidateSet('Debug', 'Release')]
    [string]$Configuration = 'Release',
    [switch]$Clean,
    [switch]$RunTests,
    [string]$Compiler = 'gcc',
    [string]$Archiver = 'ar'
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$root = Split-Path -Parent $PSScriptRoot
$buildRoot = Join-Path $root 'build\bootstrap'
$objDir = Join-Path $buildRoot 'obj'
$libDir = Join-Path $buildRoot 'lib'
$binDir = Join-Path $buildRoot 'bin'
$legacyBinDir = Join-Path $root 'tools\bin'

$includeDirs = @(
    (Join-Path $root 'compiler\include'),
    (Join-Path $root 'vm\include'),
    (Join-Path $root 'runtime\include'),
    (Join-Path $root 'cli\hosc\include')
)

$commonCFlags = @('-std=c11', '-Wall', '-Wextra')
if ($Configuration -eq 'Debug') {
    $commonCFlags += @('-O0', '-g3')
} else {
    $commonCFlags += @('-O2')
}

$linkFlags = @('-lwinmm')
$archiveFlags = @('rcs')

$compilerSources = @(
    'compiler\src\diag\diagnostic.c',
    'compiler\src\arena.c',
    'compiler\src\ast_utils.c',
    'compiler\src\lexer.c',
    'compiler\src\parser.c',
    'compiler\src\lexer\lexer.c',
    'compiler\src\parser\parser.c',
    'compiler\src\ast\ast_nodes.c',
    'compiler\src\sema\symbol_table.c',
    'compiler\src\sema\type_checker.c',
    'compiler\src\ir\lowered_ir.c',
    'compiler\src\codegen\constant_pool.c',
    'compiler\src\codegen\bytecode_emitter.c',
    'compiler\src\module\import_resolver.c',
    'compiler\src\frontend\compile_session.c',
    'compiler\src\frontend\pipeline.c'
)

$vmSources = @(
    'vm\src\object\value.c',
    'vm\src\object\object.c',
    'vm\src\object\string.c',
    'vm\src\memory\gc_mark_sweep.c',
    'vm\src\native\native_registry.c',
    'vm\src\bytecode\loader.c',
    'vm\src\core\call_frame.c',
    'vm\src\core\dispatch.c',
    'vm\src\core\interpreter_loop.c',
    'vm\src\core\vm.c'
)

$runtimeSources = @(
    'runtime\src\entry\bundle_loader.c',
    'runtime\src\platform\platform_win32.c',
    'runtime\src\embed\embed_api.c',
    'runtime\src\bundle\exe_stub.c'
)

$cliSources = @(
    'cli\hosc\src\main.c',
    'cli\hosc\src\command_run.c',
    'cli\hosc\src\command_build.c',
    'cli\hosc\src\command_check.c',
    'cli\hosc\src\command_fmt.c',
    'cli\hosc\src\command_test.c',
    'cli\hosc\src\command_version.c',
    'cli\hosc\src\cli_options.c',
    'cli\hosc\src\cli_output.c'
)

$runtimeHostSources = @(
    'runtime\src\entry\main_host.c'
)

function Remove-BuildArtifacts {
    if (Test-Path $buildRoot) {
        Remove-Item -Recurse -Force $buildRoot
    }
}

function Ensure-Directory([string]$Path) {
    New-Item -ItemType Directory -Force -Path $Path | Out-Null
}

function Resolve-RepoPath([string]$RelativePath) {
    return Join-Path $root $RelativePath
}

function Get-ObjectPath([string]$RelativePath) {
    $normalized = ($RelativePath -replace '[\\/:\.]', '_') + '.o'
    return Join-Path $objDir $normalized
}

function Invoke-Native([string]$Executable, [string[]]$Arguments, [string]$FailureMessage) {
    & $Executable @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw $FailureMessage
    }
}

function Get-IncludeFlags {
    $flags = @()
    foreach ($dir in $includeDirs) {
        $flags += "-I$dir"
    }
    return $flags
}

function Compile-CSource([string]$RelativePath) {
    $sourcePath = Resolve-RepoPath $RelativePath
    $objectPath = Get-ObjectPath $RelativePath
    $args = @() + $commonCFlags + (Get-IncludeFlags) + @('-c', $sourcePath, '-o', $objectPath)

    Invoke-Native $Compiler $args "Compile failed: $RelativePath"
    return $objectPath
}

function New-StaticLibrary([string]$OutputName, [string[]]$ObjectFiles) {
    $outputPath = Join-Path $libDir $OutputName
    if (Test-Path $outputPath) {
        Remove-Item -Force $outputPath
    }
    Invoke-Native $Archiver ($archiveFlags + @($outputPath) + $ObjectFiles) "Archive failed: $OutputName"
    return $outputPath
}

function New-Executable([string]$OutputName, [string[]]$ObjectFiles, [string[]]$Libraries) {
    $outputPath = Join-Path $binDir $OutputName
    $args = @() + $commonCFlags + @('-o', $outputPath) + $ObjectFiles + $Libraries + $linkFlags
    Invoke-Native $Compiler $args "Link failed: $OutputName"
    return $outputPath
}

function Build-TargetSet {
    Ensure-Directory $objDir
    Ensure-Directory $libDir
    Ensure-Directory $binDir

    $compilerObjects = foreach ($src in $compilerSources) { Compile-CSource $src }
    $vmObjects = foreach ($src in $vmSources) { Compile-CSource $src }
    $runtimeObjects = foreach ($src in $runtimeSources) { Compile-CSource $src }
    $cliObjects = foreach ($src in $cliSources) { Compile-CSource $src }
    $runtimeHostObjects = foreach ($src in $runtimeHostSources) { Compile-CSource $src }

    $compilerLib = New-StaticLibrary 'libhosc_compiler.a' $compilerObjects
    $vmLib = New-StaticLibrary 'libhvm.a' $vmObjects
    $runtimeLib = New-StaticLibrary 'libhosc_runtime.a' $runtimeObjects

    $cliExe = New-Executable 'hosc.exe' $cliObjects @($compilerLib, $runtimeLib, $vmLib)
    $hostExe = New-Executable 'hvm_host.exe' $runtimeHostObjects @($runtimeLib, $vmLib)

    [pscustomobject]@{
        CompilerLib = $compilerLib
        VMLib = $vmLib
        RuntimeLib = $runtimeLib
        CliExe = $cliExe
        HostExe = $hostExe
    }
}

function Sync-LegacyToolsBin([string]$CliExe, [string]$HostExe) {
    Ensure-Directory $legacyBinDir

    $legacyCli = Join-Path $legacyBinDir 'hosc.exe'
    $legacyHvm = Join-Path $legacyBinDir 'hvm.exe'
    $legacyHost = Join-Path $legacyBinDir 'hvm_host.exe'

    Copy-Item -Force $CliExe $legacyCli
    Copy-Item -Force $HostExe $legacyHvm
    Copy-Item -Force $HostExe $legacyHost

    [pscustomobject]@{
        LegacyCli = $legacyCli
        LegacyHvm = $legacyHvm
        LegacyHost = $legacyHost
    }
}

function Invoke-SmokeTests([string]$CliExe) {
    $helloPath = Resolve-RepoPath 'examples\level_a\hello.hosc'
    $helloBytecode = Resolve-RepoPath 'examples\level_a\hello.hbc'

    Invoke-Native $CliExe @('version') 'Smoke test failed: version'
    Invoke-Native $CliExe @('build', $helloPath) 'Smoke test failed: build hello'
    Invoke-Native $CliExe @('run', $helloPath) 'Smoke test failed: run hello'

    if (Test-Path $helloBytecode) {
        Remove-Item -Force $helloBytecode
    }
}

if ($Clean) {
    Remove-BuildArtifacts
}

$artifacts = Build-TargetSet
$legacy = Sync-LegacyToolsBin $artifacts.CliExe $artifacts.HostExe

Write-Host ''
Write-Host 'Bootstrap build complete:'
Write-Host "  $($artifacts.CompilerLib)"
Write-Host "  $($artifacts.VMLib)"
Write-Host "  $($artifacts.RuntimeLib)"
Write-Host "  $($artifacts.CliExe)"
Write-Host "  $($artifacts.HostExe)"
Write-Host ''
Write-Host 'tools/bin sync complete:'
Write-Host "  $($legacy.LegacyCli)"
Write-Host "  $($legacy.LegacyHvm)"
Write-Host "  $($legacy.LegacyHost)"

if ($RunTests) {
    Write-Host ''
    Write-Host 'Running bootstrap smoke tests...'
    Invoke-SmokeTests $artifacts.CliExe
}
