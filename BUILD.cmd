@echo off
chcp 65001 >nul
setlocal EnableExtensions
cd /d "%~dp0"

echo =============================================
echo GitToolboxVN v1.3 - Build Release x64
echo =============================================

rem 1) Neu CMD hien tai da co MSVC (cl.exe) thi dung ngay.
where cl >nul 2>nul
if not errorlevel 1 goto :MSVC

rem 2) Neu chua co cl.exe, tu dong tim Visual Studio / Build Tools bang vswhere
rem    va nap x64 Native Tools environment. Day la ban .CMD nen bien FOR phai dung %%I.
call :TRY_MSVC
where cl >nul 2>nul
if not errorlevel 1 goto :MSVC

rem 3) Fallback sang MinGW-w64 neu co g++.exe trong PATH.
where g++ >nul 2>nul
if not errorlevel 1 goto :MINGW

echo.
echo KHONG TIM THAY COMPILER C++.
echo.
echo BUILD.cmd da tu dong thu tim Visual Studio Build Tools qua vswhere.exe nhung khong thay cl.exe.
echo.
echo Cach 1 - Visual Studio Build Tools:
echo   Cai workload "Desktop development with C++" / MSVC x64/x86 Build Tools.
echo   Sau do chi can chay lai BUILD.cmd tu CMD thuong.
echo.
echo Cach 2 - MinGW-w64:
echo   Them thu muc bin chua g++.exe vao PATH roi chay lai BUILD.cmd.
echo.
pause
exit /b 2

:TRY_MSVC
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
set "VSROOT="

if not exist "%VSWHERE%" exit /b 1

for /f "usebackq tokens=*" %%I in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSROOT=%%I"

if not defined VSROOT exit /b 1
if not exist "%VSROOT%\Common7\Tools\VsDevCmd.bat" exit /b 1

echo [MSVC] Tim thay: %VSROOT%
echo [MSVC] Dang nap x64 Native Tools...
call "%VSROOT%\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 >nul
exit /b %errorlevel%

:MSVC
echo [MSVC] Compiler:
cl 2>&1 | findstr /C:"Microsoft"
where cl

echo.
echo [MSVC] Dang build UTF-8 Release x64...
rem /utf-8: bat compiler doc source UTF-8, tranh loi font kieu "Táº¥t cáº£".
rem /MT: link CRT vao EXE de de mang GitToolboxVN.exe sang may Windows khac.
cl /nologo /O2 /MT /EHsc /std:c++17 /utf-8 /DUNICODE /D_UNICODE GitToolboxVN.cpp /Fe:GitToolboxVN.exe /link /SUBSYSTEM:WINDOWS user32.lib gdi32.lib shell32.lib ole32.lib
if errorlevel 1 goto :FAIL
goto :OK

:MINGW
echo [MinGW] Compiler:
where g++
g++ --version | findstr /N "." | findstr "^1:" 

echo.
echo [MinGW] Dang build UTF-8 Release x64...
rem -finput-charset=UTF-8: doc source UTF-8.
rem -static-libgcc/-static-libstdc++: giam phu thuoc DLL MinGW ben ngoai.
g++ -O2 -std=c++17 -finput-charset=UTF-8 -municode -mwindows -static-libgcc -static-libstdc++ GitToolboxVN.cpp -o GitToolboxVN.exe -lshell32 -lole32 -lgdi32
if errorlevel 1 goto :FAIL
goto :OK

:OK
echo.
echo =============================================
echo BUILD THANH CONG
echo =============================================
echo   %CD%\GitToolboxVN.exe
echo.
echo Neu ban dang mo ban EXE cu, hay dong no roi chay EXE vua build.
pause
exit /b 0

:FAIL
echo.
echo =============================================
echo BUILD THAT BAI
echo =============================================
echo Xem loi compiler phia tren.
pause
exit /b 1
