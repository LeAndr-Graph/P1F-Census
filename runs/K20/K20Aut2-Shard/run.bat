@echo off
REM ============================================================================
REM  runs\K20Aut2-Shard -- the K20 order-2 leg run as SHARDS, not as one sweep.
REM
REM  WHY THIS EXISTS.  runs\K20Aut2 ran 287 minutes and 683,098,112 nodes for
REM  ZERO classes.  That is consistent with the walk sitting in one barren region
REM  of the tree, and a single sweep has no way out of it: there is no checkpoint
REM  and no way to say "skip ahead".  The engine's answer is to partition the tree
REM  and run slices -- "position == the range you run" (k20a2rep.cpp:1516).
REM
REM  THE PARTITION.  REP_LEVEL=L enumerates the search tree level by level (L =
REM  orbit-blocks committed).  The level-L nodes are a COMPLETE, DISJOINT and
REM  DETERMINISTIC partition, so slices compose exactly: running 0:N then N:2N
REM  covers the space once, with no rework and no gap.  Both knobs are live for
REM  K20 (REP_RANGE is refused by k18 only).
REM
REM  TWO PHASES.  Set SHARD and the two REP_ vars below.
REM
REM    PHASE 1 -- COUNT.  REP_LEVEL=L with NO REP_RANGE prints the node count at
REM    each level 1..L and stops.  Cheap.  Its purpose is to size the shards: you
REM    cannot pick a sensible range without knowing how many level-L nodes exist.
REM    RUN THIS FIRST.  L is a parameter here and NOT a guess -- pick it from the
REM    counts (a level whose node count is large enough to slice finely, small
REM    enough to enumerate).
REM
REM    PHASE 2 -- SWEEP.  Add REP_RANGE.  Two forms:
REM      start:end                      process that slice, plainly
REM      start:end:step(timeout,results)  CHUNKED DENSITY SWEEP -- walk in
REM        step-block chunks, advancing the instant EITHER cap is hit (timeout
REM        seconds OR results new classes), checked MID-BLOCK so a barren block is
REM        abandoned rather than finished.  Prints per chunk:
REM            +classes ... TIMED-OUT | RESULT-CAP | range-done
REM
REM  THE CHUNKED FORM IS THE POINT.  For an existence hunt it beats one long
REM  sweep: instead of six hours in one region you sample many, and the per-chunk
REM  line tells you where density is non-zero.  Either cap may be 0 to disable it.
REM
REM  WHAT IT STILL CANNOT DO.  Nothing here makes the cell smaller.  |C(alpha)| is
REM  about 3.7e8 for 2^9.1^2 and every order-2 type is OVER-CAP by construction
REM  (k20a2rep.cpp:32), so the per-node dedup cost is unchanged.  This buys
REM  COVERAGE and the ability to stop and resume, not speed.
REM
REM  PER-SHARD FILES, SO NOTHING IS OVERWRITTEN.  The log and the result file are
REM  named for SHARD.  Successive shards never collide, and no shard's results are
REM  ever destroyed to make room for the next -- which matters here, because a
REM  class is written the moment it is found and its result file is the only copy.
REM
REM  NO COMPARE STEP, for the same reason as the parent case: AllResults holds
REM  only K20_P1F_aut_gt3.txt, so a correct run would report Compare Fault.
REM ============================================================================
SET "HERE=%~dp0"
cd /d "%HERE%"
setlocal
IF NOT DEFINED P1F_ROOT FOR %%I IN ("%HERE%..\..\..") DO SET "P1F_ROOT=%%~fI"
IF NOT DEFINED P1F_EXE  SET "P1F_EXE=%P1F_ROOT%\x64\Release\P1F-Census.exe"
CALL "%P1F_ROOT%\env_reset.bat"

SET "CASE=K20Aut2-Shard"
SET "EXE=%P1F_EXE%"

REM ---- the three knobs -------------------------------------------------------
REM  SHARD      a short label; names the log and the result file
REM  REP_LEVEL  partition level (PHASE 1 measures which L is usable)
REM  REP_RANGE  leave UNSET for PHASE 1 (count only); set it for PHASE 2
REM
REM  PHASE 1 IS DONE.  Measured 2026-09-03, shard label "count":
REM      SETUP total          1.1s   <- collectTasks n=5937, everything else 0.0s
REM      LEVEL 1  branches       5   (0.0s)
REM      LEVEL 2  branches    7944   (2005.9s = 33.4 min, parallel)
REM  Two things follow, and they set the knobs below.
REM
REM  (a) SETUP IS NOT THE COST.  1.1 seconds, so there is nothing there worth
REM      caching.  The parent case's "37 minutes of silence" was the tree walk
REM      itself grinding through the first two orbit-block levels at roughly half
REM      a node per second -- its first ~ row read 1,024 nodes at 37 min, then
REM      1.96M in the next five, because deeper subtrees are far cheaper per node.
REM
REM  (b) LEVEL 2 IS THE SHARD LEVEL, and going deeper would be worse.  7944
REM      branches is ample granularity, and EVERY invocation must rebuild the
REM      frontier up to L before it can process any slice -- 33 min at L=2, and
REM      33 min PLUS level 3 at L=3.  Shallow keeps the fixed cost per run down.
REM      The usual reason to go deeper (uneven subtrees trapping one shard) is
REM      already handled: the chunk timeout is checked MID-BLOCK.
REM
REM  Level 3 was killed unfinished: 17 min with nothing printed, and expanding 5
REM  level-1 branches cost ~400s each, so 7944 of them is not an hours-scale job.
REM  2026-09-03 evening: FRESH SLICE + RAWSHARD, sized for a ~3 hour run.
REM    - range 4000:7944 is the half of the level-2 frontier nothing has touched;
REM      the plain sweep and the first shard run both worked the low end.
REM    - REP_RAWSHARD=1 skips the per-node setwiseStab (the dominant cost at
REM      |C(alpha)| ~ 3.7e8), exploring a SUPERSET of branches instead.  The engine's
REM      own note: "a superset of branches can never miss a class ... use for fast
REM      sharding/harvest".  Faster per node, same class set.
REM    - NO chunk form.  The (900,1) chunked sweep printed not ONE line in 164
REM      minutes and that is still unexplained -- not a thing to build a long run on.
REM  Shard mode peaked at 257 MB where the dive reached 6.8 GB, so this is the
REM  memory-safe way to spend hours on this cell.
SET "SHARD=L2_raw_4000"
SET "REP_RAWSHARD=1"
SET "REP_LEVEL=2"
REM  PHASE 2, PRESENCE HUNT.  40 chunks of 200 branches over the 7944, each
REM  abandoned after 15 minutes OR 1 new class, whichever comes first, checked
REM  MID-BLOCK.  Tight caps are deliberate: for "is there an |Aut| = 2 at all",
REM  moving on early maximises how many regions get sampled, and the parent case
REM  showed that sitting in one region for six hours yields nothing.  All 40 run
REM  inside ONE process, so the 33-minute frontier rebuild is paid once, not 40
REM  times.  Every chunk prints +classes and its stop reason, so the log becomes a
REM  density map of the cell -- the thing the 377-minute run could not produce.
SET "REP_RANGE=4000:7944"

SET "REP_ORDER=2"
REM  8 threads, not 10: a 10-thread K20 order-2 run drove the machine to a
REM  near-crash on 2026-09-03.  Leave headroom -- this case runs for hours.
SET "RUNARGS=20 8"
REM ---------------------------------------------------------------------------

SET "LOG=%HERE%%CASE%_%SHARD%.log"
SET "RESULT=result_%SHARD%.txt"
IF EXIST "%LOG%" echo %CASE%: %LOG% already exists -- shard "%SHARD%" has been run; pick another SHARD label or move that log.
IF EXIST "%LOG%" pause
IF EXIST "%LOG%" exit /b 1

echo %CASE%: EXE=%EXE%  LOG=%CASE%_%SHARD%.log  RESULT=%RESULT%
REM  REP_RANGE is echoed QUOTED: its (timeout,results) parentheses are literal to
REM  echo, but quoting keeps them away from any future parenthesised IF block.
echo %CASE%: shard "%SHARD%"  REP_LEVEL=%REP_LEVEL%  REP_RANGE="%REP_RANGE%"
IF "%REP_RANGE%"=="" echo %CASE%: PHASE 1 -- counting level nodes only, no search
IF NOT "%REP_RANGE%"=="" echo %CASE%: PHASE 2 -- sweeping the slice
powershell -NoProfile -Command "[Console]::OutputEncoding=[System.Text.Encoding]::UTF8; $enc=New-Object System.Text.UTF8Encoding $false; $w=New-Object System.IO.StreamWriter($env:LOG,$false,$enc); & cmd /c ('\"' + $env:EXE + '\" ' + $env:RUNARGS + ' 2>&1') | ForEach-Object { $_; $w.WriteLine($_); $w.Flush() }; $w.Close(); exit $LASTEXITCODE"
SET "RC=%ERRORLEVEL%"
IF NOT "%RC%"=="0" echo %CASE%: stopped, exit code %RC% -- see the message above

echo.
pause
exit /b %RC%
