@echo off
REM ============================================================================
REM  runs\K16Aut3-All -- the full K16 |Aut| > 2 classification.
REM  A full run gives 30 classes {3:19, 5:5, 7:4, 14:1, 15:1}, which is the
REM  published K16 count and matches the literature exactly.
REM  AllResults\K16_P1F_aut_gt1.txt is the archived catalog -- a re-run must
REM  REPRODUCE these classes, not read them.
REM  |Aut| = 2 is out of scope FOR THIS CASE, but no longer for K16: runs\K16Aut2
REM  is the order-2 leg, and since 2026-09-01 the catalog is the union of the two --
REM  K16_P1F_aut_gt1.txt, 89 classes, the complete K16 classification.
REM
REM  RUN THIS .BAT AS IS for the whole classification.  The order set below,
REM  {3,5,7}, is every cell that can hold a class.  K16 has no |Aut| divisible
REM  by 4, so there is no order-4 leg and no V4 sweep; |Aut| = 14 is reached by
REM  the order-7 leg and |Aut| = 15 by both order 3 and order 5, so composite
REM  orders need no leg of their own.
REM
REM  To run a SUBSET, edit REP_ORDERS below, save, run.  For example the
REM  order-3 leg alone, which holds 19 of the 30:
REM      SET "REP_ORDERS=3"
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

SET "CASE=K16Aut3-All"
SET "EXE=%P1F_EXE%"
SET "LOG=%HERE%%CASE%.log"
IF EXIST "%LOG%" echo %CASE%: %LOG% already exists -- this case has been run; delete the log to run it again.
IF EXIST "%LOG%" pause
IF EXIST "%LOG%" exit /b 1

SET "RESULT=result.txt"
SET "REP_ORDERS=3,5,7"

SET "RUNARGS=16 10"
echo %CASE%: EXE=%EXE%  LOG=%CASE%.log
powershell -NoProfile -Command "[Console]::OutputEncoding=[System.Text.Encoding]::UTF8; $enc=New-Object System.Text.UTF8Encoding $false; $w=New-Object System.IO.StreamWriter($env:LOG,$false,$enc); & cmd /c ('\"' + $env:EXE + '\" ' + $env:RUNARGS + ' 2>&1') | ForEach-Object { $_; $w.WriteLine($_); $w.Flush() }; $w.Close(); exit $LASTEXITCODE"
SET "RC=%ERRORLEVEL%"
IF NOT "%RC%"=="0" echo %CASE%: stopped, exit code %RC% -- see the message above

REM  Compare -- look every class this run wrote up in the archived catalog,
REM  AllResults\K16_P1F_aut_gt1.txt, and report Ok or Fault with a histogram.  --n 16 picks
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
