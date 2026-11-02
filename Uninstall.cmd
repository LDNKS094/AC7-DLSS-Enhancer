@echo off
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0scripts\Uninstall.ps1" %*
set "AC7_UNINSTALL_RESULT=%ERRORLEVEL%"
if not "%AC7_UNINSTALL_RESULT%"=="0" echo Uninstall failed with exit code %AC7_UNINSTALL_RESULT%. See the message above.
pause
exit /b %AC7_UNINSTALL_RESULT%
