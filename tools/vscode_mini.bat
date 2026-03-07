@echo off
setlocal
set SCRIPT=%~dp0vscode_mini.py

if not exist "%SCRIPT%" (
  echo File not found: %SCRIPT%
  exit /b 1
)

where py >nul 2>nul
if %errorlevel%==0 (
  py "%SCRIPT%"
  exit /b %errorlevel%
)

where python >nul 2>nul
if %errorlevel%==0 (
  python "%SCRIPT%"
  exit /b %errorlevel%
)

echo Python was not found in PATH.
echo Install Python 3 and run again.
exit /b 1
