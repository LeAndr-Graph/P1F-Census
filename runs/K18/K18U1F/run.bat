@echo off
REM ============================================================================
REM  runs\K18\K18U1F -- no uniform 1-factorization of K18 of any non-Hamiltonian
REM  type. Standalone search tools\k18u1f.cpp: no input file.
REM  About 45 min at 8 threads, BELOW NORMAL priority. Writes K18U1F.log.
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

REM  Check: no type has a result, and the deepest partial each search reached (a property
REM  of the pruning, not a mathematical result) is as recorded in ReadMe.md.
SET "BAD=0"
FOR %%T IN (4.4.4.6 4.4.10 4.6.8 6.6.6 8.10 6.12 4.14) DO (
  findstr /C:"type %%T, 17 rows: 0 classes" "%LOG%" >nul || (echo %%T: RESULT DIFFERS & SET "BAD=1")
)
FOR %%D IN ("4.4.4.6:3 rows, 3 classes" "4.4.10:5 rows, 206 classes" "4.6.8:5 rows, 48 classes" "6.6.6:4 rows, 3 classes" "8.10:12 rows, 7 classes" "6.12:7 rows, at least 1000 classes" "4.14:8 rows, at least 1000 classes") DO (
  FOR /F "tokens=1,* delims=:" %%A IN (%%D) DO (
    findstr /C:"Deepest reached: %%B held, in K18_U1F_deepest_%%A.txt" "%LOG%" >nul || (echo %%A: deepest DIFFERS, expected %%B & SET "BAD=1")
  )
)
IF "%BAD%"=="0" echo All checks passed.
IF NOT "%BAD%"=="0" echo A check FAILED -- see the log.
pause
