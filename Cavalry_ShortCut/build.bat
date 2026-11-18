@echo off
setlocal
set VCVARS=C:\Program Files (x86)\Microsoft Visual Studio\2019\BuildTools\VC\Auxiliary\Build\vcvarsall.bat
call "%VCVARS%" x64 >nul 2>&1
cl /nologo /EHsc /utf-8 Cavalry_ShortCut.cpp /Fe:Cavalry_ShortCut.exe /link /SUBSYSTEM:WINDOWS /ENTRY:wWinMainCRTStartup winhttp.lib user32.lib gdi32.lib shell32.lib imm32.lib comctl32.lib gdiplus.lib
if errorlevel 1 goto :eof

rem ---- 签名（Aite25 Studio 自签名代码签名证书）----
set SIGNTOOL="C:\Program Files (x86)\Windows Kits\10\bin\10.0.19041.0\x64\signtool.exe"
set THUMB=7C31DF35EDB183065D49C4C2023F5A0F6A816456
%SIGNTOOL% sign /sha1 %THUMB% /fd SHA256 /td SHA256 /tr http://timestamp.digicert.com Cavalry_ShortCut.exe
if errorlevel 1 echo [warn] signing failed - exe may still run but SmartScreen will warn
