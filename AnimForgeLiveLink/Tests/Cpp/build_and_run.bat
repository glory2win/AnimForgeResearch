@echo off
rem Builds and runs the standalone AnimForgeWarpVizShared tests.
rem Tries MSVC (cl), then clang++, then g++.

setlocal
set HERE=%~dp0
set SHARED=%HERE%..\..\UnrealGymProject\Shared\AnimForgeWarpVizShared
set OUT=%HERE%warpviz_tests.exe

where cl >nul 2>nul
if %errorlevel%==0 (
    echo [build] using MSVC cl
    pushd "%HERE%"
    cl /nologo /std:c++14 /EHsc /W4 /I"%SHARED%" TestMain.cpp "%SHARED%\WarpVizJson.cpp" "%SHARED%\WarpVizProtocol.cpp" /Fe:"%OUT%"
    popd
    goto :run
)

where clang++ >nul 2>nul
if %errorlevel%==0 (
    echo [build] using clang++
    clang++ -std=c++14 -Wall -I"%SHARED%" "%HERE%TestMain.cpp" "%SHARED%\WarpVizJson.cpp" "%SHARED%\WarpVizProtocol.cpp" -o "%OUT%"
    goto :run
)

where g++ >nul 2>nul
if %errorlevel%==0 (
    echo [build] using g++
    g++ -std=c++14 -Wall -I"%SHARED%" "%HERE%TestMain.cpp" "%SHARED%\WarpVizJson.cpp" "%SHARED%\WarpVizProtocol.cpp" -o "%OUT%"
    goto :run
)

echo [error] No C++ compiler found (need cl, clang++ or g++ on PATH).
echo         From a "x64 Native Tools Command Prompt for VS" this just works.
exit /b 2

:run
if not exist "%OUT%" (
    echo [error] Build failed.
    exit /b 1
)
"%OUT%"
exit /b %errorlevel%
