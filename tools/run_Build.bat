@echo off
setlocal

REM Get the directory where this script is located
set SCRIPT_DIR=%~dp0

REM Change to the tools directory
cd /d "%SCRIPT_DIR%"

REM Run the PowerShell script
powershell -ExecutionPolicy Bypass -File "build.ps1"

REM Pause to see the output
pause