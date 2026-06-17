@echo off
setlocal enabledelayedexpansion

echo ========================================
echo   WindowsDynamicIsland — Build Check
echo ========================================
echo.

:: Trouver vswhere
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" (
    set "VSWHERE=%ProgramFiles%\Microsoft Visual Studio\Installer\vswhere.exe"
)

if not exist "%VSWHERE%" (
    echo [ERREUR] vswhere.exe introuvable.
    echo Assurez-vous que Visual Studio 2022 est installe.
    pause & exit /b 1
)

:: Trouver MSBuild
for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -requires Microsoft.Component.MSBuild -find MSBuild\**\Bin\MSBuild.exe`) do (
    set "MSBUILD=%%i"
)

if not defined MSBUILD (
    echo [ERREUR] MSBuild introuvable. Installez "Developpement Desktop C++" dans VS.
    pause & exit /b 1
)

echo [OK] MSBuild trouve : %MSBUILD%
echo.

:: Verifier le Windows SDK
set "WINSDK_ROOT=%ProgramFiles(x86)%\Windows Kits\10\Include"
if not exist "%WINSDK_ROOT%" (
    set "WINSDK_ROOT=%ProgramFiles%\Windows Kits\10\Include"
)
if exist "%WINSDK_ROOT%" (
    echo [OK] Windows SDK trouve : %WINSDK_ROOT%
) else (
    echo [AVERT] Windows SDK 10 non trouve — des erreurs de compilation sont possibles.
)

:: Verifier gestureapi.h
set "GESTURE_H="
for /d %%v in ("%WINSDK_ROOT%\*") do (
    if exist "%%v\um\gestureapi.h" set "GESTURE_H=%%v\um\gestureapi.h"
)
if defined GESTURE_H (
    echo [OK] gestureapi.h trouve : %GESTURE_H%
) else (
    echo [AVERT] gestureapi.h introuvable dans le SDK — verifiez la version du SDK.
)

echo.
echo ----------------------------------------
echo   Lancement de la build Debug x64...
echo ----------------------------------------
echo.

cd /d "%~dp0"

"%MSBUILD%" WindowsDynamicIsland.vcxproj ^
    /p:Configuration=Debug ^
    /p:Platform=x64 ^
    /m ^
    /nologo ^
    /v:minimal ^
    /flp:logfile=build_errors.log;errorsonly

if %ERRORLEVEL% == 0 (
    echo.
    echo ========================================
    echo   [SUCCESS] Build reussie !
    echo   Binaire : x64\Debug\WindowsDynamicIsland.exe
    echo ========================================
) else (
    echo.
    echo ========================================
    echo   [ECHEC] Des erreurs de compilation.
    echo   Voir build_errors.log pour details.
    echo ========================================
    echo.
    echo --- Erreurs ---
    type build_errors.log
)

echo.
pause
