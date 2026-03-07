$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $PSScriptRoot
$buildScript = Join-Path $root 'tools\build.ps1'
$hosc = Join-Path $root 'tools\bin\hosc.exe'
$compiler = Join-Path $root 'tools\bin\hosc-compiler.exe'
$hvm = Join-Path $root 'tools\bin\hvm.exe'
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

function Match-Line([string]$text, [string]$line) {
    return ($text -match "(?m)^\s*$([regex]::Escape($line))\s*$")
}

New-Item -ItemType Directory -Force -Path $gateDir | Out-Null

Invoke-Step 'Build compiler/runtime/cli' {
    & $buildScript
}

Assert-True (Test-Path $hosc) "Missing binary: $hosc"
Assert-True (Test-Path $compiler) "Missing binary: $compiler"
Assert-True (Test-Path $hvm) "Missing binary: $hvm"

$versionOut = (& $hosc --version 2>&1 | Out-String)
Assert-True ($LASTEXITCODE -eq 0) 'hosc --version failed'
Assert-True ($versionOut -match '(?i)hosc\s+\d') 'version output missing expected marker'

$goodSrc = @"
package main

func main() {
    var i = 0
    while i < 3 {
        print(i)
        i = i + 1
    }
}
"@

$goodFile = Join-Path $gateDir 'good.hosc'
$goodHbc = Join-Path $gateDir 'good.hbc'
Set-Content -Path $goodFile -Value $goodSrc -NoNewline -Encoding utf8

Invoke-Step 'CLI check (valid source)' {
    & $hosc check $goodFile
}

Invoke-Step 'CLI build (valid source)' {
    & $hosc build $goodFile -o $goodHbc
}

Assert-True (Test-Path $goodHbc) "Expected output file was not created: $goodHbc"

$runOut = (& $hosc run $goodFile -o $goodHbc 2>&1 | Out-String)
Assert-True ($LASTEXITCODE -eq 0) 'hosc run failed on valid source'
Assert-True (Match-Line $runOut '0') 'run output missing line: 0'
Assert-True (Match-Line $runOut '1') 'run output missing line: 1'
Assert-True (Match-Line $runOut '2') 'run output missing line: 2'

$badStringSrc = @"
package main

func main() {
    var s = "unterminated
    print(s)
}
"@

$badFile = Join-Path $gateDir 'bad_unterminated_string.hosc'
$badHbc = Join-Path $gateDir 'bad_unterminated_string.hbc'
Set-Content -Path $badFile -Value $badStringSrc -NoNewline -Encoding utf8
if (Test-Path $badHbc) {
    Remove-Item $badHbc -Force
}

$badCmd = '"{0}" build "{1}" -o "{2}"' -f $hosc, $badFile, $badHbc
$badOut = (& cmd /c "$badCmd 2>&1" | Out-String)
$badExit = $LASTEXITCODE
Assert-True ($badExit -ne 0) 'unterminated string should fail build'
Assert-True (-not (Test-Path $badHbc)) 'failed build should not create bytecode output'
Assert-True ($badOut -match '(?i)(lexer|unterminated|string literal)') 'unterminated string did not report lexer/string failure'

$fmtSrc = "package main`r`nfunc main() {`tprint(1)    `r`n}"
$fmtFile = Join-Path $gateDir 'fmt_dirty.hosc'
Set-Content -Path $fmtFile -Value $fmtSrc -NoNewline -Encoding utf8

$fmtCheckCmd = '"{0}" fmt "{1}" --check' -f $hosc, $fmtFile
& cmd /c "$fmtCheckCmd >nul 2>&1"
$fmtCheckExit = $LASTEXITCODE
Assert-True ($fmtCheckExit -ne 0) 'fmt --check should fail on dirty file'

Invoke-Step 'CLI fmt (rewrite)' {
    & $hosc fmt $fmtFile
}

Invoke-Step 'CLI fmt --check (clean)' {
    & $hosc fmt $fmtFile --check
}

Write-Host ''
Write-Host 'HOSC quality gate passed.' -ForegroundColor Green
Write-Host "Artifacts: $gateDir" -ForegroundColor DarkGray
