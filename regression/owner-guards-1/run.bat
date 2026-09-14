@echo off
REM ============================================================================
REM  regression\owner-guards-1 -- REP_OWNER must be REFUSED where it cannot work.
REM
REM  Ownership is defined against the k18 block column: the owner of a class is the
REM  minimum BLOCK index over its column labelings. Only k18 has blocks (REP_F3COMPLETE,
REM  childReps, the a.b.c coordinate). On any other N, and on k18 without the block
REM  driver, there is no column and nothing to own.
REM
REM  Ignoring the variable there is the dangerous option, not the safe one: someone who
REM  sets REP_OWNER to shard a k14 or k16 sweep would believe the outputs are disjoint
REM  when they are ordinary duplicate-prone results, and a plain concatenation would
REM  over-count. So the run stops, with exit code 1 and before RESULT is created.
REM
REM  Every case here is instant -- the guards fire before any search starts.
REM ============================================================================
SET "HERE=%~dp0"
cd /d "%HERE%"
setlocal
IF NOT DEFINED P1F_ROOT FOR %%I IN ("%HERE%..\..") DO SET "P1F_ROOT=%%~fI"
IF NOT DEFINED P1F_EXE  SET "P1F_EXE=%P1F_ROOT%\x64\Release\P1F-Census.exe"
CALL "%P1F_ROOT%\env_reset.bat"

SET "CASE=owner-guards-1"
SET "EXE=%P1F_EXE%"
IF NOT EXIST "%EXE%" (
    echo %CASE%: %EXE% not found -- run build.bat in the repo root first.
    exit /b 2
)
SET "LOG=%HERE%%CASE%.log"
IF EXIST "%LOG%" del "%LOG%"
FOR %%F IN (g14.txt g16.txt g20.txt g18.txt g14all.txt) DO IF EXIST %%F del %%F

REM  tag        N   result file   the variable under test, plus enough to reach a real run.
REM  The VAR=VAL pairs MUST be quoted: cmd splits arguments on "=" as well as space, so an
REM  unquoted REP_OWNER=1 arrives as two arguments and the variable is never set.
CALL :GUARD k14      14  g14.txt    "REP_ORDERS=2"  "REP_OWNER=1"
CALL :GUARD k16      16  g16.txt    "REP_ORDERS=3"  "REP_OWNER=1"
CALL :GUARD k20      20  g20.txt    "REP_ORDERS=19" "REP_OWNER=1"
CALL :GUARD k18nodrv 18  g18.txt    "REP_ORDER=2"   "REP_OWNER=1"
CALL :GUARD k14all   14  g14all.txt "REP_ORDERS=2"  "REP_OWNERALL=1"

echo.
echo %CASE%: checking...
REM  findperl.bat picks the perl -- see the reasoning there.  Without one there is no way to
REM  tell a PASS from a FAIL, so the case stops rather than reporting either.
CALL "%P1F_ROOT%\tools\findperl.bat"
IF ERRORLEVEL 1 (
    echo %CASE%: no perl found -- check.pl needs it.  Install Strawberry Perl from
    echo %CASE%: https://strawberryperl.com/ or Git for Windows from https://git-scm.com/download/win.
    pause
    exit /b 2
)
"%PERL%" check.pl
SET "RC=%ERRORLEVEL%"
IF NOT "%RC%"=="0" echo.
IF NOT "%RC%"=="0" pause
exit /b %RC%

REM ---- :GUARD <tag> <N> <resultfile> <VAR=VAL> <VAR=VAL> ----------------------
REM Runs the engine with just those two variables set, then records the exit code and
REM whether RESULT was created. Both matter: a guard that prints but returns 0, or that
REM fires after the output file is opened, is not a guard.
:GUARD
setlocal
SET "TAG=%~1"
SET "NN=%~2"
SET "RESULT=%~3"
FOR /F "tokens=1,2 delims==" %%A IN ("%~4") DO SET "%%A=%%B"
FOR /F "tokens=1,2 delims==" %%A IN ("%~5") DO SET "%%A=%%B"
echo ---- %TAG%: N=%NN% %~4 %~5 >> "%LOG%"
"%EXE%" %NN% 4 >> "%LOG%" 2>&1
SET "RC=%ERRORLEVEL%"
IF EXIST "%~3" (SET "FILE=present") ELSE (SET "FILE=absent")
echo VERDICT %TAG% rc=%RC% result=%FILE% >> "%LOG%"
echo   %TAG%: rc=%RC%, RESULT %FILE%
endlocal
exit /b 0
