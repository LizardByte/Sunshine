@echo off
setlocal enabledelayedexpansion

rem Check if a compatible version of ViGEmBus is already installed (1.17 or later)
set Version=
for /f "usebackq delims=" %%a in (`wmic product where "name='ViGEm Bus Driver' or name='Nefarius Virtual Gamepad Emulation Bus Driver'" get Version /format:Textvaluelist`) do (
    for /f "delims=" %%# in ("%%a") do set "%%#"
)

rem Extract Major and Minor versions
for /f "tokens=1,2 delims=." %%a in ("%Version%") do (
    set "MajorVersion=%%a"
    set "MinorVersion=%%b"
)

rem Compare the version to 1.17
if /i !MajorVersion! gtr 1 goto skip
if /i !MajorVersion! equ 1 (
    if /i !MinorVersion! geq 17 (
        goto skip
    )
)
goto continue

:skip
echo "The installed version is %Version%, no update needed. Exiting."
exit /b 0

:continue
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
      echo Using system proxy !proxy! to download Virtual Gamepad
      set proxy=-x !proxy!
    )
  ) else (
    rem Proxy is not enabled.
  )
)

rem get browser_download_url from asset 0 of https://api.github.com/repos/nefarius/vigembus/releases/latest
set latest_release_url=https://api.github.com/repos/nefarius/vigembus/releases/latest

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
curl -s -L !proxy! -o "%temp_dir%\virtual_gamepad.exe" %browser_download_url%

rem Install Virtual Gamepad
%temp_dir%\virtual_gamepad.exe /passive /promptrestart

rem Delete temp directory
rmdir /S /Q "%temp_dir%"
