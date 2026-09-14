@echo off
REM ============================================================================
REM  runs\K20Aut2-Dive -- K20 order 2 as a RANDOM DIVE HUNT.
REM
REM  ONE GOAL: find a single class with |Aut| = 2 exactly, and so settle open
REM  problem (ii) of the companion paper -- does K20 have a P1F whose group is
REM  exactly order 2.  Nothing here is a census and nothing here is reproducible.
REM
REM  WHY DIVES.  REP_DIVE=B replaces the exhaustive tree walk with randomized
REM  backtracking DFS restarts across all threads: each restart begins at a
REM  uniform-random root, explores in random candidate order, backtracks on dead
REM  ends, and gives up after B nodes, emitting every complete cover it reaches.
REM  The global canonKey still dedups.  Three consequences, all of them the point:
REM
REM    FASTER PER NODE.  Dives use NO setwiseStab.  On this type |C(alpha)| is
REM    about 3.7e8 and that call is the dominant per-node cost of the ordinary
REM    over-cap path, so removing it is the speedup.
REM
REM    IMMUNE TO THE DEDUP QUESTION.  No setwiseStab means no schreierDedup, so
REM    the orbit-quotient whose completeness is unproven never runs.  A class
REM    found here stands whatever that question turns out to be.
REM
REM    IT ESCAPES BARREN REGIONS.  Both earlier attempts stalled in one: the plain
REM    sweep did 878,635,008 nodes in 377 min for nothing, the level-2 shard
REM    146,202,624 in 164 min for nothing.  A fresh random root per restart is
REM    exactly what neither could do.
REM
REM  THE COST: RANDOM, SO NOT REPRODUCIBLE.  A re-run explores elsewhere and need
REM  not find the same class, or any.  That is acceptable HERE and nowhere else in
REM  this repo: for an existence question one class is a permanent answer, and the
REM  reproducibility that a census needs is not needed to prove a thing exists.
REM
REM  THE TOOL IS VALIDATED -- at K16, against a complete published census.  Forcing
REM  K16's 2^7.1^2 over-cap (REP_SYMCAP=1000000) puts it on this same dive path, and
REM  it recovered ALL 60 classes {2:59, 14:1} in 158.2s:
REM      [K16-REP]  FAST SEARCH: 134 random restarts, 60 new distinct classes
REM  About two restarts per class, and it went straight for |Aut| = 2 (59 of the 60)
REM  rather than the symmetric ones.  So a null result here is NOT the tool failing.
REM
REM  B IS CALIBRATED, AND SMALL IS RIGHT.  Measured at K16 (target 5 classes, dive
REM  path forced by REP_SYMCAP=1000000):
REM      B=1e5   ~47 restarts    4.3s
REM      B=1e4  ~285 restarts    3.2s   <- fastest
REM      B=1e3 ~1206 restarts    6.8s
REM      B=1e2  ~18k restarts   85.7s
REM  Dives complete across FIVE orders of magnitude, down to 100 nodes per restart,
REM  so budget is not what stops a descent -- RESTARTS PER UNIT TIME is the variable,
REM  and 1e4 is the optimum there.
REM
REM  THIS CORRECTS TWO EARLIER RUNS.  B = 1e6 gave ~120 restarts in 50 min and found
REM  nothing; B = 1e7 gave TWO restarts in 5 min, which was worse -- raising B spends
REM  draws to buy depth that was never the constraint.  At B = 1e4 an hour is roughly
REM  12,000 restarts.  The dive has never had a real number of draws at K20.
REM
REM  THE FIRST THING TO WATCH IS NOT |Aut| = 2 -- it is whether ANY class appears at
REM  all.  178 of the 230 catalogued classes contain an involution, so if descents
REM  complete at K20 at all, thousands of draws should emit something.  Continued
REM  silence across ~10k restarts is then a statement about K20's completion RATE,
REM  which is the one thing the K16 calibration cannot predict: K20's tree is 2
REM  orbit-blocks deeper with far larger candidate sets.
REM
REM  DO NOT WAIT FOR A CLOSING LINE.  "FAST SEARCH: N random restarts, ..." prints
REM  only when the leg ENDS -- target reached or killed by the engine, not by you.
REM  While running, the only signals are the ~ row's saved(duplicates) count and its
REM  node count; nodes / B estimates the restarts done so far.
REM
REM  REQUIREMENTS.  The dive path needs shardLevel == 0, so this case sets NO
REM  REP_LEVEL and NO REP_RANGE.  Do not add them here -- use K20Aut2-Shard for
REM  the systematic form.
REM
REM  WHAT TO WATCH.  The |Aut| histogram, nothing else.  Any |Aut| = 2 row settles
REM  the open problem.  Rows at 6, 18 or 342 are re-finds of catalogued classes and
REM  mean the hunt is working but has not reached the target stratum.
REM
REM  NO COMPARE STEP: AllResults holds only |Aut| > 3, so a correct run would
REM  report Compare Fault.  Same reason as the other two K20 order-2 cases.
REM
REM  RESULT is refused if it already exists, so a re-run needs the old one out of
REM  the way.  MOVE IT, DO NOT DELETE IT.  A class here may not come again.
REM ============================================================================
SET "HERE=%~dp0"
cd /d "%HERE%"
setlocal
IF NOT DEFINED P1F_ROOT FOR %%I IN ("%HERE%..\..\..") DO SET "P1F_ROOT=%%~fI"
IF NOT DEFINED P1F_EXE  SET "P1F_EXE=%P1F_ROOT%\x64\Release\P1F-Census.exe"
CALL "%P1F_ROOT%\env_reset.bat"

SET "CASE=K20Aut2-Dive"
SET "EXE=%P1F_EXE%"

REM ---- the two knobs ---------------------------------------------------------
REM  RUNLABEL  names the log and the result file; change it per pilot
REM  REP_DIVE  per-restart node budget B (see the note above -- UNMEASURED)
SET "RUNLABEL=b1e4"
SET "REP_DIVE=10000"

SET "REP_ORDER=2"
REM  8 threads, not 10: a 10-thread dive drove the machine to a near-crash on
REM  2026-09-03 (789 MB and CPU pegged).  Leave headroom -- this case runs for hours.
SET "RUNARGS=20 8"
REM ---------------------------------------------------------------------------

SET "LOG=%HERE%%CASE%_%RUNLABEL%.log"
SET "RESULT=result_%RUNLABEL%.txt"
IF EXIST "%LOG%" echo %CASE%: %LOG% already exists -- run "%RUNLABEL%" has been done; pick another RUNLABEL or move that log.
IF EXIST "%LOG%" pause
IF EXIST "%LOG%" exit /b 1

echo %CASE%: EXE=%EXE%  LOG=%CASE%_%RUNLABEL%.log  RESULT=%RESULT%
echo %CASE%: random dive hunt, budget B=%REP_DIVE% nodes/restart -- runs until killed
echo %CASE%: FIRST watch for ANY class at all -- that proves dives are completing
echo %CASE%: then watch the ^|Aut^| histogram; goal = ONE class with ^|Aut^| = 2
powershell -NoProfile -Command "[Console]::OutputEncoding=[System.Text.Encoding]::UTF8; $enc=New-Object System.Text.UTF8Encoding $false; $w=New-Object System.IO.StreamWriter($env:LOG,$false,$enc); & cmd /c ('\"' + $env:EXE + '\" ' + $env:RUNARGS + ' 2>&1') | ForEach-Object { $_; $w.WriteLine($_); $w.Flush() }; $w.Close(); exit $LASTEXITCODE"
SET "RC=%ERRORLEVEL%"
IF NOT "%RC%"=="0" echo %CASE%: stopped, exit code %RC% -- see the message above

echo.
pause
exit /b %RC%
