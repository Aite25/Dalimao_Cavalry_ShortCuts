@echo off
REM ?? Cavalry MCP ???? 127.0.0.1:6769?
cd /d "%~dp0"
start "cavalry-bridge" /min node cavalry_bridge.mjs
