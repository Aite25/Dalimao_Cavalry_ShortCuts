@echo off
REM 常驻 Cavalry MCP 桥。放在启动项里就只需要在 Cavalry 启动后点一次 Allow。
cd /d "E:\dsh work\dsh_cavalry"
start "" /min node cavalry_bridge.mjs
