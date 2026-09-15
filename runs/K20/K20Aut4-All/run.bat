@echo off
REM ============================================================================
REM  runs\K20Aut4-All -- the K20 classification of |Aut| > 3, by GROUP SIZE.
REM  A full run gives 230 classes {6:168, 9:46, 18:9, 19:3, 57:1, 171:2, 342:1};
REM  the |Aut| = 9 row is 46 = 39 of C9 type plus 7 of C3xC3 type from the E9 leg.
REM  AllResults\K20_P1F_aut_gt3.txt is the archived catalog -- a re-run must
REM  REPRODUCE these classes, not read them.
REM
REM  WHAT IS OUT OF SCOPE.  |Aut| = 3 EXACTLY, and it is NOT empty: the order-3
REM  leg is the one cell no theorem settles and no run finishes (type 3^6.1^2, about
REM  4.9e12 cover-nodes), and a capped harvest has already found ~100 such classes.
REM  A class with |Aut| = 3 has an order-3 automorphism but only 3 elements, so it
REM  falls outside "> 3" -- which is why this case is scoped by GROUP SIZE.  Saying
REM  "admits an automorphism of order >= 3" would claim that cell too, and the run
REM  does not deliver it.  |Aut| = 2 is out of scope as well: the order-2 search
REM  does not finish at K20 any more than at K16 or K18.
REM  NOT a gap here, though it is at K18: the elementary-abelian 2-groups.  V4, C2^3
REM  and larger have > 3 elements and no element of order >= 3, so no cyclic leg
REM  sees them -- but the V4 sweep does, and returns empty (both arithmetic V4
REM  shapes hold a fixed-point-free involution and 4 divides 20).  Every bigger one
REM  contains a V4, so they are empty too.
REM
REM  RUN THIS .BAT AS IS for the whole classification.  The order set below,
REM  {19,V4,E9,S3,5,6,7,9}, is every cell not settled by a theorem.  Orders 5 and 7
REM  are expected-EMPTY and are SEARCHED rather than asserted, so the run states the
REM  emptiness instead of citing it.  Measured, 10 threads, 2026-08-30: order 5 is
REM  14 minutes of the 19 (all of it type 5^4; type 5^2 1^10 is skipped), order 7 is
REM  0.1s.  V4, E9 and S3 are near-instant: both arithmetic V4 shapes contain a
REM  fixed-point-free involution and are skipped citing the order-2 parity theorem.
REM
REM  To run a SUBSET, edit REP_ORDERS below, save, run.  Every leg that actually
REM  produces a class, in about four minutes:
REM      SET "REP_ORDERS=19,V4,E9,S3,6,9"
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

SET "CASE=K20Aut4-All"
SET "EXE=%P1F_EXE%"
SET "LOG=%HERE%%CASE%.log"
IF EXIST "%LOG%" echo %CASE%: %LOG% already exists -- this case has been run; delete the log to run it again.
IF EXIST "%LOG%" pause
IF EXIST "%LOG%" exit /b 1

SET "RESULT=result.txt"
SET "REP_ORDERS=19,V4,E9,S3,5,6,7,9"

SET "RUNARGS=20 10"
echo %CASE%: EXE=%EXE%  LOG=%CASE%.log
powershell -NoProfile -Command "[Console]::OutputEncoding=[System.Text.Encoding]::UTF8; $enc=New-Object System.Text.UTF8Encoding $false; $w=New-Object System.IO.StreamWriter($env:LOG,$false,$enc); & cmd /c ('\"' + $env:EXE + '\" ' + $env:RUNARGS + ' 2>&1') | ForEach-Object { $_; $w.WriteLine($_); $w.Flush() }; $w.Close(); exit $LASTEXITCODE"
SET "RC=%ERRORLEVEL%"
IF NOT "%RC%"=="0" echo %CASE%: stopped, exit code %RC% -- see the message above

REM  Compare -- look every class this run wrote up in the archived catalog,
REM  AllResults\K20_P1F_aut_gt3.txt, and report Ok or Fault with a histogram.  --n 20 picks
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
echo %CASE%:     perl %P1F_ROOT%\tools\compare_to_catalog.pl --n 20 %RESULT%
GOTO :CHECK_DONE
:CHECK_RUN
"%PERL%" "%P1F_ROOT%\tools\compare_to_catalog.pl" --n 20 "%RESULT%"
REM  2 = the catalog file is not in the repository yet, so there was nothing to look up in.
REM  That is not a fault in the run and must not fail the bat; only 1, a real Fault, does.
REM  IF ERRORLEVEL n means "n or higher", so the 2 test has to come first.
IF ERRORLEVEL 2 GOTO :CHECK_DONE
IF ERRORLEVEL 1 SET "RC=3"
:CHECK_DONE

echo.
pause
exit /b %RC%
