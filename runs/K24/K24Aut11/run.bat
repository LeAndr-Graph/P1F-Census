@echo off
REM ============================================================================
REM  runs\K24\K24Aut11 -- the K24 P1Fs with an automorphism of order 11
REM  (see ../ReadMe.md): the only surviving type is 11^2 1^2. It also finds
REM  the 22 even-starter classes and the 5 starter classes with |Aut| 253, 506.
REM  First run 2026-10-30: 557 classes {11:531, 22:21, 253:4, 506:1}; with the
REM  4 classes {23:4} of K24Aut23 that is the 561 of the catalog.
REM  About 62 min at 8 threads, BELOW NORMAL priority.
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

SET "CASE=K24Aut11"
SET "EXE=%P1F_EXE%"
SET "LOG=%HERE%%CASE%.log"
IF EXIST "%LOG%" echo %CASE%: %LOG% already exists -- this case has been run; delete the log to run it again.
IF EXIST "%LOG%" pause
IF EXIST "%LOG%" exit /b 1

SET "RESULT=result.txt"
SET "REP_ORDERS=11"

SET "RUNARGS=24 8"
echo %CASE%: EXE=%EXE%  LOG=%CASE%.log  started %DATE% %TIME%
powershell -NoProfile -Command "(Get-Process -Id $PID).PriorityClass='BelowNormal'; [Console]::OutputEncoding=[System.Text.Encoding]::UTF8; $enc=New-Object System.Text.UTF8Encoding $false; $w=New-Object System.IO.StreamWriter($env:LOG,$false,$enc); & cmd /c ('\"' + $env:EXE + '\" ' + $env:RUNARGS + ' 2>&1') | ForEach-Object { $_; $w.WriteLine($_); $w.Flush() }; $w.Close(); exit $LASTEXITCODE"
SET "RC=%ERRORLEVEL%"
IF NOT "%RC%"=="0" echo %CASE%: stopped, exit code %RC% -- see the message above
echo %CASE%: ended %DATE% %TIME%
echo %CASE%: ended %DATE% %TIME% >> "%LOG%"

REM  Compare against AllResults\K24_P1F_aut_gt3.txt (--n 24).
IF NOT "%RC%"=="0" GOTO :CHECK_DONE
CALL "%P1F_ROOT%\tools\findperl.bat" && GOTO :CHECK_RUN
echo %CASE%: perl was not found, so the catalogue check did not run; %RESULT% is complete.
GOTO :CHECK_DONE
:CHECK_RUN
SET "CHKOUT=%TEMP%\%CASE%_check.txt"
"%PERL%" "%P1F_ROOT%\tools\compare_to_catalog.pl" --n 24 "%RESULT%" > "%CHKOUT%" 2>&1
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
