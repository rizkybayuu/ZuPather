@echo off
REM =======================================================================
REM ZuPather - Unified Build Entrypoint (Windows Default)
REM For direct Windows builds: call build_windows.bat
REM For macOS builds: run ./build_macos.sh
REM =======================================================================
call "%~dp0build_windows.bat" %*
exit /b %ERRORLEVEL%
