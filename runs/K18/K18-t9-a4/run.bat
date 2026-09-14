@echo off
REM ============================================================================
REM  runs\K18-t9-a4 -- K18 |Aut| = 2, TYPE 9 (2^9, fpf), a = 4 stratum.
REM  The open stratum: a = 4 is fertile and does contain |Aut| = 2 classes (the
REM  first was found in block 4.0.483), so the t9 count is > 727.  What is known
REM  so far is kept in AllResults\K18-t9-a4.txt -- a re-run must REPRODUCE those
REM  classes, not read them.
REM
REM  RUN THIS .BAT AS IS for the full sweep: RAW blocks c = 0 .. 65039, of which
REM  59,872 are canonical and actually completed.
REM
REM  Both range variables are ALWAYS set -- there is no "run to the end" default:
REM      REP_F3START   first RAW block c                 (default 0)
REM      REP_F3STOP    last RAW block c, INCLUSIVE      (default 65039 = the last one)
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
REM  FAST FLAG SET (~3x wall over the plain run, validated identical-class):
REM      REP_TYPEMASK=1      one-per-pair as a genM vertex-1 mask
REM      REP_SGEN=1          forced-S rows enumerated on the sigma0 quotient
REM      REP_DIAGPRUNE=8:14  diagonal look-ahead prune
REM  All three are a = 4 only.
REM
REM  The log and the result file are named after the case AND the range, so two ranges
REM  never write over each other:  K18-t9-a4_<start>-<stop>.log and K18-t9-a4_<start>-<stop>.txt
REM  The .bat refuses to start if that log exists, and the program refuses an existing
REM  result file -- delete them deliberately when you mean to run a range again.
REM ============================================================================
SET "HERE=%~dp0"
cd /d "%HERE%"
setlocal
IF NOT DEFINED P1F_ROOT FOR %%I IN ("%HERE%..\..\..") DO SET "P1F_ROOT=%%~fI"
IF NOT DEFINED P1F_EXE  SET "P1F_EXE=%P1F_ROOT%\x64\Release\P1F-Census.exe"
CALL "%P1F_ROOT%\env_reset.bat"

SET "CASE=K18-t9-a4"
SET "EXE=%P1F_EXE%"

REM  Block range -- always explicit.
REM
REM  SET TO 0..2238, NOT the whole column, so the run ends on a known answer.  P47, the
REM  only order-16 class this column yields, is owned by block 2237 (see ReadMe.md), and
REM  this range ends one block past it.
REM
REM  WHERE TO LOOK: P47 does NOT appear in the RESULT file.  That file is the |Aut| = 2
REM  census, and a class with a bigger group is counted in the closing histogram instead:
REM      [F3COMPLETE] duplicates by |Aut| > 2: aut{16:1}
REM  aut{16:1} is the check.  The result file holding only |Aut| = 2 classes is correct,
REM  not a miss.  (Measured 2026-09-09: 5 classes saved, aut{16:1} duplicates, 839 min.)
REM
REM  For the full sweep put 65039 back.  At the 22.9 s per canonical block this range
REM  measured, the whole column is about 16 days -- and that is likely pessimistic, since
REM  the rate climbed from 564 to 850 nodes/ms across the range, so later blocks are cheaper.
SET "REP_F3START=0"
SET "REP_F3STOP=2238"

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
SET "REP_F3A=4"
SET "REP_F3COUNT=1"
SET "REP_F3COMPLETE=1"
SET "REP_PATAPPLY=oneperpair"
SET "REP_TYPEMASK=1"
SET "REP_SGEN=1"
SET "REP_DIAGPRUNE=8:14"
REM  REP_PRECALC: precalculate, once per block, the orbits {F, sigma F} admissible at
REM  that block's prefix, then FILTER that list instead of regenerating candidates with
REM  genM at every node.  Measured on block 1777: 27.3s -> 20.8s, a 1.3x speedup, same
REM  tree and byte-identical result.  REP_DIAGPRUNE and REP_TYPEMASK are applied inside
REM  the list walk too, so nothing is bypassed.  Needs REP_PRUNELEVEL=1, set above.
REM  Unset it to fall back.
SET "REP_PRECALC=1"
REM  NOT set: REP_SWAPCAP.  It is documented as a sound a=4 cap with a 49% deep-node cut,
REM  but on block 1777 it removes ZERO nodes and costs 31% of the wall clock -- the search
REM  never reaches more than 8 swap rows there, so the cap never fires and you pay only for
REM  testing it.  Measure it on blocks where it actually fires before adding it.

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
