@echo off
setlocal
cd /d "%~dp0"
if not exist build\library.exe (
    call build.bat
    if errorlevel 1 exit /b 1
)
build\library.exe %*
if errorlevel 1 (
    echo Application stopped because of an error. See the message above.
    pause
    exit /b 1
)
