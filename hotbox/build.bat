@echo off
REM 编译 hotbox（需要 VS2019 BuildTools 的 MSVC；本机已装）
set VCVARS="C:\Program Files (x86)\Microsoft Visual Studio\2019\BuildTools\VC\Auxiliary\Build\vcvarsall.bat"
call %VCVARS% x64 >nul 2>&1
if errorlevel 1 (
  echo [x] 找不到 vcvarsall.bat，请改这个脚本里的路径
  exit /b 1
)
cl /nologo /EHsc /utf-8 hotbox.cpp /link /SUBSYSTEM:WINDOWS /ENTRY:wWinMainCRTStartup winhttp.lib user32.lib gdi32.lib
if errorlevel 1 (
  echo [x] 编译失败
  exit /b 1
)
echo.
echo [ok] hotbox.exe 已生成
