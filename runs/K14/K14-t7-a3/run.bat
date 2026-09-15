@echo off
REM ============================================================================
REM  runs\K14-t7-a3 -- verify the search engine used by runs\K18-t9-a0 and runs\K18-t9-a4.
REM  One block column of the K14 |Aut| = 2 census, run through the SAME block
REM  driver K18 uses.  K14 is the one size whose order-2 leg finishes, so the
REM  answer is known independently: runs\K14Aut2-All finds it by the ordinary
REM  type loop, not by blocks.
REM  See ReadMe.md.  RESULT is refused if it already exists.
REM ============================================================================
SET "HERE=%~dp0"
cd /d "%HERE%"
setlocal
IF NOT DEFINED P1F_ROOT FOR %%I IN ("%HERE%..\..\..") DO SET "P1F_ROOT=%%~fI"
IF NOT DEFINED P1F_EXE  SET "P1F_EXE=%P1F_ROOT%\x64\Release\p1f.exe"
CALL "%P1F_ROOT%\env_reset.bat"

SET "CASE=K14-t7-a3"
SET "EXE=%P1F_EXE%"
SET "LOG=%HERE%%CASE%.log"
IF EXIST "%LOG%" echo %CASE%: %LOG% already exists -- this case has been run; delete the log to run it again.
IF EXIST "%LOG%" pause
IF EXIST "%LOG%" exit /b 1

SET "RESULT=%CASE%.txt"
SET "REP_ORDERS=2"
SET "REP_ONLYTYPES=7"
SET "REP_LEVEL=3"
SET "REP_PRUNELEVEL=1"
SET "REP_F3A=3"
SET "REP_F3COUNT=1"
SET "REP_F3COMPLETE=1"
SET "REP_F3START=0"
SET "REP_F3STOP=451"
SET "RUNARGS=14 10"

echo %CASE%: EXE=%EXE%  LOG=%CASE%.log
powershell -NoProfile -Command "[Console]::OutputEncoding=[System.Text.Encoding]::UTF8; $enc=New-Object System.Text.UTF8Encoding $false; $w=New-Object System.IO.StreamWriter($env:LOG,$false,$enc); & cmd /c ('\"' + $env:EXE + '\" ' + $env:RUNARGS + ' 2>&1') | ForEach-Object { $_; $w.WriteLine($_); $w.Flush() }; $w.Close(); exit $LASTEXITCODE"
SET "RC=%ERRORLEVEL%"
IF NOT "%RC%"=="0" echo %CASE%: stopped, exit code %RC%

IF NOT "%RC%"=="0" GOTO :CHECK_DONE
CALL "%P1F_ROOT%\tools\findperl.bat" && GOTO :CHECK_RUN
echo %CASE%: perl not found; check with  perl %P1F_ROOT%\tools\compare_to_catalog.pl --n 14 %RESULT%
GOTO :CHECK_DONE
:CHECK_RUN
"%PERL%" "%P1F_ROOT%\tools\compare_to_catalog.pl" --n 14 "%RESULT%"
IF ERRORLEVEL 2 GOTO :CHECK_DONE
IF ERRORLEVEL 1 SET "RC=3"
:CHECK_DONE

echo.
echo %CASE%: EXPECTED 1 class(es) with ^|Aut^| = 2.  The Compare step above says whether
echo %CASE%: what was written is in the catalog; it CANNOT tell you a class is missing, so
echo %CASE%: check the count as well.
echo.
pause
exit /b %RC%
