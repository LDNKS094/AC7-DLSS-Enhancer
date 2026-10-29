@echo off
if exist "%~dp0AC7-Capture8.request" (
  echo An eight-frame request exists. No two-frame request sent.
  exit /b 1
)
if exist "%~dp0AC7-Capture.request" (
  echo Capture request already exists. Do not send another request.
  exit /b 1
)
echo Switch back to the flying game now. Capture request will be sent in 5 seconds.
powershell.exe -NoProfile -Command "Start-Sleep -Seconds 5"
if errorlevel 1 exit /b 1
if exist "%~dp0AC7-Capture.request" (
  echo Another request was created while waiting. No new request sent.
  exit /b 1
)
if exist "%~dp0AC7-Capture8.request" exit /b 1
> "%~dp0AC7-Capture.request" echo AC7 two-frame diagnostic request
if errorlevel 1 exit /b 1
echo Requested one two-frame capture. This sampler never rearms in the same process.
