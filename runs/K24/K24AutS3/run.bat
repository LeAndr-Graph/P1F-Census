@echo off
REM ============================================================================
REM  runs\K24\K24AutS3 -- K24 P1Fs whose automorphism group contains S3
REM  (REP_ORDERS=S3, see ../ReadMe.md). Shapes that reach the search:
REM  t2 d3 R2 f0, t0 d2 R3 f2, t2 d0 R3 f0; the rest are skipped by the lemmas.
REM  First run 2026-10-30 (with the GSYM prune in genM): 0 classes, about 34 min
REM  (t2 d0 R3 f0 is most of it; before GSYM it was estimated at ~330 h).
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

SET "CASE=K24AutS3"
SET "EXE=%P1F_EXE%"
SET "LOG=%HERE%%CASE%.log"
IF EXIST "%LOG%" echo %CASE%: %LOG% already exists -- this case has been run; delete the log to run it again.
IF EXIST "%LOG%" pause
IF EXIST "%LOG%" exit /b 1

SET "RESULT=result.txt"
SET "REP_ORDERS=S3"
SET "ExtPrint=1"

SET "RUNARGS=24 8"
echo %CASE%: EXE=%EXE%  LOG=%CASE%.log  started %DATE% %TIME%
powershell -NoProfile -Command "(Get-Process -Id $PID).PriorityClass='BelowNormal'; [Console]::OutputEncoding=[System.Text.Encoding]::UTF8; $enc=New-Object System.Text.UTF8Encoding $false; $w=New-Object System.IO.StreamWriter($env:LOG,$false,$enc); & cmd /c ('\"' + $env:EXE + '\" ' + $env:RUNARGS + ' 2>&1') | ForEach-Object { $_; $w.WriteLine($_); $w.Flush() }; $w.Close(); exit $LASTEXITCODE"
SET "RC=%ERRORLEVEL%"
IF NOT "%RC%"=="0" echo %CASE%: stopped, exit code %RC% -- see the message above
echo %CASE%: ended %DATE% %TIME%
echo %CASE%: ended %DATE% %TIME% >> "%LOG%"

echo.
pause
exit /b %RC%
