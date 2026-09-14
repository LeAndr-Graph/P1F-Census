@echo off
REM ============================================================================
REM  runs\K16Aut2 -- the K16 order-2 leg: every class with an involution.
REM  A full run gives 60 classes, {2:59, 14:1}.  The 59 are every K16 class whose
REM  group is exactly order 2; the one with |Aut| = 14 is caught here as well
REM  because 14 is even, and it is the only even order K16 has above 2.
REM
REM  TOGETHER WITH K16Aut3-All THIS IS THE WHOLE OF K16.  That case gives the 30
REM  classes with |Aut| > 2; this one gives the 59 with |Aut| = 2, plus the
REM  |Aut| = 14 both cases reach.  59 + 30 = 89, the published K16 total, and
REM  AllResults\K16_P1F_aut_gt1.txt is exactly that union -- the complete K16
REM  classification.  A re-run must REPRODUCE these classes, not read them.
REM
REM  WHY THE LIMIT IS 60, AND WHAT IT COSTS
REM  REP_ORDER=2:60 stops the run the moment the 60th distinct class is found.
REM  Without a target the search keeps going over the whole range to prove there
REM  is no 61st, and that proof is nearly all of the wall time: measured, the
REM  60th and last class arrives at about 600 s while the complete run takes
REM  4,909 s (a second machine finished the same complete run in 36 minutes).
REM  So the target turns roughly an hour of waiting into 7 minutes (measured:
REM  390.5 s at 10 threads, 205,153,668 nodes), and the results are the same 60
REM  either way.
REM
REM  WHAT THE TARGET DOES NOT DO.  Stopping at 60 REPRODUCES the known answer; it
REM  does not PROVE it.  The run cannot tell you there is no 61st class, because
REM  it stopped looking, and it says so itself:
REM      HARVESTED 60 classes (target 60 reached -- NOT proven complete)
REM  Completeness rests on the full run, which was done once and gave exactly
REM  these 60.  To repeat that proof instead, use
REM      SET "REP_ORDER=2"
REM  and expect the hour.
REM
REM  A target also changes HOW the range is walked: with one set the engine loops
REM  the range, re-diving and deduping, until the target is met; without one it
REM  makes a single systematic pass.  Node counts differ between the two modes.
REM
REM  RESULT is refused if it already exists -- delete result.txt deliberately when
REM  you mean to run again.
REM ============================================================================
SET "HERE=%~dp0"
cd /d "%HERE%"
setlocal
IF NOT DEFINED P1F_ROOT FOR %%I IN ("%HERE%..\..\..") DO SET "P1F_ROOT=%%~fI"
IF NOT DEFINED P1F_EXE  SET "P1F_EXE=%P1F_ROOT%\x64\Release\P1F-Census.exe"
CALL "%P1F_ROOT%\env_reset.bat"

SET "CASE=K16Aut2"
SET "EXE=%P1F_EXE%"
SET "LOG=%HERE%%CASE%.log"
IF EXIST "%LOG%" echo %CASE%: %LOG% already exists -- this case has been run; delete the log to run it again.
IF EXIST "%LOG%" pause
IF EXIST "%LOG%" exit /b 1

SET "RESULT=result.txt"
SET "REP_ORDER=2:60"

SET "RUNARGS=16 10"
echo %CASE%: EXE=%EXE%  LOG=%CASE%.log
echo %CASE%: order-2 leg, stopping at 60 distinct classes -- expect {2:59, 14:1}
powershell -NoProfile -Command "[Console]::OutputEncoding=[System.Text.Encoding]::UTF8; $enc=New-Object System.Text.UTF8Encoding $false; $w=New-Object System.IO.StreamWriter($env:LOG,$false,$enc); & cmd /c ('\"' + $env:EXE + '\" ' + $env:RUNARGS + ' 2>&1') | ForEach-Object { $_; $w.WriteLine($_); $w.Flush() }; $w.Close(); exit $LASTEXITCODE"
SET "RC=%ERRORLEVEL%"
IF NOT "%RC%"=="0" echo %CASE%: stopped, exit code %RC% -- see the message above

REM  Compare -- look every class this run wrote up in the archived catalog,
REM  AllResults\K16_P1F_aut_gt1.txt, and report Ok or Fault with a histogram.  --n 16 picks
REM  that catalog: there is one per N, each named for what it holds.  The check is
REM  PRESENT / NOT PRESENT and nothing else: it does not know which classes this case
REM  should have produced, so it cannot report a missing one.  What a full run must
REM  total is in ReadMe.md.  A result file with no results is Ok.
REM  NOTE the catalog's |Aut| = 2 half was BUILT from this case's first run, so the check is
REM  a reproducibility test of later runs, not independent evidence for the original 60.
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
echo %CASE%:     perl %P1F_ROOT%\tools\compare_to_catalog.pl --n 16 %RESULT%
GOTO :CHECK_DONE
:CHECK_RUN
"%PERL%" "%P1F_ROOT%\tools\compare_to_catalog.pl" --n 16 "%RESULT%"
REM  2 = the catalog file is not in the repository yet, so there was nothing to look up in.
REM  That is not a fault in the run and must not fail the bat; only 1, a real Fault, does.
REM  IF ERRORLEVEL n means "n or higher", so the 2 test has to come first.
IF ERRORLEVEL 2 GOTO :CHECK_DONE
IF ERRORLEVEL 1 SET "RC=3"
:CHECK_DONE

echo.
pause
exit /b %RC%
