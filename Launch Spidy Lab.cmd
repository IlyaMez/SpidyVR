@echo off
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0tools\launch-lab.ps1"
if errorlevel 1 pause
