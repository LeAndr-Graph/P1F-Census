@echo off
REM  findperl.bat -- locate a perl for the check scripts and leave it in PERL.
REM
REM  Called, not executed on its own, and it deliberately does no SETLOCAL: the whole point
REM  is to hand PERL back to the bat that called it.
REM
REM      CALL "%P1F_ROOT%\tools\findperl.bat"
REM      IF ERRORLEVEL 1  ...say perl is missing and skip the check...
REM      "%PERL%" "%P1F_ROOT%\tools\compare_to_catalog.pl" "%RESULT%"
REM
REM  PATH comes first, so a perl the user chose always wins.  Only if PATH has none does this
REM  look in the standard install folders, and it reaches them through environment variables,
REM  never a literal drive path -- a literal one would work on the machine it was written on
REM  and nowhere else.  Git for Windows earns the extra look because its perl is on the PATH
REM  of a Git Bash shell but not of the cmd one a bat runs in, so a machine that does have
REM  perl can still look to a bat as if it has none.
REM
REM  Exit code 0 = found, PERL set.  1 = not found, PERL left empty.

SET "PERL="
where perl >nul 2>&1 && SET "PERL=perl"
IF DEFINED PERL EXIT /B 0

FOR %%P IN ("%ProgramFiles%\Git\usr\bin\perl.exe" "%LOCALAPPDATA%\Programs\Git\usr\bin\perl.exe" "%SystemDrive%\Strawberry\perl\bin\perl.exe") DO IF EXIST "%%~P" SET "PERL=%%~P"
IF DEFINED PERL EXIT /B 0

EXIT /B 1
