@echo off
cd /d "%~dp0"
if not exist GitToolboxVN.exe (
  echo Chua co GitToolboxVN.exe. Hay chay BUILD.cmd truoc.
  pause
  exit /b 1
)
start "" "%~dp0GitToolboxVN.exe"
