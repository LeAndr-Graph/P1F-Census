@echo off
REM ============================================================================
REM  regression\run_all.bat -- run every regression case and tally the
REM  verdicts. Each case bat runs the engine, then compare.pl decides: byte
REM  compare first, sorted compare only if that differs (two correct runs of a
REM  multi-order case disagree on record ORDER, never on content), then a
REM  content report if it still differs.
REM  Exit code 0 = all passed, 1 = at least one case failed.
REM  The whole suite is seconds-to-minutes; the long census runs live in runs\.
REM ============================================================================
SET "HERE=%~dp0"
cd /d "%HERE%"
setlocal enabledelayedexpansion
SET /A NPASS=0
SET /A NFAIL=0
SET "FAILED="
REM Each case bat cd's into its own folder, and cmd enumerates "for /d" lazily
REM against the CURRENT directory -- so cd back to %HERE% after every case, and
REM capture the verdict BEFORE that cd (a successful cd resets ERRORLEVEL).
for /d %%D in (*) do (
    if exist "%%D\run.bat" (
        echo.
        echo ======== %%D ========
        call "%HERE%%%D\run.bat"
        set "RC=!ERRORLEVEL!"
        cd /d "%HERE%"
        if !RC! GTR 0 (
            set /A NFAIL+=1
            set "FAILED=!FAILED! %%D"
        ) else (
            set /A NPASS+=1
        )
    )
)
echo.
echo ======== SUMMARY: !NPASS! passed, !NFAIL! failed ========
if not "!FAILED!" == "" echo failed:!FAILED!
echo.
pause
if !NFAIL! GTR 0 exit /b 1
exit /b 0
