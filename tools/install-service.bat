@echo off

set SERVICE_NAME=sunshinesvc
set SERVICE_BIN="%~dp0\tools\sunshinesvc.exe"
set SERVICE_START_TYPE=auto

rem Check if sunshinesvc already exists
sc qc %SERVICE_NAME% > nul 2>&1
if %ERRORLEVEL%==0 (
    rem Stop the existing service if running
    net stop %SERVICE_NAME%

    rem Reconfigure the existing service
    set SC_CMD=config
) else (
    rem Create a new service
    set SC_CMD=create
)

rem Run the sc command to create/reconfigure the service
sc %SC_CMD% %SERVICE_NAME% binPath= %SERVICE_BIN% start= %SERVICE_START_TYPE%

rem Start the new service
net start %SERVICE_NAME%
