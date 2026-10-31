@echo off
REM ============================================================================
REM  runs\K22\K22AutV4split -- the long K22 V4 shape (fx2,0,0 F5) in slices.
REM  The other V4 shape (fx2,2,2 F4) is done: 0 classes, 101 s (runs\K22\K22AutV4).
REM
REM  Start it always the same way: run.bat (no parameters). The slice is set below.
REM  If the slice's log exists, the run CONTINUES it: it restarts at the last
REM  "~ so far" row's "Block restart #" (no such row yet: from the slice start
REM  again -- nothing is skipped) and appends to the same log and result.
REM  A finished slice ("End of job" in its log) is not run again.
REM
REM  REP_SPLIT cuts every root block at level L and numbers the level-L branches;
REM  8 threads take the next branch in order. Same search, same node counts as
REM  an unsplit run. Level 3 = 46,656 branches, ~3 min harvest per run.
REM    L = 3, A = 0, B = end   all level-3 branches
REM    L = 3, A = 100, B = 200 branches 100 .. 199
REM  To cut one long level-3 branch finer, set L = 4 and its level-4 range
REM  (the map: "level 3 branch p = level 4 branches x-y", printed by a level-4 run).
REM
REM  Slice files: K22AutV4_L<L>_<A>_<B>.log and result_L<L>_<A>_<B>.txt. The exe
REM  writes each run's result to result_part.txt (it refuses an existing file);
REM  the bat appends it to the slice result. done.txt lists finished slices.
REM  (Parameters are for test runs only: [nopause] [L A [B]].)
REM ============================================================================
SET "SLICE_L=3"
SET "SLICE_A=0"
SET "SLICE_B=end"

SET "HERE=%~dp0"
cd /d "%HERE%"
setlocal
IF NOT DEFINED P1F_ROOT FOR %%I IN ("%HERE%..\..\..") DO SET "P1F_ROOT=%%~fI"
IF NOT DEFINED P1F_EXE  SET "P1F_EXE=%P1F_ROOT%\x64\Release\p1f.exe"
CALL "%P1F_ROOT%\env_reset.bat"

SET "NOPAUSE="
IF /I "%~1"=="nopause" (SET "NOPAUSE=1" & SHIFT)
SET "L=%SLICE_L%"
SET "A0=%SLICE_A%"
SET "B=%SLICE_B%"
IF NOT "%~1"=="" SET "L=%~1"
IF NOT "%~2"=="" SET "A0=%~2"
IF NOT "%~3"=="" SET "B=%~3"
SET "CASE=K22AutV4_L%L%_%A0%_%B%"
SET "LOG=%HERE%%CASE%.log"
SET "AS=%A0%"
IF NOT EXIST "%LOG%" GOTO :RUN
findstr /C:"End of job" "%LOG%" >nul
IF NOT ERRORLEVEL 1 (echo %CASE%: already finished -- change SLICE_L/A/B to run another slice & GOTO :END)
SET "K="
FOR /F "usebackq delims=" %%k IN (`powershell -NoProfile -Command "$l=(Select-String -Path $env:LOG -Pattern '^~ so far' | Select-Object -Last 1).Line; if ($l) { (($l -split '\|')[5].Trim() -split ' ')[0] }"`) DO SET "K=%%k"
IF "%K%"=="" (echo %CASE%: no restart point in the log yet -- rerunning from %A0%) ELSE (SET "AS=%K%")
echo %CASE%: continuing, branches %AS% .. %B%

:RUN
SET "SLICERES=%HERE%result_%CASE:K22AutV4_=%.txt"
SET "RESULT=%HERE%result_part.txt"
IF EXIST "%RESULT%" (type "%RESULT%" >> "%SLICERES%" & del "%RESULT%")
SET "BB=%B%"
IF "%BB%"=="end" SET "BB=999999999"
SET "REP_ORDERS=V4"
SET "REP_ONLYTYPES=2"
SET "REP_SPLIT=%L%:%AS%:%BB%"
SET "ExtPrint=1"
SET "EXE=%P1F_EXE%"
SET "RUNARGS=22 8"
echo %CASE%: EXE=%EXE%  started %DATE% %TIME%, branches %AS% .. %B%
echo %CASE%: started %DATE% %TIME%, branches %AS% .. %B% >> "%LOG%"
powershell -NoProfile -Command "[Console]::OutputEncoding=[System.Text.Encoding]::UTF8; $enc=New-Object System.Text.UTF8Encoding $false; $w=New-Object System.IO.StreamWriter($env:LOG,$true,$enc); & cmd /c ('\"' + $env:EXE + '\" ' + $env:RUNARGS + ' 2>&1') | ForEach-Object { $_; $w.WriteLine($_); $w.Flush() }; $w.Close(); exit $LASTEXITCODE"
SET "RC=%ERRORLEVEL%"
IF EXIST "%RESULT%" (type "%RESULT%" >> "%SLICERES%" & del "%RESULT%")
echo %CASE%: ended %DATE% %TIME%, exit code %RC%
echo %CASE%: ended %DATE% %TIME%, exit code %RC% >> "%LOG%"
findstr /C:"End of job" "%LOG%" >nul
IF "%RC%"=="0" IF NOT ERRORLEVEL 1 echo level %L% branches %A0% .. %B% done %DATE% %TIME%  (%CASE%.log)>> done.txt


:END
echo.
IF NOT DEFINED NOPAUSE pause
exit /b %RC%
