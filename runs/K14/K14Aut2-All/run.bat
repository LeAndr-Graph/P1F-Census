@echo off
REM ============================================================================
REM  runs\K14Aut2-All -- the full K14 |Aut| > 1 classification.
REM  Aut2, not Aut3: a folder name says |Aut| >= N, so K16Aut3-All is |Aut| > 2 and
REM  K20Aut4-All is |Aut| > 3.  This case reaches |Aut| >= 2, because K14 is the one
REM  size whose order-2 leg finishes.  It was once called K14Aut3-All, a name that
REM  excluded three of the 21 classes it actually produces.
REM  A full run gives 21 classes {2:3, 3:5, 4:1, 6:5, 12:5, 84:1, 156:1}.
REM  AllResults\K14_P1F_aut_gt1.txt is the archived catalog -- a re-run must
REM  REPRODUCE these classes, not read them.
REM
REM  THE WHOLE CLASSIFICATION, |Aut| = 2 INCLUDED.  K14 is the one size where
REM  the order-2 leg finishes -- at K16, K18 and K20 it is intractable and the
REM  |Aut| = 2 classes need the block-driven census cases instead.  That is why
REM  this case is |Aut| > 1 while its K16, K18 and K20 siblings are |Aut| > 2
REM  and > 3, and why the catalog name says gt1.
REM
REM  RUN THIS .BAT AS IS for the whole classification.  The order set below,
REM  {2,3,7,13}, is every prime dividing any K14 |Aut|; their union is the
REM  complete |Aut| > 1 classification.  Composite orders need no leg of their
REM  own: a group of order 6 contains an element of order 3 and is found by the
REM  order-3 leg.
REM
REM  To run a SUBSET, edit REP_ORDERS below, save, run.  The order-2 leg is
REM  almost all of the wall time; without it,
REM      SET "REP_ORDERS=3,7,13"
REM  gives the 5 classes of |Aut| = 3 plus the 12 duplicates of larger groups.
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

SET "CASE=K14Aut2-All"
SET "EXE=%P1F_EXE%"
SET "LOG=%HERE%%CASE%.log"
IF EXIST "%LOG%" echo %CASE%: %LOG% already exists -- this case has been run; delete the log to run it again.
IF EXIST "%LOG%" pause
IF EXIST "%LOG%" exit /b 1

SET "RESULT=result.txt"
SET "REP_ORDERS=2,3,7,13"

SET "RUNARGS=14 10"
echo %CASE%: EXE=%EXE%  LOG=%CASE%.log
powershell -NoProfile -Command "[Console]::OutputEncoding=[System.Text.Encoding]::UTF8; $enc=New-Object System.Text.UTF8Encoding $false; $w=New-Object System.IO.StreamWriter($env:LOG,$false,$enc); & cmd /c ('\"' + $env:EXE + '\" ' + $env:RUNARGS + ' 2>&1') | ForEach-Object { $_; $w.WriteLine($_); $w.Flush() }; $w.Close(); exit $LASTEXITCODE"
SET "RC=%ERRORLEVEL%"
IF NOT "%RC%"=="0" echo %CASE%: stopped, exit code %RC% -- see the message above

REM  Compare -- look every class this run wrote up in the archived catalog,
REM  AllResults\K14_P1F_aut_gt1.txt, and report Ok or Fault with a histogram.  --n 14 picks
REM  that catalog: there is one per N, each named for what it holds.  The check is
REM  PRESENT / NOT PRESENT and nothing else: it does not know which classes this case
REM  should have produced, so it cannot report a missing one.  What a full run must
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
echo %CASE%:     perl %P1F_ROOT%\tools\compare_to_catalog.pl --n 14 %RESULT%
GOTO :CHECK_DONE
:CHECK_RUN
REM  The check writes to a temporary file so its verdict can go to BOTH the
REM  console and the log; %LOG% holds the engine's output alone otherwise, and
REM  the Compare line is what makes a disagreeing run diagnosable.
SET "CHKOUT=%TEMP%\%CASE%_check.txt"
"%PERL%" "%P1F_ROOT%\tools\compare_to_catalog.pl" --n 14 "%RESULT%" > "%CHKOUT%" 2>&1
SET "CHKRC=%ERRORLEVEL%"
TYPE "%CHKOUT%"
TYPE "%CHKOUT%" >> "%LOG%"
DEL /Q "%CHKOUT%" 2>nul
REM  2 = the catalog file is not in the repository yet, so there was nothing to look up in.
REM  That is not a fault in the run and must not fail the bat; only 1, a real Fault, does.
REM  GEQ 2 has to come first, for the same reason IF ERRORLEVEL did.
IF %CHKRC% GEQ 2 GOTO :CHECK_DONE
IF %CHKRC% GEQ 1 SET "RC=3"
:CHECK_DONE

echo.
pause
exit /b %RC%
