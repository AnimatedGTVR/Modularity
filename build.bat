@echo off
setlocal EnableDelayedExpansion

set "SCRIPT_HOME=%~dp0"
if "%SCRIPT_HOME:~-1%"=="\" set "SCRIPT_HOME=%SCRIPT_HOME:~0,-1%"

call :resolve_modularity_root
if errorlevel 1 (
    echo [ERROR] Could not find the Modularity source root. Set MODULARITY_ROOT or run from inside/above the Modularity folder.
    pause
    exit /b 1
)
cd /d "%SCRIPT_DIR%"

echo.
echo ================================
echo      Native Windows Build
echo ================================
echo.
echo [INFO] Modularity root: %SCRIPT_DIR%
echo.

git submodule update --init --recursive

set "START_TIME=%time%"
call :time_to_cs "%START_TIME%" START_CS

set "CLEAN_BUILD=0"
set "BUILD_TYPE=Release"
set "BUILD_CPP_STANDARD=c++23"
set "PACKAGE_FORMAT=7Z"
set "PACKAGE_EXT=7z"
for %%A in (%*) do (
    if /I "%%~A"=="--clean" (
        set "CLEAN_BUILD=1"
    ) else if /I "%%~A"=="--zip" (
        set "PACKAGE_FORMAT=ZIP"
        set "PACKAGE_EXT=zip"
    ) else if /I "%%~A"=="--7z" (
        set "PACKAGE_FORMAT=7Z"
        set "PACKAGE_EXT=7z"
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

echo [INFO] Configuring with CMake (Visual Studio 18 2026, %BUILD_CPP_STANDARD%)...
cmake -A x64 .. -DCMAKE_BUILD_TYPE=%BUILD_TYPE% -DMODULARITY_BUILD_CPP_STANDARD=%BUILD_CPP_STANDARD% %MONO_ROOT_ARG%

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
for /r %%F in (*.lib) do call :copy_thirdparty_lib "%%F" "Packages\\ThirdParty"
for /r %%F in (*.dll) do call :copy_if_not_packages "%%F" "Packages\\ThirdParty"
if not exist "Packages\\Engine" mkdir "Packages\\Engine"
for /r %%F in (core*.dll) do call :copy_if_not_packages "%%F" "Packages\\Engine"

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
cmake -S . -B "%PLAYER_CACHE_DIR%" -DMODULARITY_BUILD_EDITOR=OFF -DCMAKE_BUILD_TYPE=%BUILD_TYPE% -DMODULARITY_BUILD_CPP_STANDARD=%BUILD_CPP_STANDARD% %MONO_ROOT_ARG%
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
for /r %%F in (*.lib) do call :copy_thirdparty_lib "%%F" "Packages\\ThirdParty"
for /r %%F in (*.dll) do call :copy_if_not_packages "%%F" "Packages\\ThirdParty"
if not exist "Packages\\Engine" mkdir "Packages\\Engine"
for /r %%F in (core*.dll) do call :copy_if_not_packages "%%F" "Packages\\Engine"
popd

echo [INFO] Producing distribution archive (CPack %PACKAGE_FORMAT%)...
pushd build
cpack -G %PACKAGE_FORMAT% -C %BUILD_TYPE%
set "CPACK_RESULT=%errorlevel%"
popd
if not "%CPACK_RESULT%"=="0" (
    echo [WARN] CPack failed (exit %CPACK_RESULT%). Distribution archive was not produced.
)

call :time_to_cs "%time%" END_CS
set /a "DUR_CS=END_CS-START_CS"
if !DUR_CS! lss 0 set /a "DUR_CS+=8640000"
set /a "DUR_SEC=DUR_CS/100"
set /a "DUR_REM=DUR_CS%%100"
if !DUR_REM! lss 10 set "DUR_REM=0!DUR_REM!"

echo.
echo =========================================
echo   SUCCESS! Native Windows Build Complete in !DUR_SEC!.!DUR_REM!s!
echo   Editor:        build\%BUILD_TYPE%\Modularity.exe
echo   Player:        build\%BUILD_TYPE%\ModularityPlayer.exe
echo   Distribution:  build\Modularity-1.0.0-Windows.%PACKAGE_EXT%
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

:: Same as :copy_if_not_packages but also skips engine static libs and
:: per-target import libs that are link-time only (not needed at runtime
:: and individually huge — core.lib / core_player.lib are ~100 MB each).
:copy_thirdparty_lib
set "SRC=%~1"
set "DEST=%~2"
set "BASE=%~nx1"
echo %SRC% | findstr /I /C:"\\Packages\\" >nul
if not errorlevel 1 exit /b 0
if /I "%BASE%"=="core.lib"           exit /b 0
if /I "%BASE%"=="core_player.lib"    exit /b 0
if /I "%BASE%"=="Modularity.lib"     exit /b 0
if /I "%BASE%"=="ModularityPlayer.lib" exit /b 0
if /I "%BASE%"=="glad.lib"           exit /b 0
if /I "%BASE%"=="imgui.lib"          exit /b 0
if /I "%BASE%"=="imguizmo.lib"       exit /b 0
copy /Y "%SRC%" "%DEST%" >nul
exit /b 0

:resolve_modularity_root
if defined MODULARITY_ROOT (
    call :try_modularity_root "%MODULARITY_ROOT%"
    if not errorlevel 1 exit /b 0
)

call :search_modularity_root_up "%SCRIPT_HOME%"
if not errorlevel 1 exit /b 0

call :search_modularity_root_up "%CD%"
if not errorlevel 1 exit /b 0

exit /b 1

:search_modularity_root_up
set "SEARCH_DIR=%~f1"
:search_modularity_root_loop
call :try_modularity_root "!SEARCH_DIR!"
if not errorlevel 1 exit /b 0
call :try_modularity_root "!SEARCH_DIR!\Modularity"
if not errorlevel 1 exit /b 0
for %%I in ("!SEARCH_DIR!\..") do set "PARENT_DIR=%%~fI"
if /I "!PARENT_DIR!"=="!SEARCH_DIR!" exit /b 1
set "SEARCH_DIR=!PARENT_DIR!"
goto search_modularity_root_loop

:try_modularity_root
set "CANDIDATE_ROOT=%~f1"
if exist "%CANDIDATE_ROOT%\CMakeLists.txt" if exist "%CANDIDATE_ROOT%\src" if exist "%CANDIDATE_ROOT%\Resources" (
    findstr /C:"project(Modularity" "%CANDIDATE_ROOT%\CMakeLists.txt" >nul 2>&1
    if not errorlevel 1 (
        set "SCRIPT_DIR=%CANDIDATE_ROOT%"
        exit /b 0
    )
)
exit /b 1

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
