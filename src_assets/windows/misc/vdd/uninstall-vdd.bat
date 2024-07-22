@echo off

rem uninstall
set "DIST_DIR=C:/IddSampleDriver"
set "NEFCON=%DIST_DIR%\nefconw.exe"
if exist %DIST_DIR% (
    %NEFCON% --remove-device-node --hardware-id ROOT\iddsampledriver --class-guid 4d36e968-e325-11ce-bfc1-08002be10318
)
rmdir /S /Q "%DIST_DIR%"
