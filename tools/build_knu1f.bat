@echo off
REM Build the uniform 1-factorization search -> knu1f.exe (formerly k18u1f; any even N 4..32)
REM   knu1f.exe N type [rows] [threads]    e.g. knu1f.exe 18 4.14   (see knu1f_README.md)
cd /d "%~dp0"
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
cl /nologo /O2 /arch:AVX2 /EHsc /std:c++17 /Fe:knu1f.exe knu1f.cpp
