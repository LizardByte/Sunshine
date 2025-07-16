@echo off

rem Get sunshine root directory
for %%I in ("%~dp0\..") do set "ROOT_DIR=%%~fI"

rem uninstall
set "DIST_DIR=%ROOT_DIR%\tools\vdd"
set "NEFCON=%DIST_DIR%\nefconw.exe"
if exist %DIST_DIR% (
    %NEFCON% --remove-device-node --hardware-id ROOT\MttVDD --class-guid 4d36e968-e325-11ce-bfc1-08002be10318
)
reg delete "HKLM\SOFTWARE\ZakoTech" /f
rmdir /S /Q "%DIST_DIR%"
