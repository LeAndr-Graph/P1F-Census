@echo off
REM ============================================================================
REM  runs\K18\K18U1F -- no uniform 1-factorization of K18 of any non-Hamiltonian
REM  type. Standalone search tools\k18u1f.cpp: no canonizer, no input file.
REM  About 70 min at 8 threads, BELOW NORMAL priority. Writes K18U1F.log.
REM ============================================================================
SET "HERE=%~dp0"
cd /d "%HERE%"
setlocal
IF NOT DEFINED P1F_ROOT FOR %%I IN ("%HERE%..\..\..") DO SET "P1F_ROOT=%%~fI"
SET "EXE=%P1F_ROOT%\tools\k18u1f.exe"
IF NOT EXIST "%EXE%" CALL "%P1F_ROOT%\tools\build_k18u1f.bat"
cd /d "%HERE%"
SET "LOG=%HERE%K18U1F.log"
IF EXIST "%LOG%" echo K18U1F: %LOG% already exists -- delete it to run again.
IF EXIST "%LOG%" pause
IF EXIST "%LOG%" exit /b 1

FOR %%T IN (4.4.4.6 4.4.10 4.6.8 6.6.6 8.10 6.12 4.14) DO (
  echo %%T started %TIME%
  start "K18U1F %%T" /belownormal /wait /b "%EXE%" 18 %%T >> "%LOG%" 2>&1
)
findstr RESULT "%LOG%"
pause
