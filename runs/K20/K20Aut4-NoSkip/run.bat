@echo off
REM ============================================================================
REM  runs\K20Aut4-NoSkip -- the theorems of K20Aut4-All, re-derived BY SEARCH.
REM
REM  WHAT IT ASSERTS.  Orders 4, 11, 13 and 17 admit no P1F of K20.  The main case
REM  does not search them: it SKIPS them citing the order-4 parity theorem and the
REM  odd-prime parity theorem.  This case turns those four proofs into a result the
REM  suite re-checks on every build.  A correct run finds ZERO classes.
REM
REM      = TOTAL    |   ...   |   ... | ... | 0
REM
REM  Anything other than 0 means a theorem is being misapplied -- either the proof
REM  is wrong or the predicate that implements it skips a type it should not.  The
REM  predicate is the likelier of the two and no test covered it before this one.
REM
REM  WHY IT IS WORTH A CASE.  Measured 2026-09-03, and the numbers are the argument:
REM
REM      orders 11,13,17 searched      0.0s     362 nodes     0 classes
REM      order 4, all 25 types        32.6s      62 nodes     0 classes
REM      orders 6+9 with NO skip      93.8s   1,653,362 nodes  (vs 31.2s / 1,653,307)
REM
REM  Searching every type the skips retire costs about TWO MINUTES across the whole
REM  |Aut| > 3 case, and adds 55 nodes -- the retired types die at the root, so the
REM  extra time is per-type setup, not search.  The skip buys almost nothing here.
REM
REM  THE ONE SKIP THAT IS LOAD-BEARING is order-3 type 3^5.1^5: about 4.9e12
REM  cover-nodes, and the 32-core grind never finished it.  That is what the
REM  odd-prime parity theorem was written to retire, and it stays retired.  Nothing
REM  in this case touches it.
REM
REM  WHY TWO PHASES.  REP_ONLYTYPES applies its indices within EVERY order, and
REM  order 3 needs it -- type 5 is the intractable 3^5.1^5 and type 6 is 3^6.1^2,
REM  the open |Aut| = 3 cell, and neither may be searched.  But order 4 has 25 types
REM  and needs all of them, so it cannot share a run with that restriction.  Orders
REM  11, 13 and 17 have exactly ONE type each (index 1), so they ride along with
REM  order 3 for free.  Hence: phase A = 3(types 1,3), 11, 13, 17;  phase B = 4.
REM  Both write into the one log, phase B appending.
REM  DO NOT widen REP_ONLYTYPES beyond 1,3.
REM
REM  READING THE LOG.  A type row saying "skipped" means one of TWO things, and the
REM  SETUP ticks tell them apart:
REM      no SETUP ticks              -> retired by a theorem before any work
REM      SETUP collectTasks n=0      -> SEARCHED, and empty at the root
REM  Under REP_NOSKIP every row should be the second kind.  That distinction is the
REM  whole point of this case, and it is why the ticks exist.
REM
REM  NO COMPARE STEP: a run that writes nothing has nothing to look up.  The check
REM  is the = TOTAL row reading 0.
REM
REM  RESULT is refused if it already exists -- move it aside to run again.
REM ============================================================================
SET "HERE=%~dp0"
cd /d "%HERE%"
setlocal
IF NOT DEFINED P1F_ROOT FOR %%I IN ("%HERE%..\..\..") DO SET "P1F_ROOT=%%~fI"
IF NOT DEFINED P1F_EXE  SET "P1F_EXE=%P1F_ROOT%\x64\Release\P1F-Census.exe"
CALL "%P1F_ROOT%\env_reset.bat"

SET "CASE=K20Aut4-NoSkip"
SET "EXE=%P1F_EXE%"
SET "LOG=%HERE%%CASE%.log"
IF EXIST "%LOG%" echo %CASE%: %LOG% already exists -- this case has been run; delete the log to run it again.
IF EXIST "%LOG%" pause
IF EXIST "%LOG%" exit /b 1

SET "REP_NOSKIP=1"
SET "RUNARGS=20 10"
echo %CASE%: EXE=%EXE%  LOG=%CASE%.log
echo %CASE%: searching the types K20Aut4-All retires by theorem -- BOTH phases must give 0

REM  PHASE A -- order 3 types 1 and 3, plus orders 11, 13, 17.
REM  These share a run because REP_ONLYTYPES indexes within EVERY order and 11, 13
REM  and 17 have exactly ONE type each, which is index 1.  Order 3 is the reason the
REM  restriction is needed at all: type 5 is the intractable 3^5.1^5 and type 6 is
REM  3^6.1^2, the open |Aut| = 3 cell.  Neither may be searched here.
SET "RESULT=result_a.txt"
SET "REP_ORDERS=3,11,13,17"
SET "REP_ONLYTYPES=1,3"
echo %CASE%: phase A -- orders 3(types 1,3), 11, 13, 17
powershell -NoProfile -Command "[Console]::OutputEncoding=[System.Text.Encoding]::UTF8; $enc=New-Object System.Text.UTF8Encoding $false; $w=New-Object System.IO.StreamWriter($env:LOG,$false,$enc); & cmd /c ('\"' + $env:EXE + '\" ' + $env:RUNARGS + ' 2>&1') | ForEach-Object { $_; $w.WriteLine($_); $w.Flush() }; $w.Close(); exit $LASTEXITCODE"
SET "RCA=%ERRORLEVEL%"
IF NOT "%RCA%"=="0" echo %CASE%: phase A stopped, exit code %RCA%

REM  PHASE B -- order 4, ALL 25 types, so REP_ONLYTYPES must be cleared first.
REM  In cmd, SET "VAR=" removes the variable (it does not set it empty), which is
REM  what the engine needs to see.  The log is APPENDED so one file holds both.
SET "REP_ONLYTYPES="
SET "RESULT=result_b.txt"
SET "REP_ORDERS=4"
echo %CASE%: phase B -- order 4, all 25 types
powershell -NoProfile -Command "[Console]::OutputEncoding=[System.Text.Encoding]::UTF8; $enc=New-Object System.Text.UTF8Encoding $false; $w=New-Object System.IO.StreamWriter($env:LOG,$true,$enc); & cmd /c ('\"' + $env:EXE + '\" ' + $env:RUNARGS + ' 2>&1') | ForEach-Object { $_; $w.WriteLine($_); $w.Flush() }; $w.Close(); exit $LASTEXITCODE"
SET "RCB=%ERRORLEVEL%"
IF NOT "%RCB%"=="0" echo %CASE%: phase B stopped, exit code %RCB%

SET "RC=%RCA%"
IF NOT "%RCB%"=="0" SET "RC=%RCB%"

echo.
echo %CASE%: PASS requires BOTH "= TOTAL" rows in %CASE%.log to read 0 classes,
echo %CASE%: and both result_a.txt and result_b.txt to be empty.
echo.
pause
exit /b %RC%
