@echo off
setlocal EnableDelayedExpansion

title "ZuPather - Unit and Regression Test Suite"
cd /d "%~dp0.."

echo =======================================================
echo    ZuPather - Unit ^& Regression Test Runner
echo =======================================================
echo.

set "COMPILER_BIN=C:\msys64\mingw32\bin\gcc.exe"
if not exist "!COMPILER_BIN!" (
    for %%X in (gcc.exe) do set "COMPILER_BIN=%%~$PATH:X"
)

if not exist "!COMPILER_BIN!" (
    echo [ERROR] GCC compiler not found!
    exit /b 1
)

for %%F in ("!COMPILER_BIN!") do set "PATH=%%~dpF;!PATH!"

echo [COMPILING] tests\test_layout_roundtrip.c via "!COMPILER_BIN!"...
"!COMPILER_BIN!" -O2 -o "tests\test_layout_roundtrip.exe" tests\test_layout_roundtrip.c -lgdi32 -lcomctl32 -lmsimg32 -luxtheme
if !ERRORLEVEL! neq 0 (
    echo [ERROR] Failed to compile test suite!
    exit /b 1
)

echo [RUNNING] tests\test_layout_roundtrip.exe...
echo.
"tests\test_layout_roundtrip.exe"
set "TEST_EXIT=!ERRORLEVEL!"

if exist "tests\test_layout_roundtrip.exe" del /f /q "tests\test_layout_roundtrip.exe" >nul 2>&1

exit /b !TEST_EXIT!
