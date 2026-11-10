@echo off
REM Build the uniform 1-factorization search -> k18u1f.exe
REM   k18u1f.exe N type [rows] [threads]    e.g. k18u1f.exe 18 4.14
cd /d "%~dp0"
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
cl /nologo /O2 /arch:AVX2 /EHsc /std:c++17 /Fe:k18u1f.exe k18u1f.cpp
