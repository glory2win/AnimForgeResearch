@echo off
rem Builds AnimForgeMayaWarpViz.mll via UnrealBuildTool.
rem
rem Prerequisites:
rem   * UE_ROOT      - engine root (folder containing Engine\)
rem   * MAYA_SDK     - Maya devkit root (contains include\ and lib\)
rem
rem Usage:  BuildMayaPlugin.bat [Development|Shipping]

setlocal

if "%UE_ROOT%"=="" (
    echo [error] Set UE_ROOT to the Unreal Engine root, e.g. C:\Epic\UE_5.4
    exit /b 1
)
if "%MAYA_SDK%"=="" (
    echo [error] Set MAYA_SDK to the Maya devkit root, e.g. C:\devkits\Maya2025\devkitBase
    exit /b 1
)

set CONFIG=%1
if "%CONFIG%"=="" set CONFIG=Development

set PROJECT_DIR=%~dp0..\..\..
for %%I in ("%PROJECT_DIR%") do set PROJECT_DIR=%%~fI

echo [build] AnimForgeMayaWarpViz %CONFIG% (project: %PROJECT_DIR%)
call "%UE_ROOT%\Engine\Build\BatchFiles\RunUBT.bat" AnimForgeMayaWarpViz Win64 %CONFIG% -Project="%PROJECT_DIR%\AnimForgeGym.uproject"
if errorlevel 1 (
    echo [error] UBT build failed.
    exit /b 1
)

set BIN_DIR=%PROJECT_DIR%\Binaries\Win64
if exist "%BIN_DIR%\AnimForgeMayaWarpViz.dll" (
    copy /y "%BIN_DIR%\AnimForgeMayaWarpViz.dll" "%BIN_DIR%\AnimForgeMayaWarpViz.mll" >nul
    echo [build] OK: %BIN_DIR%\AnimForgeMayaWarpViz.mll
    echo [build] Add %BIN_DIR% to MAYA_PLUG_IN_PATH and load the plugin in Maya.
) else (
    echo [warn] Build reported success but %BIN_DIR%\AnimForgeMayaWarpViz.dll was not found.
)

endlocal
