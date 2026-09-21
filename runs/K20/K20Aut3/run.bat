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
REM  Expected time on a 32-core PC: about 1.5 hours per block, 6.5 days for all 104.
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
IF NOT DEFINED P1F_EXE  SET "P1F_EXE=%P1F_ROOT%\x64\Release\p1f.exe"
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

REM ---- catalogue check -------------------------------------------------------
REM  A block holds a SUBSET of the census: what it finds must already be known,
REM  and that is what is checked here.  Forward direction only -- PRESENT / NOT
REM  PRESENT -- so it cannot report a class the block should have found and did
REM  not.  Valid for any block range.
REM
REM  The reference is BOTH K20 catalogues concatenated.  A block of this sweep
REM  finds every class with an order-3 automorphism, which is the |Aut| = 3 ones
REM  AND the |Aut| > 3 ones whose group order is divisible by 3 -- block 103, for
REM  instance, returns 10,133 of the first and 8 of the second.  Checked against
REM  K20_P1F_aut_eq3 alone those 8 come back "not present" and the case reports a
REM  Fault on a correct run.  The two catalogues are disjoint by construction, so
REM  concatenating them needs no dedup and is exactly "|Aut| >= 3".
REM
REM  The |Aut| = 3 catalogue ships gzipped, because at 206 MB the plain file is
REM  over GitHub's per-file limit, so expand it to a temporary copy, append the
REM  other catalogue, check against that with --against, and delete it again.
REM  --n is not passed: --against replaces the reference outright.
SET "CATGZ=%P1F_ROOT%\AllResults\K20_P1F_aut_eq3.txt.gz"
SET "CATGT3=%P1F_ROOT%\AllResults\K20_P1F_aut_gt3.txt"
SET "CATTMP=%TEMP%\K20_P1F_aut_ge3.txt"
IF NOT "%RC%"=="0" GOTO :CHECK_DONE
IF NOT EXIST "%CATGZ%" (
    echo %CASE%: %CATGZ% is not in the repository, so the check was skipped.
    GOTO :CHECK_DONE
)
CALL "%P1F_ROOT%\tools\findperl.bat" && GOTO :CHECK_EXPAND
echo.
echo %CASE%: perl was not found on PATH, so the catalogue check did not run.
echo %CASE%: the results in %RESULT% are complete and correct -- only the check was skipped.
echo %CASE%: install Strawberry Perl from https://strawberryperl.com/ or Git for Windows
echo %CASE%: from https://git-scm.com/download/win -- either one supplies it -- and run this bat
echo %CASE%: again to check them.
GOTO :CHECK_DONE
:CHECK_EXPAND
echo %CASE%: building the ^|Aut^| ^>= 3 reference in %CATTMP% ...
powershell -NoProfile -Command "$o=[IO.File]::Create($env:CATTMP); $i=[IO.File]::OpenRead($env:CATGZ); $g=New-Object IO.Compression.GzipStream($i,[IO.Compression.CompressionMode]::Decompress); $g.CopyTo($o); $g.Dispose(); $i.Dispose(); if (Test-Path $env:CATGT3) { $b=[IO.File]::OpenRead($env:CATGT3); $b.CopyTo($o); $b.Dispose() }; $o.Dispose()"
IF ERRORLEVEL 1 (
    echo %CASE%: could not build the reference -- the check was skipped, the results are unaffected.
    GOTO :CHECK_DONE
)
"%PERL%" "%P1F_ROOT%\tools\compare_to_catalog.pl" --against "%CATTMP%" "%RESULT%"
REM  2 = there was nothing to look up in, which is not a fault in the run.
REM  IF ERRORLEVEL n means "n or higher", so the 2 test has to come first.
IF ERRORLEVEL 2 GOTO :CHECK_CLEAN
IF ERRORLEVEL 1 SET "RC=3"
:CHECK_CLEAN
DEL /Q "%CATTMP%" 2>nul
:CHECK_DONE

echo.
pause
exit /b %RC%
