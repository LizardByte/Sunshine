@echo off
setlocal EnableDelayedExpansion

rem ------------  resolve Sunshine root folder
for %%I in ("%~dp0\..") do set "ROOT_DIR=%%~fI"

set "SERVICE_NAME=SunshineService"
set "SERVICE_BIN=%ROOT_DIR%\tools\sunshinesvc.exe"
set "MARKER=%TEMP%\sunshine_start_mode.txt"

rem ------------  default for a brand-new install
set "SERVICE_START_TYPE=auto"

rem ------------  if the uninstall script left us a start-mode, use it
if exist "%MARKER%" (
    set /p SERVICE_START_TYPE=<"%MARKER%"
    del "%MARKER%"
)

rem ------------  create the service with the preserved mode
sc create "%SERVICE_NAME%" ^
    binPath= "\"%SERVICE_BIN%\"" ^
    start= !SERVICE_START_TYPE! ^
    DisplayName= "Sunshine Service"

sc description "%SERVICE_NAME%" "Sunshine is a self-hosted game-stream host for Moonlight."

rem ------------  start it unless the mode is “disabled”
if /i not "!SERVICE_START_TYPE!"=="disabled" (
    net start "%SERVICE_NAME%" >nul 2>&1
)

endlocal
