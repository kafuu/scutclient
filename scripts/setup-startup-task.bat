@echo off
setlocal EnableExtensions DisableDelayedExpansion

set "TASK_NAME=scutclient"
set "ACTION=%~1"
set "IFACE="
set "SCUT_USER="
set "SCUT_PASS="
set "EXE_PATH="
set "SCUT_DEBUG="

set "ROOT_DIR=%~dp0.."
pushd "%ROOT_DIR%" >nul 2>&1
if errorlevel 1 (
  echo Failed to locate project root.
  exit /b 1
)
set "ROOT_DIR=%CD%"
popd >nul 2>&1

if exist "%ROOT_DIR%\build\Release\scutclient.exe" (
  set "EXE_PATH=%ROOT_DIR%\build\Release\scutclient.exe"
) else if exist "%ROOT_DIR%\build-vs2022-x64\Release\scutclient.exe" (
  set "EXE_PATH=%ROOT_DIR%\build-vs2022-x64\Release\scutclient.exe"
) else if exist "%ROOT_DIR%\build-nmake-x64\scutclient.exe" (
  set "EXE_PATH=%ROOT_DIR%\build-nmake-x64\scutclient.exe"
)

if "%ACTION%"=="" goto :usage
shift

:parse_args
if "%~1"=="" goto :dispatch
if /I "%~1"=="--iface" (
  set "IFACE=%~2"
  shift
  shift
  goto :parse_args
)
if /I "%~1"=="--username" (
  set "SCUT_USER=%~2"
  shift
  shift
  goto :parse_args
)
if /I "%~1"=="--password" (
  set "SCUT_PASS=%~2"
  shift
  shift
  goto :parse_args
)
if /I "%~1"=="--exe" (
  set "EXE_PATH=%~2"
  shift
  shift
  goto :parse_args
)
if /I "%~1"=="--task-name" (
  set "TASK_NAME=%~2"
  shift
  shift
  goto :parse_args
)
if /I "%~1"=="--debug" (
  set "SCUT_DEBUG=1"
  shift
  goto :parse_args
)
echo Unknown option: %~1
echo.
goto :usage

:dispatch
if /I "%ACTION%"=="list" goto :list
if /I "%ACTION%"=="install" goto :install
if /I "%ACTION%"=="run" goto :run
if /I "%ACTION%"=="status" goto :status
if /I "%ACTION%"=="uninstall" goto :uninstall
if /I "%ACTION%"=="remove" goto :uninstall
if /I "%ACTION%"=="help" goto :usage
if /I "%ACTION%"=="--help" goto :usage
echo Unknown action: %ACTION%
echo.
goto :usage

:list
call :require_exe || exit /b 1
"%EXE_PATH%" --list-ifaces
exit /b %ERRORLEVEL%

:install
call :require_admin || exit /b 1
call :require_exe || exit /b 1
if "%IFACE%"=="" (
  echo Missing --iface. Run "%~nx0 list" first.
  exit /b 2
)
if "%SCUT_USER%"=="" (
  echo Missing --username.
  exit /b 2
)
if "%SCUT_PASS%"=="" (
  echo Missing --password.
  exit /b 2
)

set "SCUT_TASK=%TASK_NAME%"
set "SCUT_EXE=%EXE_PATH%"
set "SCUT_IFACE=%IFACE%"
set "SCUT_USERNAME=%SCUT_USER%"
set "SCUT_PASSWORD=%SCUT_PASS%"

powershell -NoProfile -ExecutionPolicy Bypass -Command "$ErrorActionPreference='Stop'; $qq=[string][char]34; $esc=[string][char]92 + $qq; function q([string]$s){$qq + ($s -replace $qq,$esc) + $qq}; $a=@('--iface',(q $env:SCUT_IFACE),'--username',(q $env:SCUT_USERNAME),'--password',(q $env:SCUT_PASSWORD)); if($env:SCUT_DEBUG){$a+='--debug'}; $act=New-ScheduledTaskAction -Execute $env:SCUT_EXE -Argument ($a -join ' '); $trg=New-ScheduledTaskTrigger -AtLogOn; $pri=New-ScheduledTaskPrincipal -UserId 'SYSTEM' -RunLevel Highest; $set=New-ScheduledTaskSettingsSet -StartWhenAvailable -MultipleInstances IgnoreNew; Register-ScheduledTask -TaskName $env:SCUT_TASK -Action $act -Trigger $trg -Principal $pri -Settings $set -Force | Out-Null"
if errorlevel 1 (
  echo Failed to install scheduled task.
  exit /b 1
)

echo Installed task "%TASK_NAME%".
echo It will run as SYSTEM at user logon without showing a console window.
echo To test now: %~nx0 run
exit /b 0

:run
call :require_admin || exit /b 1
schtasks /Run /TN "%TASK_NAME%"
exit /b %ERRORLEVEL%

:status
schtasks /Query /TN "%TASK_NAME%" /V /FO LIST
exit /b %ERRORLEVEL%

:uninstall
call :require_admin || exit /b 1
schtasks /Delete /TN "%TASK_NAME%" /F
exit /b %ERRORLEVEL%

:require_exe
if not "%EXE_PATH%"=="" if exist "%EXE_PATH%" exit /b 0
echo scutclient.exe was not found.
echo Build first, or pass --exe "D:\path\to\scutclient.exe".
exit /b 1

:require_admin
net session >nul 2>&1
if "%ERRORLEVEL%"=="0" exit /b 0
echo This action must be run from an Administrator command prompt.
exit /b 1

:usage
echo Usage:
echo   %~nx0 list [--exe path]
echo   %~nx0 install --iface "\Device\NPF_{GUID}" --username USER --password PASS [--exe path] [--task-name NAME] [--debug]
echo   %~nx0 run [--task-name NAME]
echo   %~nx0 status [--task-name NAME]
echo   %~nx0 uninstall [--task-name NAME]
echo.
echo Examples:
echo   %~nx0 list
echo   %~nx0 install --iface "\Device\NPF_{GUID}" --username 20230000 --password "your-password"
echo   %~nx0 status
echo   %~nx0 uninstall
exit /b 2
