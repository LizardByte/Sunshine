@echo off

rem Get temp directory
set temp_dir=%temp%/Sunshine

rem Create temp directory if it doesn't exist
if not exist "%temp_dir%" mkdir "%temp_dir%"

rem get browser_download_url from asset 0 of https://api.github.com/repos/vigem/vigembus/releases/latest
set latest_release_url=https://api.github.com/repos/vigem/vigembus/releases/latest

rem Use curl to get the api response, and find the browser_download_url
for /F "tokens=* USEBACKQ" %%F in (`curl -s -L %latest_release_url% ^| findstr browser_download_url`) do (
  set browser_download_url=%%F
)

rem Strip quotes
set browser_download_url=%browser_download_url:"=%

rem Remove the browser_download_url key
set browser_download_url=%browser_download_url:browser_download_url: =%

echo %browser_download_url%

rem Download the exe
curl -s -L -o "%temp_dir%\vigembus.exe" %browser_download_url%

rem Install vigembus
%temp_dir%\vigembus.exe /passive /promptrestart

rem Delete temp directory
rmdir /S /Q "%temp_dir%"
