@echo off
setlocal

rem Build the chameleonEsp solution in Release^|x64.
rem Usage: build.bat [rebuild]   (pass "rebuild" to force a clean rebuild)

set "SOLUTION=%~dp0chameleonEsp.slnx"
set "CONFIG=Release"
set "PLATFORM=x64"

set "TARGET=Build"
if /i "%~1"=="rebuild" set "TARGET=Rebuild"

rem Locate MSBuild via vswhere (shipped with VS 2017+).
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" (
    echo [error] vswhere.exe not found. Is Visual Studio installed?
    exit /b 1
)

for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -requires Microsoft.Component.MSBuild -find MSBuild\**\Bin\MSBuild.exe`) do (
    set "MSBUILD=%%i"
)

if not defined MSBUILD (
    echo [error] MSBuild.exe not found via vswhere.
    exit /b 1
)

echo Using MSBuild: %MSBUILD%
echo Target: %TARGET%  Config: %CONFIG%^|%PLATFORM%
echo.

"%MSBUILD%" "%SOLUTION%" /t:%TARGET% /p:Configuration=%CONFIG% /p:Platform=%PLATFORM% /m /nologo /v:minimal
if errorlevel 1 (
    echo.
    echo [error] Build failed.
    exit /b 1
)

echo.
echo [ok] Build succeeded. Output: %~dp0chameleonEsp\%PLATFORM%\%CONFIG%\
endlocal
