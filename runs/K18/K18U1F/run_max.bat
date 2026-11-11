@echo off
REM ============================================================================
REM  runs\K18\K18U1F\run_max.bat -- largest partial factorization per cycle type,
REM  every class at that size, each checked against its file in AllResults.
REM  Types 4.4.4.6 4.6.8 4.4.10 6.6.6 8.10 6.12: about 30 min at 8 threads.
REM  Type 4.14 (about 16 h) is not included: see run_max_shards.bat.
REM ============================================================================
SET "HERE=%~dp0"
cd /d "%HERE%"
setlocal
IF NOT DEFINED P1F_ROOT FOR %%I IN ("%HERE%..\..\..") DO SET "P1F_ROOT=%%~fI"
SET "EXE=%P1F_ROOT%\tools\k18u1f.exe"
IF NOT EXIST "%EXE%" CALL "%P1F_ROOT%\tools\build_k18u1f.bat"
cd /d "%HERE%"
SET "LOG=%HERE%K18U1F_max.log"
IF EXIST "%LOG%" DEL "%LOG%"

FOR %%T IN (4.4.4.6 4.6.8 4.4.10 6.6.6 8.10 6.12) DO (
  echo %%T started %TIME%
  start "K18U1F max %%T" /belownormal /wait /b "%EXE%" 18 %%T max >> "%LOG%" 2>&1
  fc /b "K18_U1F_max_%%T.txt" "%P1F_ROOT%\AllResults\K18_U1F_max_%%T.txt" >nul && echo %%T: matches AllResults
  fc /b "K18_U1F_max_%%T.txt" "%P1F_ROOT%\AllResults\K18_U1F_max_%%T.txt" >nul || echo %%T: DIFFERS from AllResults
)
findstr RESULT "%LOG%"
IF NOT DEFINED NOPAUSE pause
