@echo off
REM ============================================================================
REM  runs\K22\K22Aut7 -- the K22 P1Fs with an automorphism of order 7. By the
REM  fixed-point lemma (see ../ReadMe.md) the only order-7 type is 7^3 1.
REM  A full run gives 539 classes {7:536, 21:3} (2026-10-26), about 45 minutes at
REM  8 threads. Part of the |Aut| > 6 census (../ReadMe.md).
REM
REM  RESULT is refused if it already exists -- delete result.txt deliberately when
REM  you mean to run again.
REM ============================================================================
SET "HERE=%~dp0"
cd /d "%HERE%"
setlocal
IF NOT DEFINED P1F_ROOT FOR %%I IN ("%HERE%..\..\..") DO SET "P1F_ROOT=%%~fI"
IF NOT DEFINED P1F_EXE  SET "P1F_EXE=%P1F_ROOT%\x64\Release\p1f.exe"
CALL "%P1F_ROOT%\env_reset.bat"

SET "CASE=K22Aut7"
SET "EXE=%P1F_EXE%"
SET "LOG=%HERE%%CASE%.log"
IF EXIST "%LOG%" echo %CASE%: %LOG% already exists -- this case has been run; delete the log to run it again.
IF EXIST "%LOG%" pause
IF EXIST "%LOG%" exit /b 1

SET "RESULT=result.txt"
SET "REP_ORDERS=7"
SET "ExtPrint=1"

SET "RUNARGS=22 8"
echo %CASE%: EXE=%EXE%  LOG=%CASE%.log
powershell -NoProfile -Command "[Console]::OutputEncoding=[System.Text.Encoding]::UTF8; $enc=New-Object System.Text.UTF8Encoding $false; $w=New-Object System.IO.StreamWriter($env:LOG,$false,$enc); & cmd /c ('\"' + $env:EXE + '\" ' + $env:RUNARGS + ' 2>&1') | ForEach-Object { $_; $w.WriteLine($_); $w.Flush() }; $w.Close(); exit $LASTEXITCODE"
SET "RC=%ERRORLEVEL%"
IF NOT "%RC%"=="0" echo %CASE%: stopped, exit code %RC% -- see the message above

REM  Compare against AllResults\K22_P1F_aut_gt6.txt (--n 22). Until that catalog exists
REM  the check reports "skipped" and exits 2, which does not fail the bat.
IF NOT "%RC%"=="0" GOTO :CHECK_DONE
CALL "%P1F_ROOT%\tools\findperl.bat" && GOTO :CHECK_RUN
echo %CASE%: perl was not found, so the catalogue check did not run; %RESULT% is complete.
GOTO :CHECK_DONE
:CHECK_RUN
SET "CHKOUT=%TEMP%\%CASE%_check.txt"
"%PERL%" "%P1F_ROOT%\tools\compare_to_catalog.pl" --n 22 "%RESULT%" > "%CHKOUT%" 2>&1
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
