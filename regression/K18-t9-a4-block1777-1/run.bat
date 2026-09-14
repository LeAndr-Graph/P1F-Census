@echo off
REM ============================================================================
REM  regression\K18-t9-a4-block1777-1 -- ONE a = 4 block, the fast end-to-end
REM  check of the whole t9 a = 4 fast path (TYPEMASK + SGEN + DIAGPRUNE +
REM  oneperpair) in about half a minute.
REM  NO baseline: the block is searched from scratch and RESULT holds what it
REM  yields (REP_F3START=1777 REP_F3STOP=1777).
REM  EXPECTED: results=1 -> ONE record. The block re-finds a class OWNED by block
REM  4.0.133, so that record must appear in the frozen seed6.txt, which is kept as
REM  an INDEPENDENT oracle and is no longer an input. nodes=18241536 +/- one
REM  1024-per-worker flush quantum, as measured over blocks 1000-1999.
REM ============================================================================
SET "HERE=%~dp0"
cd /d "%HERE%"
setlocal
IF NOT DEFINED P1F_ROOT FOR %%I IN ("%HERE%..\..") DO SET "P1F_ROOT=%%~fI"
IF NOT DEFINED P1F_EXE  SET "P1F_EXE=%P1F_ROOT%\x64\Release\P1F-Census.exe"
CALL "%P1F_ROOT%\env_reset.bat"

SET "CASE=K18-t9-a4-block1777-1"
SET "EXE=%P1F_EXE%"
SET "LOG=%HERE%%CASE%.log"
IF NOT EXIST "%EXE%" (
    echo %CASE%: %EXE% not found -- run build.bat in the repo root first.
    exit /b 2
)
REM RESULT is refused if it already exists, so a stale one from an earlier run must go first.
IF EXIST result.txt del result.txt
IF EXIST tmp.txt del tmp.txt

REM No baseline: the block is searched from scratch and RESULT holds what it yields.
REM seed6.txt stays as the INDEPENDENT oracle -- the 6 classes this block must give.
SET "RESULT=result.txt"
SET "REP_ORDER=2"
SET "REP_ONLYTYPES=9"
SET "REP_LEVEL=3"
SET "REP_PRUNELEVEL=1"
SET "REP_F3A=4"
SET "REP_F3START=1777"
SET "REP_F3STOP=1777"
SET "REP_F3COUNT=1"
SET "REP_F3COMPLETE=1"
SET "REP_PATAPPLY=oneperpair"
SET "REP_TYPEMASK=1"
SET "REP_SGEN=1"
SET "REP_DIAGPRUNE=8:14"
REM  REP_PRECALC is what this case is FOR, as much as the classes are: the list walk is a
REM  second implementation of the same search, and nothing else in the suite exercises it.
REM  Its baseline was frozen from the plain path, so a pass here means the two agree on the
REM  actual matrices -- which is the only claim worth making about a rewrite of the walk.
REM  REP_DIAGPRUNE and REP_TYPEMASK above are applied INSIDE the list walk; if either is ever
REM  skipped there again, this case keeps passing and only the node count moves, so watch it.
SET "REP_PRECALC=1"

REM  Duplicate rejection is ON by default. This case checks the SEARCH -- that the fast path
REM  still finds the class block 4.0.1777 contains -- so it opts out and keeps seeing the raw
REM  re-find. Ownership itself is covered by regression\K18-t9-a4-owner-1, where 4.0.1777
REM  appears again and must be rejected as foreign.
SET "REP_OWNER=0"

echo %CASE%: EXE=%EXE%  LOG=%CASE%.log
REM One console, live output AND a log. The engine writes unbuffered, and PowerShell
REM echoes each line to the screen while a StreamWriter appends it to %LOG%, flushed
REM per line -- so nothing has to be tailed from a second window. (Tee-Object is not
REM used: Windows PowerShell 5.1 has no -Encoding on it and would write UTF-16.)
REM If PowerShell is missing, fall back to a plain redirect and say so.
SET "RUNARGS=18 10"
where powershell >nul 2>&1
IF ERRORLEVEL 1 (
    echo %CASE%: powershell not found -- output goes to %LOG% only, watch that file
    "%EXE%" %RUNARGS% > "%LOG%" 2>&1
) ELSE (
    powershell -NoProfile -Command "[Console]::OutputEncoding=[System.Text.Encoding]::UTF8; $enc=New-Object System.Text.UTF8Encoding $false; $w=New-Object System.IO.StreamWriter($env:LOG,$false,$enc); & cmd /c ('\"' + $env:EXE + '\" ' + $env:RUNARGS + ' 2>&1') | ForEach-Object { $_; $w.WriteLine($_); $w.Flush() }; $w.Close(); exit $LASTEXITCODE"
)
SET "RC=%ERRORLEVEL%"
REM  findperl.bat picks the perl -- see the reasoning there.  Without one there is no way to
REM  tell a PASS from a FAIL, so the case stops rather than reporting either.
CALL "%P1F_ROOT%\tools\findperl.bat"
IF ERRORLEVEL 1 (
    echo %CASE%: no perl found -- compare.pl needs it.  Install Strawberry Perl from
    echo %CASE%: https://strawberryperl.com/ or Git for Windows from https://git-scm.com/download/win.
    pause
    exit /b 2
)
"%PERL%" "..\compare.pl" "%CASE%" "%RESULT%" result_expected.txt "%CASE%.diff"
SET "RC=%ERRORLEVEL%"
REM FAIL only: hold the console so a double-clicked case does not vanish. A PASS falls
REM straight through, which is what lets run_all.bat call these in a loop.
IF NOT "%RC%"=="0" echo.
IF NOT "%RC%"=="0" pause
exit /b %RC%
