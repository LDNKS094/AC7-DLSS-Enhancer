@echo off
if exist "%~dp0AC7-Capture.request" (
  echo A two-frame request exists. No eight-frame request sent.
  exit /b 1
)
if exist "%~dp0AC7-Capture8.request" (
  echo An eight-frame request already exists. Do not send another.
  exit /b 1
)
echo Switch back to normal flight now. Eight-frame request will be sent in 5 seconds.
powershell.exe -NoProfile -Command "Start-Sleep -Seconds 5"
if errorlevel 1 exit /b 1
if exist "%~dp0AC7-Capture.request" exit /b 1
if exist "%~dp0AC7-Capture8.request" exit /b 1
> "%~dp0AC7-Capture8.request" echo AC7 fixed eight-frame diagnostic request
if errorlevel 1 exit /b 1
echo Requested exactly eight frames. No rearm in this process; do not run again.
