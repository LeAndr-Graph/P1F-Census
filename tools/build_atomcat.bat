@echo off
REM Build the catalog atomicity scanner -> atomcat.exe
REM   atomcat.exe <catalog or result file>   (DUMP=1 prints each atomic square)
cd /d "%~dp0"
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
cl /nologo /O2 /EHsc /std:c++17 /Fe:atomcat.exe atomic_latin_catalog.cpp
