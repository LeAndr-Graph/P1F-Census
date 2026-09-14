@echo off
REM ============================================================================
REM  runs\K20Aut2 -- the K20 order-2 leg, as a CAPPED HARVEST of 100 classes.
REM
REM  WHAT THIS IS.  A sample, not a census.  REP_ORDER=2:100 stops the run the
REM  moment the 100th distinct class is found.  The complete order-2 search does
REM  not finish at K20 -- K20Aut4-All says so in its own header -- so this case
REM  never attempts it.  Nothing here is evidence that the sample is complete --
REM  it stopped looking.  No RNG is involved, though: the randomized dive mode is
REM  opt-in via REP_DIVE and this bat does not set it.  But unlike K20Aut3, this
REM  case is OVER-CAP -- k20a2rep.cpp:32 names "every order-2 type" -- so the work
REM  goes through a shared work-queue whose pop order depends on thread timing,
REM  and a re-run need not reach a given subtree at the same point.
REM
REM  MEASURED 2026-09-03: 377 min, 878,635,008 nodes, ZERO classes, ~37 min of
REM  that setup before the first node.  See runs\K20Aut2-Shard for the sharded
REM  form, which is restartable and cannot spend six hours in one barren region.
REM
REM  WHAT THE 100 COUNTS.  Distinct classes CONTAINING AN INVOLUTION -- any group
REM  order, not |Aut| = 2 only.  This is the same semantics that made K16Aut2's
REM  60 come out as {2:59, 14:1}.  At K20, 178 of the 230 catalogued classes have
REM  EVEN group order (168 at |Aut| 6, 9 at 18, 1 at 342) and every one holds an
REM  involution, and nothing in the engine excludes them: the only |Aut| reject is
REM  g_aut2Only under REP_F3COMPLETE, which exists in the K14 and K18 engines only.
REM  Even so they are not expected to crowd the quota -- K20Aut3's July harvest of
REM  about 600 classes held exactly ONE with |Aut| = 6, the |Aut| = 3 stratum being
REM  large enough that the search never lands on a catalogued class.  So the |Aut|
REM  histogram on the = TOTAL row IS the result: dominated by 2 settles the open
REM  problem; dominated by 6/18/342 is evidence the |Aut| = 2 stratum is thin.
REM
REM  THE TARGET IS REACHABLE.  Those 178 exist, so the run cannot hang waiting
REM  for a hundredth class that is not there.  How LONG it takes to reach 100 has
REM  not been measured -- see ReadMe.md.
REM
REM  WHY IT IS WORTH RUNNING.  An |Aut| = 2 row in the histogram settles an open
REM  problem: whether K20 has a P1F whose group is exactly order 2.  That is
REM  problem (ii) of the companion paper and no run has answered it.
REM
REM  ONE CYCLE TYPE.  2^9.1^2 is the only non-empty involution type at K20.  The
REM  fixed-point-free 2^10 is empty by the order-2 parity theorem, and an
REM  involution of a P1F fixes at most 2 vertices, which retires the rest.
REM
REM  NO COMPARE STEP, DELIBERATELY.  Every other case bat ends by looking its
REM  results up in an archived catalog.  This one cannot: AllResults holds only
REM  K20_P1F_aut_gt3.txt, scope |Aut| > 3, so every harvested |Aut| = 2 class
REM  would come back "not present" and a CORRECT run would report Compare Fault
REM  and exit 3.  There is no catalog for this stratum to look up in.
REM
REM  RESULT is refused if it already exists, so a re-run needs the old one out of
REM  the way.  MOVE IT, DO NOT DELETE IT.  An |Aut| = 2 record would be the answer
REM  to open problem (ii), each class is written the moment it is found, and
REM  result.txt is the only copy.
REM ============================================================================
SET "HERE=%~dp0"
cd /d "%HERE%"
setlocal
IF NOT DEFINED P1F_ROOT FOR %%I IN ("%HERE%..\..\..") DO SET "P1F_ROOT=%%~fI"
IF NOT DEFINED P1F_EXE  SET "P1F_EXE=%P1F_ROOT%\x64\Release\P1F-Census.exe"
CALL "%P1F_ROOT%\env_reset.bat"

SET "CASE=K20Aut2"
SET "EXE=%P1F_EXE%"
SET "LOG=%HERE%%CASE%.log"
IF EXIST "%LOG%" echo %CASE%: %LOG% already exists -- this case has been run; delete the log to run it again.
IF EXIST "%LOG%" pause
IF EXIST "%LOG%" exit /b 1

SET "RESULT=result.txt"
SET "REP_ORDER=2:100"

REM  8 threads, not 10: a 10-thread K20 order-2 run drove the machine to a
REM  near-crash on 2026-09-03.  Leave headroom -- this case runs for hours.
SET "RUNARGS=20 8"
echo %CASE%: EXE=%EXE%  LOG=%CASE%.log
echo %CASE%: order-2 harvest, stopping at 100 distinct classes -- a SAMPLE, not a census
echo %CASE%: wall time is NOT measured for this case; it may run long
powershell -NoProfile -Command "[Console]::OutputEncoding=[System.Text.Encoding]::UTF8; $enc=New-Object System.Text.UTF8Encoding $false; $w=New-Object System.IO.StreamWriter($env:LOG,$false,$enc); & cmd /c ('\"' + $env:EXE + '\" ' + $env:RUNARGS + ' 2>&1') | ForEach-Object { $_; $w.WriteLine($_); $w.Flush() }; $w.Close(); exit $LASTEXITCODE"
SET "RC=%ERRORLEVEL%"
IF NOT "%RC%"=="0" echo %CASE%: stopped, exit code %RC% -- see the message above

echo.
pause
exit /b %RC%
