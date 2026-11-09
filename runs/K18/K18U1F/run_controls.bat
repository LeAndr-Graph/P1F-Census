@echo off
REM ============================================================================
REM  runs\K18\K18U1F\run_controls.bat -- known answers for the search k18u1f:
REM  one class each for K8 4.4, K10 4.6, K12 6.6, K16 4.4.4.4, none for the rest.
REM  Under a minute at 8 threads. Writes K18U1F_controls.log.
REM ============================================================================
SET "HERE=%~dp0"
cd /d "%HERE%"
setlocal
IF NOT DEFINED P1F_ROOT FOR %%I IN ("%HERE%..\..\..") DO SET "P1F_ROOT=%%~fI"
SET "EXE=%P1F_ROOT%\tools\k18u1f.exe"
IF NOT EXIST "%EXE%" CALL "%P1F_ROOT%\tools\build_k18u1f.bat"
cd /d "%HERE%"
SET "LOG=%HERE%K18U1F_controls.log"
IF EXIST "%LOG%" DEL "%LOG%"
SET "BAD=0"

FOR %%P IN ("8 4.4 1" "10 4.6 1" "12 6.6 1" "16 4.4.4.4 1" "14 4.10 0" "14 6.8 0" "14 4.4.6 0" "16 4.12 0" "16 6.10 0" "16 8.8 0" "16 4.4.8 0" "16 4.6.6 0") DO (
  FOR /F "tokens=1,2,3" %%A IN (%%P) DO (
    start "K18U1F control" /belownormal /wait /b "%EXE%" %%A %%B > "%HERE%control.tmp" 2>&1
    TYPE "%HERE%control.tmp" >> "%LOG%"
    findstr /C:"rows: %%C classes" "%HERE%control.tmp" >nul && echo K%%A %%B: %%C classes, as expected
    findstr /C:"rows: %%C classes" "%HERE%control.tmp" >nul || (echo K%%A %%B: DIFFERS, expected %%C classes & SET "BAD=1")
  )
)
DEL "%HERE%control.tmp"
IF "%BAD%"=="0" echo All controls passed.
IF NOT "%BAD%"=="0" echo A control FAILED -- see the log.
pause
