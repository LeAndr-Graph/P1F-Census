@echo off
REM Build p1f (Release^|x64): locate MSBuild via vswhere, then build the sln.
REM
REM  Started from Explorer, this bat owns its console window and closing it takes the result
REM  with it -- so every way out of here pauses first, the three failures included, since a
REM  failed build is the one whose message you actually need to read.  Pass "nopause" to skip
REM  it: that is the switch a script wants.
cd /d "%~dp0"

SET nopause=0
:parse
IF "%~1" == "" GOTO end_parse
IF /I "%~1" == "nopause" (
    SET nopause=1
)
SHIFT
GOTO parse
:end_parse

set VSWHERE="%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist %VSWHERE% (
    echo "Error: vswhere not found. Please install Visual Studio."
    if %nopause% == 0 pause
    exit /b 1
)
for /f "usebackq tokens=*" %%i in (`%VSWHERE% -latest -products * -requires Microsoft.Component.MSBuild -find MSBuild\**\Bin\MSBuild.exe`) do (
  set MSBUILD="%%i"
)
if not defined MSBUILD (
    echo "Error: MSBuild not found."
    if %nopause% == 0 pause
    exit /b 1
)
echo Building p1f.sln...
%MSBUILD% p1f.sln /p:Configuration=Release /p:Platform=x64
if %ERRORLEVEL% neq 0 (
    echo "Build failed."
    if %nopause% == 0 pause
    exit /b 1
)
echo "Build successful."
if %nopause% == 0 pause
exit /b %ERRORLEVEL%
