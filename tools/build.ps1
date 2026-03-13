$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $PSScriptRoot
$cc = 'gcc'

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

$cppApiSrc = "$root\runtime\src\hosc_cpp_api.cpp"
$cppObjDir = "$root\build\obj"
$cppObj = "$cppObjDir\hosc_cpp_api.o"

New-Item -ItemType Directory -Force -Path "$root\tools\bin" | Out-Null
New-Item -ItemType Directory -Force -Path $cppObjDir | Out-Null

$inc = @("-I$root\compiler\include", "-I$root\runtime\include")
$ld = @('-luser32','-lkernel32','-lgdi32','-lcomdlg32')

& $cc -Wall -Wextra -std=c++17 -O2 @inc -x c++ -c $cppApiSrc -o $cppObj

& $cc -Wall -Wextra -std=c99 -O2 @inc -o "$root\tools\bin\hosc-compiler.exe" @compilerSrc $cppObj @ld -lstdc++
& $cc -Wall -Wextra -std=c99 -O2 @inc -o "$root\tools\bin\hvm.exe" @runtimeSrc $cppObj @ld -lstdc++
& $cc -Wall -Wextra -std=c99 -O2 @inc -o "$root\tools\bin\hosc.exe" "$root\tools\hosc_cli.c" @ld

Write-Host 'Build complete:'
Write-Host "  $root\tools\bin\hosc-compiler.exe"
Write-Host "  $root\tools\bin\hvm.exe"
Write-Host "  $root\tools\bin\hosc.exe"
