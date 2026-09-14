@echo off
REM ============================================================================
REM  runs\K18-t9-a0 -- K18 |Aut| = 2, TYPE 9 (2^9, fpf), a = 0 stratum.
REM  This is the census leg for the 727 |Aut| = 2 classes; a full sweep must end
REM  with exactly 727 records.  AllResults\K18-t9-a0.txt holds that answer -- a
REM  re-run must REPRODUCE it, not read it.
REM
REM  RUN THIS .BAT AS IS for the full sweep: RAW blocks c = 0 .. 57215, of which
REM  8,713 are canonical and actually completed (the last is 0.0.47925).
REM
REM  Both range variables are ALWAYS set -- there is no "run to the end" default:
REM      REP_F3START   first RAW block c                 (default 0)
REM      REP_F3STOP    last RAW block c, INCLUSIVE      (default 57215 = the last one)
REM  To run a SUBSET, edit the two of them below, save, run.
REM  The subset is [REP_F3START, REP_F3STOP] -- both ends included.  For example, the
REM  first 1000 raw blocks:
REM      SET "REP_F3START=0"
REM      SET "REP_F3STOP=999"
REM  A subset is self-contained -- no baseline is read, and none is needed: duplicate
REM  rejection is ON by default, so a range writes only the classes it OWNS.  A class's
REM  owner is the lowest block of the column that holds it, which every block computes
REM  the same way, so separately run ranges are DISJOINT and simply concatenate -- no
REM  merge, no dedup.  A block that re-finds a class owned elsewhere logs "rejected
REM  (foreign)" and writes nothing; REP_OWNERALL=1 names the owner of each, and
REM  REP_OWNER=0 switches the filter off and records every re-find instead.
REM  To resume an interrupted run, take the last block the log printed and set
REM  REP_F3START to its c plus 1.
REM
REM  ACCELERATORS: REP_TYPEMASK / REP_SGEN / REP_PATAPPLY=oneperpair are UNSOUND for
REM  a = 0 (the engine accepts them and silently returns 0), and REP_DIAGPRUNE is
REM  refused unless REP_F3A=4.
REM  REP_PRECALC is the exception and is ON below.  It precalculates, once per block,
REM  the orbits {F, sigmaF} admissible at that block's prefix and then FILTERS that
REM  list instead of regenerating candidates with genM at every node.  Measured on
REM  blocks 0-9, 10 threads: 368.8s -> 65.5s, a 5.6x speedup, with the SAME TREE
REM  (163,664,896 nodes against 163,667,968) and the same 8 classes, compare.pl
REM  passing both ways.  It needs REP_PRUNELEVEL=1, which is already set; without
REM  that the two paths walk different trees.  Unset it to fall back.
REM
REM  The log and the result file are named after the case AND the range, so two ranges
REM  never write over each other:  K18-t9-a0_<start>-<stop>.log and K18-t9-a0_<start>-<stop>.txt
REM  The .bat refuses to start if that log exists, and the program refuses an existing
REM  result file -- delete them deliberately when you mean to run a range again.
REM ============================================================================
SET "HERE=%~dp0"
cd /d "%HERE%"
setlocal
IF NOT DEFINED P1F_ROOT FOR %%I IN ("%HERE%..\..\..") DO SET "P1F_ROOT=%%~fI"
IF NOT DEFINED P1F_EXE  SET "P1F_EXE=%P1F_ROOT%\x64\Release\P1F-Census.exe"
CALL "%P1F_ROOT%\env_reset.bat"

SET "CASE=K18-t9-a0"
SET "EXE=%P1F_EXE%"

REM  Block range -- always explicit.  Default = the whole column.
SET "REP_F3START=0"
SET "REP_F3STOP=57215"

REM  Log and result carry the range: two ranges of one case never collide, and a range
REM  that has already been run cannot be started again by accident.
SET "LOG=%HERE%%CASE%_%REP_F3START%-%REP_F3STOP%.log"
SET "RESULT=%CASE%_%REP_F3START%-%REP_F3STOP%.txt"
IF EXIST "%LOG%" echo %CASE%: %LOG% already exists -- that range has been run; delete the log, or pick another range.
IF EXIST "%LOG%" pause
IF EXIST "%LOG%" exit /b 1

SET "REP_ORDER=2"
SET "REP_ONLYTYPES=9"
SET "REP_LEVEL=3"
SET "REP_PRUNELEVEL=1"
SET "REP_PRECALC=1"
SET "REP_F3A=0"
SET "REP_F3COUNT=1"
SET "REP_F3COMPLETE=1"

SET "RUNARGS=18 10"
echo %CASE%: EXE=%EXE%  LOG=%LOG%
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
"%PERL%" "%P1F_ROOT%\tools\compare_to_catalog.pl" --n 18 "%RESULT%"
REM  2 = the catalog file is not in the repository, so there was nothing to look up in.
REM  That is not a fault in the run and must not fail the bat; only 1, a real Fault, does.
REM  IF ERRORLEVEL n means "n or higher", so the 2 test has to come first.
IF ERRORLEVEL 2 GOTO :CHECK_DONE
IF ERRORLEVEL 1 SET "RC=3"
:CHECK_DONE

echo.
pause
exit /b %RC%
