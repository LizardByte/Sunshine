@echo off
setlocal enabledelayedexpansion

@REM rem Get temp directory
@REM set temp_dir=%temp%/Sunshine

@REM rem Create temp directory if it doesn't exist
@REM if not exist "%temp_dir%" mkdir "%temp_dir%"

@REM rem Get system proxy setting
@REM set proxy= 
@REM for /f "tokens=3" %%a in ('reg query "HKCU\Software\Microsoft\Windows\CurrentVersion\Internet Settings" ^| find /i "ProxyEnable"') do (
@REM   set ProxyEnable=%%a

@REM   if !ProxyEnable! equ 0x1 (
@REM   for /f "tokens=3" %%a in ('reg query "HKCU\Software\Microsoft\Windows\CurrentVersion\Internet Settings" ^| find /i "ProxyServer"') do (
@REM       set proxy=%%a
@REM       echo Using system proxy !proxy! to download Virtual Gamepad
@REM       set proxy=-x !proxy!
@REM     )
@REM   ) else (
@REM     rem Proxy is not enabled.
@REM   )
@REM )

@REM rem Strip quotes
@REM set browser_download_url="https://github.com/itsmikethetech/Virtual-Display-Driver/releases/download/23.12.2HDR/VDD.HDR.23.12.zip"

@REM rem Download the zip
@REM curl -s -L !proxy! -o "%temp_dir%\vdd.zip" %browser_download_url%

@REM rem unzip
@REM powershell -c "Expand-Archive '%temp_dir%\vdd.zip' '%temp_dir%'"

@REM rem Delete temp file
@REM del "%temp_dir%\vdd.zip"

rem install
set "DRIVER_DIR=%~dp0\driver"
echo %DRIVER_DIR%

set DIST_DIR="C:/IddSampleDriver"
set "NEFCON=%DIST_DIR%\nefconw.exe"
if exist %NEFCON% (
    goto skip
)
goto continue

:skip
echo "no install needed. Exiting."
exit /b 0

:continue
mkdir %DIST_DIR%
move "%DRIVER_DIR%\*.*" %DIST_DIR%

rem install inf
set CERTIFICATE="%DIST_DIR%/Virtual_Display_Driver.cer"
certutil -addstore -f root %CERTIFICATE%
certutil -addstore -f TrustedPublisher %CERTIFICATE%

@REM install inf
%NEFCON% --create-device-node --hardware-id ROOT\iddsampledriver --service-name IDD_HDR_FOR_SUNSHINE --class-name Display --class-guid 4d36e968-e325-11ce-bfc1-08002be10318
%NEFCON% --install-driver --inf-path "%DIST_DIR%\IddSampleDriver.inf"