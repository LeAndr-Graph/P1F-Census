@echo off
REM ============================================================================
REM  regression\K14Aut3-All-1 -- K14 |Aut| > 1, the engine's structural
REM  validator. K14 (2n = 14 == 2 mod 4, like K18) has the fixed-point-free even
REM  cycle types 2^7 and 4^3.2^1 that K16 lacks, and being small it COMPLETES
REM  order 2 -- the case that is intractable at K16/K18/K20.
REM  ORDER SET {2,3,7,13} = the primes dividing any K14 |Aut|; their union is the
REM  full |Aut| > 1 classification.
REM  EXPECTED: 21 classes {2:3, 3:5, 4:1, 6:5, 12:5, 84:1, 156:1}
REM    (order-2 leg alone -> 16 {2:3,4:1,6:5,12:5,84:1,156:1}, proven complete).
REM  35 records are emitted for the 21 distinct classes: a class found by several
REM  order legs (|Aut| = 6, 12, 84, 156) is recorded once per leg.
REM ============================================================================
SET "HERE=%~dp0"
cd /d "%HERE%"
setlocal
IF NOT DEFINED P1F_ROOT FOR %%I IN ("%HERE%..\..") DO SET "P1F_ROOT=%%~fI"
IF NOT DEFINED P1F_EXE  SET "P1F_EXE=%P1F_ROOT%\x64\Release\P1F-Census.exe"
CALL "%P1F_ROOT%\env_reset.bat"

SET "CASE=K14Aut3-All-1"
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
SET "REP_ORDERS=2,3,7,13"

echo %CASE%: EXE=%EXE%  LOG=%CASE%.log
REM One console, live output AND a log. The engine writes unbuffered, and PowerShell
REM echoes each line to the screen while a StreamWriter appends it to %LOG%, flushed
REM per line -- so nothing has to be tailed from a second window. (Tee-Object is not
REM used: Windows PowerShell 5.1 has no -Encoding on it and would write UTF-16.)
REM If PowerShell is missing, fall back to a plain redirect and say so.
SET "RUNARGS=14 10"
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
