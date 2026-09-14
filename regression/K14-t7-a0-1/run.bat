@echo off
REM ============================================================================
REM  regression\K14-t7-a0-1 -- guards the search engine used by K18-t9-a0 and K18-t9-a4.
REM  K14 type t7 = 2^7 (fixed-point-free), branch a=0, one block column through the same block
REM  driver K18 uses.  EXPECTED: 0 record(s), compared against
REM  result_expected.txt.
REM ============================================================================
SET "HERE=%~dp0"
cd /d "%HERE%"
setlocal
IF NOT DEFINED P1F_ROOT FOR %%I IN ("%HERE%..\..") DO SET "P1F_ROOT=%%~fI"
IF NOT DEFINED P1F_EXE  SET "P1F_EXE=%P1F_ROOT%\x64\Release\P1F-Census.exe"
CALL "%P1F_ROOT%\env_reset.bat"

SET "CASE=K14-t7-a0-1"
SET "EXE=%P1F_EXE%"
SET "LOG=%HERE%%CASE%.log"
IF NOT EXIST "%EXE%" (
    echo %CASE%: %EXE% not found -- run build.bat in the repo root first.
    exit /b 2
)
IF EXIST result.txt del result.txt

SET "RESULT=result.txt"
SET "REP_ORDERS=2"
SET "REP_ONLYTYPES=7"
SET "REP_LEVEL=3"
SET "REP_PRUNELEVEL=1"
SET "REP_F3A=0"
SET "REP_F3COUNT=1"
SET "REP_F3COMPLETE=1"
SET "REP_F3START=0"
SET "REP_F3STOP=451"
SET "RUNARGS=14 10"

echo %CASE%: EXE=%EXE%  LOG=%CASE%.log
where powershell >nul 2>&1
IF ERRORLEVEL 1 (
    echo %CASE%: powershell not found -- output goes to %LOG% only
    "%EXE%" %RUNARGS% > "%LOG%" 2>&1
) ELSE (
    powershell -NoProfile -Command "[Console]::OutputEncoding=[System.Text.Encoding]::UTF8; $enc=New-Object System.Text.UTF8Encoding $false; $w=New-Object System.IO.StreamWriter($env:LOG,$false,$enc); & cmd /c ('\"' + $env:EXE + '\" ' + $env:RUNARGS + ' 2>&1') | ForEach-Object { $_; $w.WriteLine($_); $w.Flush() }; $w.Close(); exit $LASTEXITCODE"
)
REM  A case that expects no records still writes an empty RESULT; create it if the
REM  engine wrote nothing, so compare.pl has both sides to compare.
IF NOT EXIST result.txt type nul > result.txt
CALL "%P1F_ROOT%\tools\findperl.bat"
IF ERRORLEVEL 1 (
    echo %CASE%: no perl found -- compare.pl needs it.
    pause
    exit /b 2
)
"%PERL%" "..\compare.pl" "%CASE%" "%RESULT%" result_expected.txt "%CASE%.diff"
SET "RC=%ERRORLEVEL%"
REM  The classes are the coarse signal; the node count is the sensitive one.  This case writes
REM  no classes at all, so WITHOUT the node check it would pass even if the branch stopped being
REM  searched entirely -- an empty result is indistinguishable from an empty search.
IF NOT "%RC%"=="0" GOTO :VERDICT
"%PERL%" "..\check_nodes.pl" "%CASE%" "%LOG%" 13656
IF ERRORLEVEL 1 SET "RC=4"
:VERDICT
IF NOT "%RC%"=="0" echo.
IF NOT "%RC%"=="0" pause
exit /b %RC%
