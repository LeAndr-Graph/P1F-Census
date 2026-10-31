@echo off
REM Build the RESULT-file validator -> p1f_check.exe
REM   p1f_check.exe <result.txt> [--quiet]   : every record a P1F, |Aut| recomputed, duplicate classes found
cd /d "%~dp0"
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
cl /nologo /O2 /EHsc /std:c++17 /Fe:p1f_check.exe p1f_check.cpp
