@echo off
REM ============================================================================
REM  regression\K18-t9-a4-owner-1 -- the owner filter (REP_OWNER), end to end.
REM
REM  A class is reachable from MANY blocks, so a sweep re-finds it over and over.
REM  Ownership settles which single block may write it: the class's owner is the
REM  minimum block index over all of its column labelings, a property of the class
REM  and the column, so every block computes the same value and exactly one block
REM  matches. Everything else is rejected -- with no baseline file and no memory of
REM  classes found in other blocks.
REM
REM  Four blocks, chosen to cover every path the filter has (about 3 minutes total):
REM
REM    4.0.133    1 cover,  owns class #1        -> results=1  saved=1  duplicates=0
REM    4.0.1777   1 cover,  class #1, owner 133  -> results=1  saved=0  duplicates=1 foreign
REM    4.0.2237   1 cover,  owns P47 (|Aut|=16)  -> results=1  saved=0  duplicates=1 |Aut|>2
REM    4.0.17607  2 covers, class #2, owner 182  -> results=2  saved=0  duplicates=2
REM                                                 (1 foreign + 1 in-block)
REM
REM  4.0.17607 is why the per-block set exists: the block really does yield the same
REM  class twice, and both covers have the SAME owner, so ownership cannot separate
REM  them -- only the canonized class key can. 4.0.1777 is why ownership exists: a
REM  class this block contains but does not own, which the old global set could only
REM  reject by remembering every class found in every other block. 4.0.2237 is the one
REM  known a=4 class with |Aut| > 2, so it is the only check that the general
REM  involution enumeration -- not the tau = sigma0 shortcut -- gets the owner right.
REM
REM  Node counts must be unchanged by the filter: it drops results, it never prunes.
REM ============================================================================
SET "HERE=%~dp0"
cd /d "%HERE%"
setlocal
IF NOT DEFINED P1F_ROOT FOR %%I IN ("%HERE%..\..") DO SET "P1F_ROOT=%%~fI"
IF NOT DEFINED P1F_EXE  SET "P1F_EXE=%P1F_ROOT%\x64\Release\p1f.exe"
CALL "%P1F_ROOT%\env_reset.bat"

SET "CASE=K18-t9-a4-owner-1"
SET "EXE=%P1F_EXE%"
IF NOT EXIST "%EXE%" (
    echo %CASE%: %EXE% not found -- run build.bat in the repo root first.
    exit /b 2
)
REM RESULT is refused if it already exists, so stale files from an earlier run must go.
FOR %%F IN (result133.txt result1777.txt result2237.txt result17607.txt) DO IF EXIST %%F del %%F

SET "REP_ORDER=2"
SET "REP_ONLYTYPES=9"
SET "REP_LEVEL=3"
SET "REP_PRUNELEVEL=1"
SET "REP_F3A=4"
SET "REP_F3COUNT=1"
SET "REP_F3COMPLETE=1"
SET "REP_PATAPPLY=oneperpair"
SET "REP_TYPEMASK=1"
SET "REP_SGEN=1"
SET "REP_DIAGPRUNE=8:14"
REM  The list walk, under the owner filter. Ownership decides which block WRITES a class, and
REM  the list walk changes the order classes are found in, so the two interact: this case is
REM  the one that would catch an owner verdict that depends on discovery order.
SET "REP_PRECALC=1"
SET "REP_OWNER=1"
SET "REP_OWNERALL=1"

CALL :BLOCK 133
IF ERRORLEVEL 1 EXIT /B 2
CALL :BLOCK 1777
IF ERRORLEVEL 1 EXIT /B 2
CALL :BLOCK 2237
IF ERRORLEVEL 1 EXIT /B 2
CALL :BLOCK 17607
IF ERRORLEVEL 1 EXIT /B 2

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

:BLOCK
SET "REP_F3START=%~1"
SET "REP_F3STOP=%~1"
SET "RESULT=result%~1.txt"
SET "LOG=%HERE%owner%~1.log"
echo %CASE%: EXE=%EXE%  LOG=owner%~1.log
powershell -NoProfile -Command "[Console]::OutputEncoding=[System.Text.Encoding]::UTF8; $enc=New-Object System.Text.UTF8Encoding $false; $w=New-Object System.IO.StreamWriter($env:LOG,$false,$enc); & cmd /c ('\"' + $env:EXE + '\" 18 10 2>&1') | ForEach-Object { $_; $w.WriteLine($_); $w.Flush() }; $w.Close(); exit $LASTEXITCODE"
IF NOT "%ERRORLEVEL%"=="0" echo %CASE%: block 4.0.%~1 stopped, exit code %ERRORLEVEL%
exit /b 0
