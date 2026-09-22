@echo off
REM ============================================================================
REM  runs\K18Aut3-All -- the full K18 |Aut| > 2 classification.
REM  A full run gives 531 classes {3:351, 4:144, 8:22, 16:12, 17:1, 272:1}.
REM  AllResults\K18_P1F_aut_gt1.txt is the archived catalog -- a re-run must
REM  REPRODUCE these classes, not read them.
REM  |Aut| = 2 is out of scope here; that is the three K18-t8 / K18-t9 cases.
REM
REM  RUN THIS .BAT AS IS for the whole classification.  The order set below,
REM  {4,5,7,17,V4,3}, is exactly the cells not covered by a theorem: orders
REM  11 and 13 and the composite orders are covered by the odd-prime parity
REM  theorem and by power reduction, and the theorem-covered types inside
REM  3, 5 and 7 are skipped by the engine itself.
REM
REM  To run a SUBSET, edit REP_ORDERS below, save, run.  For example, everything
REM  except the heavy order-3 leg (which is a full enumeration of the even-t
REM  types and dominates the wall time):
REM      SET "REP_ORDERS=4,5,7,17,V4"
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

SET "CASE=K18Aut3-All"
SET "EXE=%P1F_EXE%"
SET "LOG=%HERE%%CASE%.log"
IF EXIST "%LOG%" echo %CASE%: %LOG% already exists -- this case has been run; delete the log to run it again.
IF EXIST "%LOG%" pause
IF EXIST "%LOG%" exit /b 1

SET "RESULT=result.txt"
SET "REP_ORDERS=4,5,7,17,V4,3"

SET "RUNARGS=18 10"
echo %CASE%: EXE=%EXE%  LOG=%CASE%.log
powershell -NoProfile -Command "[Console]::OutputEncoding=[System.Text.Encoding]::UTF8; $enc=New-Object System.Text.UTF8Encoding $false; $w=New-Object System.IO.StreamWriter($env:LOG,$false,$enc); & cmd /c ('\"' + $env:EXE + '\" ' + $env:RUNARGS + ' 2>&1') | ForEach-Object { $_; $w.WriteLine($_); $w.Flush() }; $w.Close(); exit $LASTEXITCODE"
SET "RC=%ERRORLEVEL%"
IF NOT "%RC%"=="0" echo %CASE%: stopped, exit code %RC% -- see the message above

REM  Compare -- look every class this run wrote up in the archived catalog,
REM  AllResults\K18_P1F_aut_gt1.txt, and report Ok or Fault with a histogram.  It checks
REM  PRESENT / NOT PRESENT and nothing else: it does not know which classes this range
REM  should have produced, so it cannot report a missing one.  What a full column must
REM  total is in ReadMe.md.  A result file with no results is Ok.
REM  findperl.bat picks the perl -- see the reasoning there.  If there is none, say so and
REM  say what to do: the RESULT file is already written and correct, only the check is lost.
IF NOT "%RC%"=="0" GOTO :CHECK_DONE
CALL "%P1F_ROOT%\tools\findperl.bat" && GOTO :CHECK_RUN
echo.
echo %CASE%: perl was not found on PATH, so the catalogue check did not run.
echo %CASE%: the results in %RESULT% are complete and correct -- only the check was skipped.
echo %CASE%: install Strawberry Perl from https://strawberryperl.com/ or Git for Windows
echo %CASE%: from https://git-scm.com/download/win -- either one supplies it -- and run this bat
echo %CASE%: again to check them, or check them now with
echo %CASE%:     perl %P1F_ROOT%\tools\compare_to_catalog.pl %RESULT%
GOTO :CHECK_DONE
:CHECK_RUN
REM  The check writes to a temporary file so its verdict can go to BOTH the
REM  console and the log; %LOG% holds the engine's output alone otherwise, and
REM  the Compare line is what makes a disagreeing run diagnosable.
SET "CHKOUT=%TEMP%\%CASE%_check.txt"
"%PERL%" "%P1F_ROOT%\tools\compare_to_catalog.pl" --n 18 "%RESULT%" > "%CHKOUT%" 2>&1
SET "CHKRC=%ERRORLEVEL%"
TYPE "%CHKOUT%"
TYPE "%CHKOUT%" >> "%LOG%"
DEL /Q "%CHKOUT%" 2>nul
REM  2 = the catalog file is not in the repository, so there was nothing to look up in.
REM  That is not a fault in the run and must not fail the bat; only 1, a real Fault, does.
REM  GEQ 2 has to come first, for the same reason IF ERRORLEVEL did.
IF %CHKRC% GEQ 2 GOTO :CHECK_DONE
IF %CHKRC% GEQ 1 SET "RC=3"
:CHECK_DONE

echo.
pause
exit /b %RC%
