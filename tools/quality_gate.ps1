$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $PSScriptRoot
$buildScript = Join-Path $root 'tools\build.ps1'
$hosc = Join-Path $root 'tools\bin\hosc.exe'
$hvm = Join-Path $root 'tools\bin\hvm.exe'
$hvmHost = Join-Path $root 'tools\bin\hvm_host.exe'
$gateDir = Join-Path $root 'build\quality_gate'

function Assert-True([bool]$cond, [string]$message) {
    if (-not $cond) {
        throw $message
    }
}

function Invoke-Step([string]$name, [scriptblock]$action) {
    Write-Host "==> $name" -ForegroundColor Cyan
    & $action
    if ($LASTEXITCODE -ne 0) {
        throw "Step failed ($name) with exit code $LASTEXITCODE"
    }
}

New-Item -ItemType Directory -Force -Path $gateDir | Out-Null

Invoke-Step 'Build compiler/runtime/cli' {
    & $buildScript
}

Assert-True (Test-Path $hosc) "Missing binary: $hosc"
Assert-True (Test-Path $hvm) "Missing binary: $hvm"
Assert-True (Test-Path $hvmHost) "Missing binary: $hvmHost"

$versionOut = (& $hosc version 2>&1 | Out-String)
Assert-True ($LASTEXITCODE -eq 0) 'hosc version failed'
Assert-True ($versionOut -match 'HOSC 0\.2\.0') 'version output missing expected marker'

$helloSrc = @"
package main

func main() {
    print("Hello from quality gate")
}
"@

$helloFile = Join-Path $gateDir 'hello.hosc'
$helloHbc = Join-Path $gateDir 'hello.hbc'
Set-Content -Path $helloFile -Value $helloSrc -NoNewline -Encoding utf8
if (Test-Path $helloHbc) {
    Remove-Item $helloHbc -Force
}

Invoke-Step 'CLI check (valid source)' {
    & $hosc check $helloFile
}

Invoke-Step 'CLI build (valid source)' {
    & $hosc build $helloFile
}

Assert-True (Test-Path $helloHbc) "Expected output file was not created: $helloHbc"

$runOut = (& $hosc run $helloFile 2>&1 | Out-String)
Assert-True ($LASTEXITCODE -eq 0) 'hosc run failed on valid source'
Assert-True ($runOut -match 'Hello from quality gate') 'run output missing expected line'

$customHbc = Join-Path $gateDir 'custom-output.hbc'
if (Test-Path $customHbc) {
    Remove-Item $customHbc -Force
}

Invoke-Step 'CLI run with custom bytecode output' {
    & $hosc run $helloFile -o $customHbc
}

Assert-True (Test-Path $customHbc) "Expected custom bytecode output was not created: $customHbc"

$badSrc = @"
package main

print("missing function")
"@

$badFile = Join-Path $gateDir 'missing_main.hosc'
Set-Content -Path $badFile -Value $badSrc -NoNewline -Encoding utf8

$previousErrorActionPreference = $ErrorActionPreference
$ErrorActionPreference = 'Continue'
$badOut = (& $hosc check $badFile 2>&1 | Out-String)
$badExit = $LASTEXITCODE
$ErrorActionPreference = $previousErrorActionPreference
Assert-True ($badExit -ne 0) 'source without func main should fail check'
Assert-True ($badOut -match 'H001') 'missing main diagnostic should include H001'

Write-Host ''
Write-Host 'HOSC quality gate passed.' -ForegroundColor Green
Write-Host "Artifacts: $gateDir" -ForegroundColor DarkGray
