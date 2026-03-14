$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $PSScriptRoot
$cc = 'gcc'
$cxx = 'g++'

$cFlags = @('-Wall','-Wextra','-std=c99','-O2')
$cxxFlags = @('-Wall','-Wextra','-std=c++17','-O2')

$inc = @(
  "-I$root\\compiler\\include",
  "-I$root\\runtime\\include",
  "-I$root\\core\\include",
  "-I$root\\services\\include"
)
$ld = @('-luser32','-lkernel32','-lgdi32','-lcomdlg32')

$compilerSrc = @(
  "$root\\core\\src\\file_utils.c",
  "$root\\compiler\\src\\hosc_compiler.c",
  "$root\\compiler\\src\\lexer.c",
  "$root\\compiler\\src\\parser.c",
  "$root\\compiler\\src\\arena.cpp",
  "$root\\compiler\\src\\codegen.c",
  "$root\\compiler\\src\\ast_utils.c",
  "$root\\runtime\\src\\hvm.c",
  "$root\\runtime\\src\\bytecode.cpp",
  "$root\\runtime\\src\\bytecode_io.c",
  "$root\\runtime\\src\\gc.cpp",
  "$root\\runtime\\src\\vm_memory.cpp",
  "$root\\runtime\\src\\hvm_compiler.c",
  "$root\\services\\src\\runtime_services.c",
  "$root\\services\\src\\runtime_gui.c"
)

$runtimeSrc = @(
  "$root\\core\\src\\file_utils.c",
  "$root\\runtime\\src\\hvm_runner.c",
  "$root\\runtime\\src\\hvm_platform.c",
  "$root\\runtime\\src\\hvm.c",
  "$root\\runtime\\src\\bytecode.cpp",
  "$root\\runtime\\src\\bytecode_io.c",
  "$root\\runtime\\src\\gc.cpp",
  "$root\\runtime\\src\\vm_memory.cpp",
  "$root\\services\\src\\runtime_services.c",
  "$root\\services\\src\\runtime_gui.c"
)

New-Item -ItemType Directory -Force -Path "$root\\tools\\bin" | Out-Null
New-Item -ItemType Directory -Force -Path "$root\\build\\obj" | Out-Null

function Get-ObjPath([string]$src) {
  $rel = $src.Substring($root.Length).TrimStart('\\','/')
  $rel = $rel -replace '[\\/:.]','_'
  return Join-Path "$root\\build\\obj" ($rel + '.o')
}

function Compile-Source([string]$src, [string]$compiler, [string[]]$flags, [string[]]$includes) {
  $obj = Get-ObjPath $src
  & $compiler @flags @includes -c -o $obj $src
  if ($LASTEXITCODE -ne 0) {
    throw "Compile failed: $src"
  }
  return $obj
}

function Build-Objects([string[]]$sources) {
  $objs = @()
  foreach ($src in $sources) {
    if ($src.ToLower().EndsWith('.cpp')) {
      $objs += Compile-Source $src $cxx $cxxFlags $inc
    } else {
      $objs += Compile-Source $src $cc $cFlags $inc
    }
  }
  return ,$objs
}

$compilerObj = Build-Objects $compilerSrc
& $cxx @cxxFlags @inc -o "$root\\tools\\bin\\hosc_compiler.exe" @compilerObj @ld
if ($LASTEXITCODE -ne 0) { throw 'Link failed: hosc_compiler.exe' }

$runtimeObj = Build-Objects $runtimeSrc
& $cxx @cxxFlags @inc -o "$root\\tools\\bin\\hvm.exe" @runtimeObj @ld
if ($LASTEXITCODE -ne 0) { throw 'Link failed: hvm.exe' }

$cliObj = Compile-Source "$root\\tools\\hosc_cli.c" $cc $cFlags $inc
& $cc @cFlags @inc -o "$root\\tools\\bin\\hosc.exe" $cliObj @ld
if ($LASTEXITCODE -ne 0) { throw 'Link failed: hosc.exe' }

Write-Host 'Build complete:'
Write-Host "  $root\\tools\\bin\\hosc_compiler.exe"
Write-Host "  $root\\tools\\bin\\hvm.exe"
Write-Host "  $root\\tools\\bin\\hosc.exe"
