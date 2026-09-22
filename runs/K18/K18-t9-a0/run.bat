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
REM  A subset is self-contained -- no baseline is read, and none is needed: separately
REM  run ranges are DISJOINT and simply concatenate, with no merge and no dedup.
REM  To resume an interrupted run, take the last block the log printed and set
REM  REP_F3START to its c plus 1.
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
IF NOT DEFINED P1F_EXE  SET "P1F_EXE=%P1F_ROOT%\x64\Release\p1f.exe"
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
REM  The check writes to a temporary file so its verdict can go to BOTH the
REM  console and the log; %LOG% holds the engine's output alone otherwise, and
REM  the Compare line is what makes a disagreeing run diagnosable.
SET "CHKOUT=%TEMP%\%CASE%_check.txt"
"%PERL%" "%P1F_ROOT%\tools\compare_to_catalog.pl" --n 18 "%RESULT%" > "%CHKOUT%" 2>&1
SET "CHKRC=%ERRORLEVEL%"
TYPE "%CHKOUT%"
TYPE "%CHKOUT%" >> "%LOG%"
DEL /Q "%CHKOUT%" 2>nul
REM  2 = the catalog file is not in the repository, so there was nothing to look up in.
REM  That is not a fault in the run and must not fail the bat; only 1, a real Fault, does.
REM  GEQ 2 has to come first, for the same reason IF ERRORLEVEL did.
IF %CHKRC% GEQ 2 GOTO :CHECK_DONE
IF %CHKRC% GEQ 1 SET "RC=3"
:CHECK_DONE

echo.
pause
exit /b %RC%
