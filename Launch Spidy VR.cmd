@echo off
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0tools\launch-game-vr.ps1" %*
set "SPIDY_EXIT=%ERRORLEVEL%"
if not "%SPIDY_EXIT%"=="0" (
    echo.
    echo Spidy VR stopped. The reason is printed above.
    pause
)
exit /b %SPIDY_EXIT%
