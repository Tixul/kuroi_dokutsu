@echo off
cls

REM ============================================================
REM  NgpCraft_base_template - Build script (Windows)
REM  MIT License
REM
REM  Flash save uses standalone AMD stubs (no system.lib needed).
REM  See build_official_lib.bat if you want the system.lib path.
REM
REM  CONFIGURATION: Edit the paths below to match your setup.
REM ============================================================

REM --- Toolchain path (Toshiba cc900 compiler) ---
SET compilerPath=C:\t900

REM --- Flash save (standalone -- no system.lib required) ---
REM     0 = disabled (default)   1 = enabled
REM     MKD SAVE-2 (2026-05-17) : flash save active pour persister bonus
REM     accumules + niveaux unlocked entre sessions. Reset complet a la mort.
SET FlashSave=1

REM --- ROM name (also update CartTitle in src/core/carthdr.h) ---
SET romName=kuroi_dokutsu

REM --- Pad ROM to 2MB for flash carts? (1=yes, 0=no) ---
SET ResizeRom=1

REM --- Launch emulator after build? (1=yes, 0=no) ---
SET Run=1

REM --- Emulator executable path ---
SET emuPath="C:\emu\NeoPop\NeoPop-Win32.exe"

REM ============================================================
REM  BUILD (do not edit below unless you know what you are doing)
REM ============================================================

SET THOME=%compilerPath%
SET BinPath=bin
SET romExt=ngc
SET rootPath=%~dp0
SET MakeCmd=%THOME%\BIN\GMAKE.EXE
SET PythonCmd=%LOCALAPPDATA%\Programs\Python\Python311\python.exe
IF NOT EXIST "%PythonCmd%" SET PythonCmd=py -3
path=%path%;%THOME%\bin

echo [NgpCraft_base_template] Building...

if "%FlashSave%"=="1" (
    "%MakeCmd%" -f makefile clean PYTHON="%PythonCmd%" NGP_ENABLE_FLASH_SAVE=1
    "%MakeCmd%" -f makefile PYTHON="%PythonCmd%" NGP_ENABLE_FLASH_SAVE=1
    "%MakeCmd%" -f makefile move_files PYTHON="%PythonCmd%" NGP_ENABLE_FLASH_SAVE=1
) else (
    "%MakeCmd%" -f makefile clean PYTHON="%PythonCmd%"
    "%MakeCmd%" -f makefile PYTHON="%PythonCmd%"
    "%MakeCmd%" -f makefile move_files PYTHON="%PythonCmd%"
)

if "%ResizeRom%"=="1" (
    if exist "%~dp0utils\NGPRomResize.exe" (
        echo [NgpCraft_base_template] Resizing ROM to 2MB...
        MOVE "%~dp0%BinPath%\%romName%.%romExt%" "%~dp0%BinPath%\_%romName%.%romExt%" >nul 2>&1
        "%~dp0utils\NGPRomResize.exe" "%~dp0%BinPath%\_%romName%.%romExt%"
        MOVE "%~dp0%BinPath%\_%romName%.%romExt%" "%~dp0%BinPath%\%romName%.%romExt%" >nul 2>&1
        DEL "%~dp0%BinPath%MB__%romName%.%romExt%" >nul 2>&1
    ) else (
        echo [NgpCraft_base_template] Skip resize: utils\NGPRomResize.exe not found.
    )
)

if "%Run%"=="1" (
    if exist %emuPath% (
        echo [NgpCraft_base_template] Launching emulator...
        %emuPath% "%rootPath:~0,-1%.\%BinPath%\%romName%.%romExt%"
    ) else (
        echo [NgpCraft_base_template] Skip run: emulator not found at %emuPath%.
    )
)

echo [NgpCraft_base_template] Done.
