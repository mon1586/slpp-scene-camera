@echo off
setlocal
set "SSC_SAVED_PATH=%PATH%"
set "PATH="
set "Path="
set "Path=%SSC_SAVED_PATH%"

where pwsh.exe >nul 2>nul
if %ERRORLEVEL% EQU 0 (
    pwsh.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0scripts\build.ps1" %*
) else (
    powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0scripts\build.ps1" %*
)
exit /b %ERRORLEVEL%
