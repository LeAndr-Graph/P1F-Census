@echo off
REM ============================================================================
REM  runs\K22\K22AutV4 -- K22 P1Fs whose automorphism group contains a Klein
REM  four-group V4 (REP_ORDERS=V4, see ../ReadMe.md), shapes 1 and 3 only
REM  (REP_ONLYTYPES=1,3): fx2,2,2 F4 and fx2,2,2 F5, 0 classes each, ~5 min together.
REM  Shape 2, fx2,0,0 F5, is NOT searched here: months (runs\K22\K22AutV4split).
REM  V4 is not part of the |Aut| > 6 census (../ReadMe.md); this case records shapes 1, 3.
REM  The old log of 2026-10-26 (stopped inside shape 2) is K22AutV4_2026-10-26.log.
REM
REM  The log is refused if it already exists -- delete it deliberately when
REM  you mean to run again.
REM ============================================================================
SET "HERE=%~dp0"
cd /d "%HERE%"
setlocal
IF NOT DEFINED P1F_ROOT FOR %%I IN ("%HERE%..\..\..") DO SET "P1F_ROOT=%%~fI"
IF NOT DEFINED P1F_EXE  SET "P1F_EXE=%P1F_ROOT%\x64\Release\p1f.exe"
CALL "%P1F_ROOT%\env_reset.bat"

SET "CASE=K22AutV4"
SET "EXE=%P1F_EXE%"
SET "LOG=%HERE%%CASE%.log"
IF EXIST "%LOG%" echo %CASE%: %LOG% already exists -- this case has been run; delete the log to run it again.
IF EXIST "%LOG%" pause
IF EXIST "%LOG%" exit /b 1

SET "RESULT=result.txt"
SET "REP_ORDERS=V4"
SET "REP_ONLYTYPES=1,3"
SET "ExtPrint=1"

SET "RUNARGS=22 8"
echo %CASE%: EXE=%EXE%  LOG=%CASE%.log  started %DATE% %TIME%
powershell -NoProfile -Command "(Get-Process -Id $PID).PriorityClass='BelowNormal'; [Console]::OutputEncoding=[System.Text.Encoding]::UTF8; $enc=New-Object System.Text.UTF8Encoding $false; $w=New-Object System.IO.StreamWriter($env:LOG,$false,$enc); & cmd /c ('\"' + $env:EXE + '\" ' + $env:RUNARGS + ' 2>&1') | ForEach-Object { $_; $w.WriteLine($_); $w.Flush() }; $w.Close(); exit $LASTEXITCODE"
SET "RC=%ERRORLEVEL%"
IF NOT "%RC%"=="0" echo %CASE%: stopped, exit code %RC% -- see the message above
echo %CASE%: ended %DATE% %TIME%
echo %CASE%: ended %DATE% %TIME% >> "%LOG%"

echo.
pause
exit /b %RC%
