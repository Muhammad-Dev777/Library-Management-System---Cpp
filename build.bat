@echo off
setlocal
cd /d "%~dp0"
set "LIBRARY_COMPILER=g++"
if exist "C:\msys64\ucrt64\bin\g++.exe" (
    set "LIBRARY_COMPILER=C:\msys64\ucrt64\bin\g++.exe"
    set "PATH=C:\msys64\ucrt64\bin;%PATH%"
)
if not exist build mkdir build
"%LIBRARY_COMPILER%" -std=c++17 -Wall -Wextra -Wpedantic -O2 -static main.cpp -o build\library.exe
if errorlevel 1 (
    echo Build failed. Check that GCC is installed and close any running copy of library.exe.
    pause
    exit /b 1
)
echo Build succeeded. Start the application using run.bat.
exit /b 0
