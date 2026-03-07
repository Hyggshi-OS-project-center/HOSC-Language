$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $PSScriptRoot
$cc = 'gcc'

$compilerSrc = @(
  "$root\\compiler\\src\\hosc_compiler.c",
  "$root\\compiler\\src\\lexer.c",
  "$root\\compiler\\src\\parser.c",
  "$root\\compiler\\src\\arena.c",
  "$root\\compiler\\src\\codegen.c",
  "$root\\compiler\\src\\ast_utils.c",
  "$root\\runtime\\src\\hvm.c",
  "$root\\runtime\\src\\hvm_compiler.c"
)

$runtimeSrc = @(
  "$root\\runtime\\src\\hvm_runner.c",
  "$root\\runtime\\src\\hvm.c"
)

New-Item -ItemType Directory -Force -Path "$root\\tools\\bin" | Out-Null

$inc = @("-I$root\\compiler\\include", "-I$root\\runtime\\include")
$ld = @('-luser32','-lkernel32','-lgdi32','-lcomdlg32')

& $cc -Wall -Wextra -std=c99 -O2 @inc -o "$root\\tools\\bin\\hosc-compiler.exe" @compilerSrc @ld
& $cc -Wall -Wextra -std=c99 -O2 @inc -o "$root\\tools\\bin\\hvm.exe" @runtimeSrc @ld
& $cc -Wall -Wextra -std=c99 -O2 @inc -o "$root\\tools\\bin\\hosc.exe" "$root\\tools\\hosc_cli.c" @ld

Write-Host 'Build complete:'
Write-Host "  $root\\tools\\bin\\hosc-compiler.exe"
Write-Host "  $root\\tools\\bin\\hvm.exe"
Write-Host "  $root\\tools\\bin\\hosc.exe"
