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

rem Get sunshine root directory
for %%I in ("%~dp0\..") do set "ROOT_DIR=%%~fI"

set "DIST_DIR=%ROOT_DIR%\tools\vdd"
set "NEFCON=%DIST_DIR%\nefconw.exe"

rem 如果目录存在则删除
if exist "%DIST_DIR%" (
    rmdir /s /q "%DIST_DIR%"
)

mkdir "%DIST_DIR%"
copy "%DRIVER_DIR%\*.*" %DIST_DIR%

%NEFCON% --remove-device-node --hardware-id ROOT\MttVDD --class-guid 4d36e968-e325-11ce-bfc1-08002be10318
echo 正在等待vdd卸载...
timeout /t 5 /nobreak > nul

@REM write registry
reg add "HKLM\SOFTWARE\ZakoTech\ZakoDisplayAdapter" /v VDDPATH /t REG_SZ /d "%DIST_DIR%" /f

@REM rem install cet
set CERTIFICATE="%DIST_DIR%/MttVDD.cer"
certutil -addstore -f root %CERTIFICATE%
@REM certutil -addstore -f TrustedPublisher %CERTIFICATE%

@REM install inf
%NEFCON% --create-device-node --hardware-id Root\MttVDD --service-name IDD_HDR_FOR_SUNSHINE --class-name Display --class-guid 4D36E968-E325-11CE-BFC1-08002BE10318
%NEFCON% --install-driver --inf-path "%DIST_DIR%\MttVDD.inf"