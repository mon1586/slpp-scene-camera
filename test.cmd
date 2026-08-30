@echo off
call "%~dp0build.cmd" -TestsOnly %*
exit /b %ERRORLEVEL%
