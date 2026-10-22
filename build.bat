@echo off
setlocal EnableDelayedExpansion

REM =======================================================================
REM 0. Keep Console Open Check (for Explorer double-click)
REM =======================================================================
if "%1"=="--no-pause" goto SkipKeepOpen
if "%1"=="-nopause" goto SkipKeepOpen

if not defined ZUPATHER_KEEP_OPEN (
    set "ZUPATHER_KEEP_OPEN=1"
    cmd /k call "%~f0" %*
    exit /b
)
:SkipKeepOpen

title ZuPather - Zuma Path Editor - Binary Builder
cd /d "%~dp0"

REM 1. Tutup aplikasi jika sedang berjalan agar file biner tidak terkunci
taskkill /f /im ZuPather.exe >nul 2>&1

echo =======================================================
echo    ZuPather - Zuma Path Editor - Binary Builder
echo    Target: .\ZuPather.exe (Single Unified Executable)
echo =======================================================
echo.

REM =======================================================================
REM [PRIORITY 1] Check Computer Specs & Active Drives
REM =======================================================================
set "SYS_ARCH=x64"
if "%PROCESSOR_ARCHITECTURE%"=="x86" (
    if not defined PROCESSOR_ARCHITEW6432 set "SYS_ARCH=x86"
)
if "%PROCESSOR_ARCHITECTURE%"=="ARM64" set "SYS_ARCH=ARM64"

set "TARGET_DRIVE=%SystemDrive%"
if "%TARGET_DRIVE%"=="" set "TARGET_DRIVE=C:"

echo [INFO] System Architecture : %SYS_ARCH%
echo [INFO] Target Binary       : ZuPather.exe
echo [INFO] System Drive        : %TARGET_DRIVE%
echo.

set "COMPILER_TYPE="
set "COMPILER_BIN="
set "VCVARS_BAT="

echo [INFO] Detecting active drives on this system...
set "ACTIVE_DRIVES="

REM 1. Coba fsutil fsinfo drives (Sangat cepat bawaan Windows NT)
for /f "tokens=1*" %%a in ('fsutil fsinfo drives 2^>nul') do (
    for %%d in (%%b) do (
        set "drv=%%d"
        set "drv=!drv:\=!"
        if not "!drv!"=="" (
            if exist "!drv!\" set "ACTIVE_DRIVES=!ACTIVE_DRIVES! !drv!"
        )
    )
)

REM 2. Fallback jika fsutil dibatasi: loop huruf drive
if "%ACTIVE_DRIVES%"=="" (
    for %%d in (C D E F G H I J K L M N O P Q R S T U V W X Y Z A B) do (
        if exist "%%d:\" set "ACTIVE_DRIVES=!ACTIVE_DRIVES! %%d:"
    )
)

REM Pastikan SystemDrive (%TARGET_DRIVE%) diperiksa di awal
if not "%TARGET_DRIVE%"=="" (
    set "SORTED_DRIVES=%TARGET_DRIVE%"
    for %%d in (!ACTIVE_DRIVES!) do (
        if /i not "%%d"=="%TARGET_DRIVE%" set "SORTED_DRIVES=!SORTED_DRIVES! %%d"
    )
    set "ACTIVE_DRIVES=!SORTED_DRIVES!"
)

echo [INFO] Active drives found : %ACTIVE_DRIVES%
echo [INFO] Scanning adaptively for available C compilers...
echo.

REM =======================================================================
REM [PRIORITY 2] Adaptive C/C++ Compiler Search
REM =======================================================================

REM --- 1. Cek MSVC via vswhere ---
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" set "VSWHERE=%ProgramFiles%\Microsoft Visual Studio\Installer\vswhere.exe"

if exist "%VSWHERE%" (
    for /f "usebackq delims=" %%i in (`"%VSWHERE%" -latest -products * -prerelease -property installationPath 2^>nul`) do (
        if exist "%%i\VC\Auxiliary\Build\vcvars32.bat" (
            set "VCVARS_BAT=%%i\VC\Auxiliary\Build\vcvars32.bat"
            set "COMPILER_TYPE=MSVC"
        ) else if exist "%%i\VC\Auxiliary\Build\vcvarsall.bat" (
            set "VCVARS_BAT=%%i\VC\Auxiliary\Build\vcvarsall.bat x86"
            set "COMPILER_TYPE=MSVC"
        )
    )
)

REM --- 2. Cek System PATH ---
if "!COMPILER_TYPE!"=="" (
    for %%c in (gcc.exe g++.exe clang.exe cl.exe) do (
        if "!COMPILER_TYPE!"=="" (
            where %%c >nul 2>nul
            if !ERRORLEVEL! equ 0 (
                for /f "usebackq delims=" %%W in (`where %%c 2^>nul`) do (
                    if "!COMPILER_BIN!"=="" (
                        set "COMPILER_BIN=%%W"
                        set "COMPILER_TYPE=GCC"
                    )
                )
            )
        )
    )
)

REM --- 3. Fast-Path Check (Active Drives) ---
if "!COMPILER_TYPE!"=="" (
    REM Prioritas A: Cek MinGW / MSYS2 lengkap dengan dukungan libwebp
    for %%d in (%ACTIVE_DRIVES%) do (
        if "!COMPILER_TYPE!"=="" (
            for %%p in (
                "msys64\mingw32\bin\gcc.exe"
                "msys64\ucrt64\bin\gcc.exe"
                "msys64\mingw64\bin\gcc.exe"
                "msys64\clang32\bin\clang.exe"
                "mingw32\bin\gcc.exe"
                "mingw64\bin\gcc.exe"
                "MinGW\bin\gcc.exe"
            ) do (
                if "!COMPILER_TYPE!"=="" (
                    if exist "%%d\%%~p" (
                        set "COMPILER_BIN=%%d\%%~p"
                        set "COMPILER_TYPE=GCC"
                    )
                )
            )
        )
    )

    REM Prioritas B: Toolchain lainnya
    if "!COMPILER_TYPE!"=="" (
        for %%d in (%ACTIVE_DRIVES%) do (
            if "!COMPILER_TYPE!"=="" (
                for %%p in (
                    "tools\w64devkit\bin\gcc.exe"
                    "tools\w64devkit\bin\g++.exe"
                    "apps-github\SkyRetail\v0.1\tools\w64devkit\bin\gcc.exe"
                    "apps-github\SkyRetail\v0.1\tools\w64devkit\bin\g++.exe"
                    "Program Files\CodeBlocks\MinGW\bin\gcc.exe"
                    "Program Files (x86)\CodeBlocks\MinGW\bin\gcc.exe"
                    "Program Files\LLVM\bin\clang.exe"
                    "TDM-GCC-64\bin\gcc.exe"
                    "Strawberry\c\bin\gcc.exe"
                    "w64devkit\bin\gcc.exe"
                ) do (
                    if "!COMPILER_TYPE!"=="" (
                        if exist "%%d\%%~p" (
                            set "COMPILER_BIN=%%d\%%~p"
                            set "COMPILER_TYPE=GCC"
                        )
                    )
                )

                REM Cek Subfolder 1 Level - WinLibs / Qt Tools
                if "!COMPILER_TYPE!"=="" (
                    for /d %%w in ("%%d\winlibs*" "%%d\Qt\Tools\mingw*") do (
                        if exist "%%w\bin\gcc.exe" if "!COMPILER_TYPE!"=="" (
                            set "COMPILER_BIN=%%w\bin\gcc.exe"
                            set "COMPILER_TYPE=GCC"
                        )
                    )
                )
            )
        )
    )
)

if not "!COMPILER_BIN!"=="" goto CompilerFound
if "!COMPILER_TYPE!"=="MSVC" goto CompilerFound

REM --- 3.5 Package Manager Tier (winget / choco / scoop) ---
if "!COMPILER_TYPE!"=="" (
    echo -------------------------------------------------------
    echo    [WARNING] No compiler found via PATH or common paths.
    echo -------------------------------------------------------
    set /p CONFIRM_INSTALL="[PROMPT] Try installing MinGW compiler automatically via winget/choco/scoop? (y/N): "
    if /i "!CONFIRM_INSTALL!"=="y" (
        set "PKG_MGR="
        where winget >nul 2>nul && set "PKG_MGR=winget"
        if "!PKG_MGR!"=="" (where choco >nul 2>nul && set "PKG_MGR=choco")
        if "!PKG_MGR!"=="" (where scoop >nul 2>nul && set "PKG_MGR=scoop")

        if "!PKG_MGR!"=="" (
            echo [SKIP] No package manager detected.
        ) else (
            echo [INFO] Detected package manager: !PKG_MGR!
            if "!PKG_MGR!"=="winget" (
                winget install --id BrechtSanders.WinLibs.POSIX.UCRT -e --silent --accept-source-agreements --accept-package-agreements
            ) else if "!PKG_MGR!"=="choco" (
                choco install mingw -y
            ) else (
                scoop install gcc
            )
            where gcc.exe >nul 2>nul && (set "COMPILER_BIN=gcc.exe" & set "COMPILER_TYPE=GCC")
        )
    )
)

if not "!COMPILER_TYPE!"=="" goto CompilerFound

REM --- 4. Deep-Scan Rekursif ---
echo [INFO] Fast-path missed. Executing DEEP SCAN on active drives...
for /f "usebackq delims=" %%f in (`powershell -NoProfile -Command "Get-PSDrive -PSProvider FileSystem | Where-Object { $_.Used -gt 0 -or $_.Free -gt 0 } | ForEach-Object { Get-ChildItem -Path $_.Root -Filter 'gcc.exe' -Recurse -ErrorAction SilentlyContinue } | Select-Object -First 1 -ExpandProperty FullName"`) do (
    if exist "%%f" (
        set "COMPILER_BIN=%%f"
        set "COMPILER_TYPE=GCC"
    )
)

if not "!COMPILER_TYPE!"=="" goto CompilerFound

REM --- 5. Portable Fallback Toolchain ---
echo [INFO] Downloading portable toolchain (w64devkit)...
if not exist "tools" mkdir "tools"
set "DL_FILE=%TARGET_DRIVE%\w64devkit_dl.zip"
powershell -NoProfile -ExecutionPolicy Bypass -Command "$ProgressPreference = 'SilentlyContinue'; [Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12; try { $rel = Invoke-RestMethod -Uri 'https://api.github.com/repos/skeeto/w64devkit/releases/latest' -Headers @{'User-Agent'='zupather-builder'}; $asset = $rel.assets | Where-Object { $_.name -like 'w64devkit-i686-*.zip' -or $_.name -like 'w64devkit-*.zip' } | Select-Object -First 1; Invoke-WebRequest -Uri $asset.browser_download_url -OutFile '%DL_FILE%'; Expand-Archive -Path '%DL_FILE%' -DestinationPath 'tools' -Force; Remove-Item '%DL_FILE%' -ErrorAction SilentlyContinue } catch { exit 1 }"
if exist "tools\w64devkit\bin\gcc.exe" (
    set "COMPILER_BIN=tools\w64devkit\bin\gcc.exe"
    set "COMPILER_TYPE=GCC"
)

:CompilerFound
if not "!COMPILER_BIN!"=="" (
    for %%F in ("!COMPILER_BIN!") do set "PATH=%%~dpF;!PATH!"
)
if "!COMPILER_TYPE!"=="MSVC" (
    echo [FOUND] MSVC Environment : "!VCVARS_BAT!"
) else if not "!COMPILER_BIN!"=="" (
    echo [FOUND] !COMPILER_TYPE! Compiler    : "!COMPILER_BIN!"
) else (
    echo [ERROR] No suitable C compiler could be identified!
    if "%1"=="--no-pause" exit /b 1
    if "%1"=="-nopause" exit /b 1
    pause
    exit /b 1
)
echo.

REM =======================================================================
REM Kompilasi ZuPather.exe dari Unified Modular Codebase
REM =======================================================================
echo -------------------------------------------------------
echo  Building ZuPather.exe (Unified Modular Codebase) ...
echo -------------------------------------------------------

del /f /q "ZuPather.exe" >nul 2>&1

if "!COMPILER_TYPE!"=="MSVC" (
    echo [COMPILING] via MSVC Clang/CL...
    call "!VCVARS_BAT!" >nul 2>&1
    set "RES_OBJ="
    del /f /q "res\zupather.res" >nul 2>&1
    if exist "res\zupather.rc" (
        rc.exe /nologo /fo res\zupather.res res\zupather.rc >nul 2>&1
        if exist "res\zupather.res" (
            set "RES_OBJ=res\zupather.res"
            echo [RESOURCE] Embedded res\zupather.res (assets\media\ZUpather.ico)
        )
    )
    cl /O2 /Fe:ZuPather.exe src\zupather_ui.c src\zupather_state.c src\undo_redo.c src\level_io.c src\dialog_modals.c src\hud_overlay.c src\canvas_render.c src\image_loader.c src\bezier_path.c src\svg_path.c src\level_progression.c !RES_OBJ! user32.lib gdi32.lib comdlg32.lib comctl32.lib msimg32.lib windowscodecs.lib ole32.lib shell32.lib uxtheme.lib dwmapi.lib mfplat.lib mfreadwrite.lib mfuuid.lib winmm.lib /link /SUBSYSTEM:WINDOWS
) else (
    echo [COMPILING] via GCC: "!COMPILER_BIN!"
    set "RES_OBJ="
    del /f /q "res\zupather.res.o" >nul 2>&1
    if exist "res\zupather.rc" (
        set "WINDRES_BIN=windres.exe"
        for %%F in ("!COMPILER_BIN!") do (
            if exist "%%~dpFwindres.exe" set "WINDRES_BIN=%%~dpFwindres.exe"
            set "PATH=%%~dpF;!PATH!"
        )
        echo [RESOURCE] Compiling res\zupather.rc via "!WINDRES_BIN!"...
        "!WINDRES_BIN!" -i res\zupather.rc -O coff -o res\zupather.res.o --include-dir="%~dp0"
        if exist "res\zupather.res.o" (
            set "RES_OBJ=res\zupather.res.o"
            echo [RESOURCE] Successfully embedded res\zupather.res.o (from assets\media\ZUpather.ico)
        ) else (
            echo [ERROR] Failed to compile icon resource res\zupather.rc!
            echo [ERROR] Please verify assets\media\ZUpather.ico exists.
            if "%1"=="--no-pause" exit /b 1
            if "%1"=="-nopause" exit /b 1
            pause
            exit /b 1
        )
    )
    "!COMPILER_BIN!" -O2 -mwindows -o "ZuPather.exe" src\zupather_ui.c src\zupather_state.c src\undo_redo.c src\level_io.c src\dialog_modals.c src\hud_overlay.c src\canvas_render.c src\image_loader.c src\bezier_path.c src\svg_path.c src\level_progression.c !RES_OBJ! -lwindowscodecs -lole32 -lshell32 -lmfplat -lmfreadwrite -lmfuuid -Wl,-Bstatic -lwebpmux -lwebpdemux -lwebp -lsharpyuv -Wl,-Bdynamic -lmsimg32 -lgdi32 -lcomdlg32 -lcomctl32 -luxtheme -ldwmapi -ladvapi32 -lwinmm -lm
)

if !ERRORLEVEL! neq 0 (
    echo.
    echo [ERROR] Compilation failed!
    if "%1"=="--no-pause" exit /b 1
    if "%1"=="-nopause" exit /b 1
    pause
    exit /b 1
)

if exist "ZuPather.exe" (
    for %%A in ("ZuPather.exe") do echo [READY] ZuPather.exe - %%~zA bytes
    echo.
    echo =======================================================
    echo [SUCCESS] ZuPather.exe successfully compiled!
    echo Location: %~dp0ZuPather.exe
    echo =======================================================
    REM Refresh Windows Explorer Icon Cache
    ie4uinit.exe -show >nul 2>&1
) else (
    echo.
    echo [ERROR] ZuPather.exe was not created!
    if "%1"=="--no-pause" exit /b 1
    if "%1"=="-nopause" exit /b 1
    pause
    exit /b 1
)

if "%1"=="--no-pause" goto Done
if "%1"=="-nopause" goto Done

echo.
pause

:Done
exit /b 0
