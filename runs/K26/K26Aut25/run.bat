@echo off
REM ============================================================================
REM  runs\K26\K26Aut25 -- the K26 P1Fs with an automorphism of order 25
REM  (1-rotational, type 25 1 = the starters in Z_25, see ../ReadMe.md).
REM  First run 2026-10-31: 23 classes {25:23} = Pike-Shalaby (2003), Table 1.
REM  About 5 min at 8 threads (setup), BELOW NORMAL priority.
REM  Run one case at a time.
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

SET "CASE=K26Aut25"
SET "EXE=%P1F_EXE%"
SET "LOG=%HERE%%CASE%.log"
IF EXIST "%LOG%" echo %CASE%: %LOG% already exists -- this case has been run; delete the log to run it again.
IF EXIST "%LOG%" pause
IF EXIST "%LOG%" exit /b 1

SET "RESULT=result.txt"
SET "REP_ORDERS=25"

SET "RUNARGS=26 8"
echo %CASE%: EXE=%EXE%  LOG=%CASE%.log  started %DATE% %TIME%
powershell -NoProfile -Command "(Get-Process -Id $PID).PriorityClass='BelowNormal'; [Console]::OutputEncoding=[System.Text.Encoding]::UTF8; $enc=New-Object System.Text.UTF8Encoding $false; $w=New-Object System.IO.StreamWriter($env:LOG,$false,$enc); & cmd /c ('\"' + $env:EXE + '\" ' + $env:RUNARGS + ' 2>&1') | ForEach-Object { $_; $w.WriteLine($_); $w.Flush() }; $w.Close(); exit $LASTEXITCODE"
SET "RC=%ERRORLEVEL%"
IF NOT "%RC%"=="0" echo %CASE%: stopped, exit code %RC% -- see the message above
echo %CASE%: ended %DATE% %TIME%
echo %CASE%: ended %DATE% %TIME% >> "%LOG%"

REM  Compare against AllResults\K26_P1F_rot25_24.txt (--n 26).
IF NOT "%RC%"=="0" GOTO :CHECK_DONE
CALL "%P1F_ROOT%\tools\findperl.bat" && GOTO :CHECK_RUN
echo %CASE%: perl was not found, so the catalogue check did not run; %RESULT% is complete.
GOTO :CHECK_DONE
:CHECK_RUN
SET "CHKOUT=%TEMP%\%CASE%_check.txt"
"%PERL%" "%P1F_ROOT%\tools\compare_to_catalog.pl" --n 26 "%RESULT%" > "%CHKOUT%" 2>&1
SET "CHKRC=%ERRORLEVEL%"
TYPE "%CHKOUT%"
TYPE "%CHKOUT%" >> "%LOG%"
DEL /Q "%CHKOUT%" 2>nul
IF %CHKRC% GEQ 2 GOTO :CHECK_DONE
IF %CHKRC% GEQ 1 SET "RC=3"
:CHECK_DONE

echo.
pause
exit /b %RC%
