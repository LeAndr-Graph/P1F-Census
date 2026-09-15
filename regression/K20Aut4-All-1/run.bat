@echo off
REM ============================================================================
REM  regression\K20Aut4-All-1 -- K20 |Aut| > 3, the complete census, quick.
REM
REM  ORDER SET {19,V4,E9,S3,6,9}: every leg that PRODUCES classes, plus the two
REM  instant theorem-echo sweeps. Orders 5 and 7 are deliberately absent -- both
REM  are expected-0 sweeps and measured 1180.6s + 39.7s of a 1511s run (81% of
REM  the wall time for zero classes), which does not belong in a suite that must
REM  stay seconds-to-minutes. Order 3 is absent too: its type 6 cannot finish
REM  (~4.9e12 cover-nodes) so it runs as a random-restart harvest, which is
REM  nondeterministic and cannot be compared against a frozen baseline at all.
REM  EXPECTED: 230 distinct classes {6:168, 9:46, 18:9, 19:3, 57:1, 171:2, 342:1}
REM    (the |Aut| = 9 row is 46 = 39 C9-type + 7 C3xC3-type found by the E9 leg)
REM  from 243 emitted records -- a class found by two legs is recorded by each.
REM  V4 costs ~0s: both arithmetic V4 shapes contain a fixed-point-free
REM  involution and are SKIPPED citing the order-2 parity theorem.
REM
REM  Compared file: result.txt, carrying the real |Aut| per class -- the same
REM  layout every case uses now. The per-leg |Aut| histogram is in the log.
REM ============================================================================
SET "HERE=%~dp0"
cd /d "%HERE%"
setlocal
IF NOT DEFINED P1F_ROOT FOR %%I IN ("%HERE%..\..") DO SET "P1F_ROOT=%%~fI"
IF NOT DEFINED P1F_EXE  SET "P1F_EXE=%P1F_ROOT%\x64\Release\p1f.exe"
CALL "%P1F_ROOT%\env_reset.bat"

SET "CASE=K20Aut4-All-1"
SET "EXE=%P1F_EXE%"
SET "LOG=%HERE%%CASE%.log"
IF NOT EXIST "%EXE%" (
    echo %CASE%: %EXE% not found -- run build.bat in the repo root first.
    exit /b 2
)
REM RESULT is refused if it already exists, so a stale one from an earlier run must go first.
IF EXIST result.txt del result.txt
IF EXIST tmp.txt del tmp.txt

SET "RESULT=result.txt"
SET "REP_ORDERS=19,V4,E9,S3,6,9"

echo %CASE%: EXE=%EXE%  LOG=%CASE%.log
REM One console, live output AND a log. The engine writes unbuffered, and PowerShell
REM echoes each line to the screen while a StreamWriter appends it to %LOG%, flushed
REM per line -- so nothing has to be tailed from a second window. (Tee-Object is not
REM used: Windows PowerShell 5.1 has no -Encoding on it and would write UTF-16.)
REM If PowerShell is missing, fall back to a plain redirect and say so.
SET "RUNARGS=20 10"
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
