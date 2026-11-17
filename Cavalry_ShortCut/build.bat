@echo off
set VCVARS=C:\Program Files (x86)\Microsoft Visual Studio\2019\BuildTools\VC\Auxiliary\Build\vcvarsall.bat
call "%VCVARS%" x64 >nul 2>&1
cl /nologo /EHsc /utf-8 Cavalry_ShortCut.cpp /Fe:Cavalry_ShortCut.exe /link /SUBSYSTEM:WINDOWS /ENTRY:wWinMainCRTStartup winhttp.lib user32.lib gdi32.lib shell32.lib imm32.lib comctl32.lib