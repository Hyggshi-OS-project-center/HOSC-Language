$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $PSScriptRoot
$cc = 'gcc'
$cxx = 'g++'
$ar = 'ar'

$cFlags = @('-Wall', '-Wextra', '-std=c99', '-O2')
$cxxFlags = @('-Wall', '-Wextra', '-std=c++17', '-O2')
$inc = @(
  "-I$root\compiler\include",
  "-I$root\runtime\include"
)
$ld = @('-luser32', '-lkernel32', '-lgdi32', '-lcomdlg32')

$compilerSrc = @(
  "$root\compiler\src\hosc_compiler.c",
  "$root\compiler\src\lexer.c",
  "$root\compiler\src\parser.c",
  "$root\compiler\src\arena.c",
  "$root\compiler\src\codegen.c",
  "$root\compiler\src\ast_utils.c",
  "$root\runtime\src\hvm.c",
  "$root\runtime\src\hvm_compiler.c"
)

$runtimeSrc = @(
  "$root\runtime\src\hvm_runner.c",
  "$root\runtime\src\hvm.c"
)

$cliSrc = "$root\tools\hosc_cli.c"
$cppApiSrc = "$root\runtime\src\hosc_cpp_api.cpp"
$outDir = "$root\tools\bin"
$objDir = "$root\build\obj"
$compilerExe = "$outDir\hosc-compiler.exe"
$compilerAliasExe = "$outDir\hosc_compiler.exe"
$cliExe = "$outDir\hosc.exe"
$runtimeExe = "$outDir\hvm.exe"
$cppLib = "$outDir\libhppapi.a"

New-Item -ItemType Directory -Force -Path $outDir | Out-Null
New-Item -ItemType Directory -Force -Path $objDir | Out-Null

function Get-ObjPath([string]$src) {
  $rel = $src.Substring($root.Length).TrimStart('\', '/')
  $rel = $rel -replace '[\\/:.]', '_'
  return Join-Path $objDir ($rel + '.o')
}

function Invoke-Native([string]$filePath, [string[]]$arguments, [string]$failureMessage) {
  & $filePath @arguments
  if ($LASTEXITCODE -ne 0) {
    throw $failureMessage
  }
}

function Compile-Source([string]$src) {
  $obj = Get-ObjPath $src
  $compiler = $cc
  $flags = $cFlags

  if ($src.ToLower().EndsWith('.cpp')) {
    $compiler = $cxx
    $flags = $cxxFlags
  }

  Invoke-Native $compiler ($flags + $inc + @('-c', $src, '-o', $obj)) "Compile failed: $src"
  if (-not (Test-Path $obj)) {
    throw "Object file missing after compile: $obj"
  }
  return $obj
}

function Link-Executable([string]$output, [string[]]$objects) {
  if (Test-Path $output) {
    Remove-Item $output -Force
  }

  Invoke-Native $cxx ($cxxFlags + $inc + @('-o', $output) + $objects + $ld) "Link failed: $output"
  if (-not (Test-Path $output)) {
    throw "Linked executable missing: $output"
  }
}

$compilerObjs = @()
foreach ($src in $compilerSrc) {
  $compilerObjs += Compile-Source $src
}

$runtimeObjs = @()
foreach ($src in $runtimeSrc) {
  $runtimeObjs += Compile-Source $src
}

$cliObj = Compile-Source $cliSrc
$cppObj = Compile-Source $cppApiSrc

if (Test-Path $cppLib) {
  Remove-Item $cppLib -Force
}
Invoke-Native $ar @('rcs', $cppLib, $cppObj) "Archive failed: $cppLib"
if (-not (Test-Path $cppLib)) {
  throw "Static library missing: $cppLib"
}

$compilerLinkObjs = @($cliObj) + $compilerObjs + @($cppObj)
$runtimeLinkObjs = $runtimeObjs + @($cppObj)

Link-Executable $compilerExe $compilerLinkObjs
Link-Executable $cliExe $compilerLinkObjs
Link-Executable $runtimeExe $runtimeLinkObjs

Copy-Item -Path $compilerExe -Destination $compilerAliasExe -Force
if (-not (Test-Path $compilerAliasExe)) {
  throw "Compiler alias missing: $compilerAliasExe"
}

Write-Host 'Build complete:'
Write-Host "  $compilerExe"
Write-Host "  $compilerAliasExe"
Write-Host "  $runtimeExe"
Write-Host "  $cliExe"
Write-Host "  $cppLib"
