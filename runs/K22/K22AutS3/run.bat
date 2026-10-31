@echo off
REM ============================================================================
REM  runs\K22\K22AutS3 -- K22 P1Fs whose automorphism group contains S3
REM  (REP_ORDERS=S3, see ../ReadMe.md). Overnight run.
REM  2026-10-26 probe: shapes t2 d2 R2 f0 and t0 d1 R3 f2 complete (0 classes);
REM  shape t0 d2 R3 f0 (491 blocks) not finished after 30 min (750k nodes).
REM  Expected: hours. p1f needs ~0.1 GB.
REM  Runs at BELOW NORMAL priority on 8 threads. Run one case at a time.
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

SET "CASE=K22AutS3"
SET "EXE=%P1F_EXE%"
SET "LOG=%HERE%%CASE%.log"
IF EXIST "%LOG%" echo %CASE%: %LOG% already exists -- this case has been run; delete the log to run it again.
IF EXIST "%LOG%" pause
IF EXIST "%LOG%" exit /b 1

SET "RESULT=result.txt"
SET "REP_ORDERS=S3"
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
