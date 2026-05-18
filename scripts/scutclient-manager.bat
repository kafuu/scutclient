@echo off
setlocal

set "SCRIPT_DIR=%~dp0"
rem This file intentionally stays ASCII-only. Chinese prompts live in the PowerShell script.

if "%~1"=="" (
  powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%SCRIPT_DIR%scutclient-manager.ps1" install --pause-on-exit
) else (
  powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%SCRIPT_DIR%scutclient-manager.ps1" %*
)

exit /b %ERRORLEVEL%
