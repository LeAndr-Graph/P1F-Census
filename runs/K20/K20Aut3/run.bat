@echo off
REM ============================================================================
REM  runs\K20Aut3 -- sweeps the K20 order-3 cell, symmetry type 3^6.1^2, and
REM  writes every P1F class it finds to the result file.  It ends by itself.
REM
REM  The cell is divided into 104 independent blocks, numbered 0 to 103.  Any
REM  set of them may be run, in any order, on any machine; results concatenate.
REM
REM  BLOCK SIZE: block 0 is 6.62e10 nodes and 13,616 classes.  Blocks are uneven,
REM  and this is one sample of the 104.
REM
REM  It took 17.1 hours on 30 threads on 2026-09-08, but do NOT plan against that:
REM  the work queue in use then left 28 of 30 threads spinning and the run spent
REM  most of its life at a fifteenth of its own peak rate.  That queue was replaced
REM  on 2026-09-09 -- each thread now takes the next branch of the block in sequence
REM  -- and the same nodes at the peak rate would be about 1.9 hours.  No block has
REM  been run end to end since, so time one before scheduling the other 103.
REM
REM  TO CHOOSE THE INTERVAL OF BLOCKS TO PROCESS, edit BStart and BLast below.
REM  Both are inclusive and counted from 0: BStart=5 BLast=6 runs blocks 5 and
REM  6, and BStart=BLast runs a single block.  Threads are the second number of
REM  RUNARGS.  Nothing else in this file needs changing.
REM
REM  ReadMe.md describes the printout.
REM ============================================================================
SET "HERE=%~dp0"
cd /d "%HERE%"
setlocal
IF NOT DEFINED P1F_ROOT FOR %%I IN ("%HERE%..\..\..") DO SET "P1F_ROOT=%%~fI"
IF NOT DEFINED P1F_EXE  SET "P1F_EXE=%P1F_ROOT%\x64\Release\P1F-Census.exe"
CALL "%P1F_ROOT%\env_reset.bat"

SET "CASE=K20Aut3"
SET "EXE=%P1F_EXE%"

REM ---- the blocks to process: both INCLUSIVE, counted from 0 -----------------
SET "BStart=0"
SET "BLast=0"

SET "REP_LEVEL=1"
SET "REP_ORDER=3"
SET "REP_PRUNELEVEL=1"
SET "REP_PRECALC=1"

REM ---- N and thread count ----------------------------------------------------
SET "RUNARGS=20 8"
REM ---------------------------------------------------------------------------

REM ---- derived from BStart / BLast; do not edit -------------------------------
SET /A BEnd=%BLast%+1
SET "REP_RANGE=%BStart%:%BEnd%"
SET "PS=00%BStart%"
SET "PL=00%BLast%"
SET "SHARD=L%REP_LEVEL%_b%PS:~-3%-%PL:~-3%"
SET "LOG=%HERE%%CASE%_%SHARD%.log"
SET "RESULT=result_%SHARD%.txt"

IF EXIST "%LOG%" echo %CASE%: %LOG% already exists -- blocks %BStart%..%BLast% have been run; move that log aside to run them again.
IF EXIST "%LOG%" pause
IF EXIST "%LOG%" exit /b 1

echo %CASE%: EXE=%EXE%  LOG=%CASE%_%SHARD%.log  RESULT=%RESULT%
echo %CASE%: blocks %BStart%..%BLast% (inclusive)  REP_LEVEL=%REP_LEVEL%  REP_RANGE="%REP_RANGE%"
powershell -NoProfile -Command "[Console]::OutputEncoding=[System.Text.Encoding]::UTF8; $enc=New-Object System.Text.UTF8Encoding $false; $w=New-Object System.IO.StreamWriter($env:LOG,$false,$enc); & cmd /c ('\"' + $env:EXE + '\" ' + $env:RUNARGS + ' 2>&1') | ForEach-Object { $_; $w.WriteLine($_); $w.Flush() }; $w.Close(); exit $LASTEXITCODE"
SET "RC=%ERRORLEVEL%"
IF NOT "%RC%"=="0" echo %CASE%: stopped, exit code %RC% -- see the message above

echo.
pause
exit /b %RC%
