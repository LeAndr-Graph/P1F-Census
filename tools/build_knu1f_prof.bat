@echo off
REM Profiling build of knu1f: same flags + debug symbols -> knu1f_prof.exe / knu1f_prof.pdb
cd /d "%~dp0"
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
cl /nologo /O2 /Zi /arch:AVX2 /EHsc /std:c++17 /Fe:knu1f_prof.exe /Fd:knu1f_prof_c.pdb knu1f.cpp /link /DEBUG /PDB:knu1f_prof.pdb
