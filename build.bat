@echo off
setlocal EnableDelayedExpansion

set "SCRIPT_DIR=%~dp0"
if "%SCRIPT_DIR:~-1%"=="\" set "SCRIPT_DIR=%SCRIPT_DIR:~0,-1%"

echo.
echo ================================
echo      Native Windows Build
echo ================================
echo.

git submodule update --init --recursive

set "START_TIME=%time%"
call :time_to_cs "%START_TIME%" START_CS

set "CLEAN_BUILD=0"
set "BUILD_TYPE=Release"
for %%A in (%*) do (
    if /I "%%~A"=="--clean" (
        set "CLEAN_BUILD=1"
    ) else (
        set "ARG=%%~A"
        if /I "!ARG:~0,13!"=="--build-type=" set "BUILD_TYPE=!ARG:~13!"
    )
)

where ccache >nul 2>&1
if %errorlevel%==0 (
    set "CCACHE_BASEDIR=%SCRIPT_DIR%"
    set "CCACHE_NOHASHDIR=1"
    echo [INFO] ccache detected. Normalizing paths for cross-build cache hits.
)

set "MONO_ROOT_ARG="
if defined MONO_ROOT set "MONO_ROOT_ARG=-DMONO_ROOT=%MONO_ROOT%"

:: Clean old build (optional)
if exist build if %CLEAN_BUILD%==1 (
    echo [INFO] Cleaning existing build directory...
    rmdir /s /q build
)

echo [INFO] Creating fresh build directory...
if not exist build mkdir build
pushd build

echo [INFO] Configuring with CMake (Visual Studio 18 2026)...
cmake -A x64 .. -DCMAKE_BUILD_TYPE=%BUILD_TYPE% %MONO_ROOT_ARG%

if errorlevel 1 (
    echo.
    echo [ERROR] CMake configuration failed!
    pause
    exit /b 1
)

echo [INFO] Building %BUILD_TYPE% (using all CPU cores)...
cmake --build . --config %BUILD_TYPE% -- /m

if errorlevel 1 (
    echo.
    echo [ERROR] Build failed!
    pause
    exit /b 1
)

echo [INFO] Collecting package binaries...
if not exist "Packages\\ThirdParty" mkdir "Packages\\ThirdParty"
for /r %%F in (*.lib) do call :copy_if_not_packages "%%F" "Packages\\ThirdParty"
for /r %%F in (*.dll) do call :copy_if_not_packages "%%F" "Packages\\ThirdParty"
if not exist "Packages\\Engine" mkdir "Packages\\Engine"
for /r %%F in (core*.lib core*.dll) do call :copy_if_not_packages "%%F" "Packages\\Engine"

echo [INFO] Copying Resources...
xcopy /e /i /y "..\Resources" "Resources\" >nul
copy /Y Resources\imgui.ini .

popd

set "PLAYER_CACHE_DIR=build\\player-cache"
if exist "%PLAYER_CACHE_DIR%" if %CLEAN_BUILD%==1 (
    echo [INFO] Cleaning player cache build directory...
    rmdir /s /q "%PLAYER_CACHE_DIR%"
)

if not exist "%PLAYER_CACHE_DIR%" mkdir "%PLAYER_CACHE_DIR%"
echo [INFO] Configuring player cache build...
cmake -S . -B "%PLAYER_CACHE_DIR%" -DMODULARITY_BUILD_EDITOR=OFF -DCMAKE_BUILD_TYPE=%BUILD_TYPE% %MONO_ROOT_ARG%
if errorlevel 1 (
    echo.
    echo [ERROR] Player cache CMake configuration failed!
    pause
    exit /b 1
)

echo [INFO] Building ModularityPlayer (%BUILD_TYPE%)...
cmake --build "%PLAYER_CACHE_DIR%" --config %BUILD_TYPE% --target ModularityPlayer -- /m
if errorlevel 1 (
    echo.
    echo [ERROR] ModularityPlayer build failed!
    pause
    exit /b 1
)

pushd "%PLAYER_CACHE_DIR%"
echo [INFO] Collecting player package binaries...
if not exist "Packages\\ThirdParty" mkdir "Packages\\ThirdParty"
for /r %%F in (*.lib) do call :copy_if_not_packages "%%F" "Packages\\ThirdParty"
for /r %%F in (*.dll) do call :copy_if_not_packages "%%F" "Packages\\ThirdParty"
if not exist "Packages\\Engine" mkdir "Packages\\Engine"
for /r %%F in (core*.lib core*.dll) do call :copy_if_not_packages "%%F" "Packages\\Engine"
popd

call :time_to_cs "%time%" END_CS
set /a "DUR_CS=END_CS-START_CS"
if !DUR_CS! lss 0 set /a "DUR_CS+=8640000"
set /a "DUR_SEC=DUR_CS/100"
set /a "DUR_REM=DUR_CS%%100"
if !DUR_REM! lss 10 set "DUR_REM=0!DUR_REM!"

echo.
echo =========================================
echo   SUCCESS! Native Linux Build Complete in !DUR_SEC!.!DUR_REM!s!
echo   At build\%BUILD_TYPE%\main.exe
echo =========================================
echo.
pause

goto :eof

:copy_if_not_packages
set "SRC=%~1"
set "DEST=%~2"
echo %SRC% | findstr /I /C:"\\Packages\\" >nul
if errorlevel 1 (
    copy /Y "%SRC%" "%DEST%" >nul
)
exit /b 0

:time_to_cs
set "T=%~1"
for /f "tokens=1-4 delims=:.," %%a in ("%T%") do (
    set /a "hh=1%%a-100"
    set /a "mm=1%%b-100"
    set /a "ss=1%%c-100"
    set /a "cc=1%%d-100"
)
set /a "total=(hh*3600 + mm*60 + ss)*100 + cc"
set "%~2=%total%"
exit /b 0
