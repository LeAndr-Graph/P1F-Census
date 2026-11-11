@echo off
REM ============================================================================
REM  runs\K18\K18U1F\run_max_shards.bat TYPE K FIRST LAST STEP
REM  The K-row pass of the max search for one type, cut into shards of STEP root
REM  jobs, one process each, for runs too long to do in one go (type 4.14).
REM  Resumable: a shard whose output file exists is skipped, so stop at any time
REM  and run the same command again. A stopped shard leaves no file.
REM  When all shards are done, they are merged into K18_U1F_max_TYPE_kK.txt.
REM  Example: run_max_shards.bat 4.14 12 0 3443 20
REM  "jobs ... of N" in any shard's output gives the total N for LAST.
REM ============================================================================
SET "HERE=%~dp0"
cd /d "%HERE%"
setlocal enabledelayedexpansion
IF "%~5"=="" echo usage: run_max_shards.bat TYPE K FIRST LAST STEP & exit /b 2
IF NOT DEFINED P1F_ROOT FOR %%I IN ("%HERE%..\..\..") DO SET "P1F_ROOT=%%~fI"
SET "EXE=%P1F_ROOT%\tools\k18u1f.exe"
IF NOT EXIST "%EXE%" CALL "%P1F_ROOT%\tools\build_k18u1f.bat"
cd /d "%HERE%"
SET "T=%~1"
SET "K=%~2"
SET "LOG=%HERE%K18U1F_max_%T%_k%K%.log"
SET "FILES="

FOR /L %%F IN (%~3,%~5,%~4) DO (
  SET /A E=%%F+%~5
  IF !E! GTR %~4 SET "E=%~4"
  SET "OUT=K18_U1F_max_%T%_k%K%_%%F-!E!.txt"
  SET "FILES=!FILES! !OUT!"
  IF EXIST "!OUT!" (
    echo jobs %%F-!E!: done before
  ) ELSE (
    echo jobs %%F-!E! started !DATE! !TIME!
    SET "U1F_QUIET=1"
    start "K18U1F %T% k%K% %%F" /belownormal /wait /b "%EXE%" 18 %T% max 8 %K% %%F !E! >> "%LOG%" 2>&1
    IF NOT EXIST "!OUT!" echo jobs %%F-!E!: stopped, no result & exit /b 1
    findstr /C:"jobs %%F-!E!" "%LOG%"
  )
)
SET "U1F_QUIET="
"%EXE%" merge "K18_U1F_max_%T%_k%K%.txt" %FILES%
