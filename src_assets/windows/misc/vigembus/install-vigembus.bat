@echo off
setlocal enabledelayedexpansion

rem Get temp directory
set temp_dir=%temp%/Sunshine

rem Create temp directory if it doesn't exist
if not exist "%temp_dir%" mkdir "%temp_dir%"

rem Get system proxy setting
set proxy= 
for /f "tokens=3" %%a in ('reg query "HKCU\Software\Microsoft\Windows\CurrentVersion\Internet Settings" ^| find /i "ProxyEnable"') do (
  set ProxyEnable=%%a
    
  if !ProxyEnable! equ 0x1 (
  for /f "tokens=3" %%a in ('reg query "HKCU\Software\Microsoft\Windows\CurrentVersion\Internet Settings" ^| find /i "ProxyServer"') do (
      set proxy=%%a
      echo Using system proxy !proxy! to download ViGEmBus
      set proxy=-x !proxy!
    )
  ) else (
    rem Proxy is not enabled.
  )
)

rem get browser_download_url from asset 0 of https://api.github.com/repos/vigem/vigembus/releases/latest
set latest_release_url=https://api.github.com/repos/vigem/vigembus/releases/latest

rem Use curl to get the api response, and find the browser_download_url
for /F "tokens=* USEBACKQ" %%F in (`curl -s !proxy! -L %latest_release_url% ^| findstr browser_download_url`) do (
  set browser_download_url=%%F
)

rem Strip quotes
set browser_download_url=%browser_download_url:"=%

rem Remove the browser_download_url key
set browser_download_url=%browser_download_url:browser_download_url: =%

echo %browser_download_url%

rem Download the exe
curl -s -L !proxy! -o "%temp_dir%\vigembus.exe" %browser_download_url%

rem Install vigembus
%temp_dir%\vigembus.exe /passive /promptrestart

rem Delete temp directory
rmdir /S /Q "%temp_dir%"
