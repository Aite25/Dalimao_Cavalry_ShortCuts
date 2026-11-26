@echo off
rem Build Cavalry_ShortCut.exe with MSVC 2019 BuildTools.
rem Keep this file ASCII + CRLF: cmd.exe mis-parses UTF-8 comments and LF-only lines.
setlocal
set VCVARS=C:\Program Files (x86)\Microsoft Visual Studio\2019\BuildTools\VC\Auxiliary\Build\vcvarsall.bat
call "%VCVARS%" x64 >nul 2>&1
cl /nologo /EHsc /utf-8 Cavalry_ShortCut.cpp /Fe:Cavalry_ShortCut.exe /link /SUBSYSTEM:WINDOWS /ENTRY:wWinMainCRTStartup winhttp.lib user32.lib gdi32.lib shell32.lib imm32.lib comctl32.lib gdiplus.lib
if errorlevel 1 exit /b 1

rem Optional Authenticode signing (Aite25 Studio). Missing cert is not fatal.
set SIGNTOOL=C:\Program Files (x86)\Windows Kits\10\bin\10.0.19041.0\x64\signtool.exe
set THUMB=7C31DF35EDB183065D49C4C2023F5A0F6A816456
if exist "%SIGNTOOL%" "%SIGNTOOL%" sign /sha1 %THUMB% /fd SHA256 /td SHA256 /tr http://timestamp.digicert.com Cavalry_ShortCut.exe
if errorlevel 1 echo [warn] signing failed - exe may still run but SmartScreen will warn
exit /b 0
