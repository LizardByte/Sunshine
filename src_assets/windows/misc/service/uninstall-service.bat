@echo off
setlocal EnableDelayedExpansion

set "SERVICE_NAME=SunshineService"
set "LEGACY_NAME=sunshinesvc"
set "MARKER=%TEMP%\sunshine_start_mode.txt"

rem -----------------  default in case the query fails
set "SERVICE_START_TYPE=auto"

rem -----------------  grab existing start-type if the service is present
sc qc "%SERVICE_NAME%" >nul 2>&1
if %ERRORLEVEL%==0 (
    for /f "tokens=3,*" %%S in ('sc qc "%SERVICE_NAME%" ^| find /i "START_TYPE"') do (
        set "NUM=%%S"
        set "EXTRA=%%T"
    )
    if "!NUM!"=="2" (
        echo !EXTRA! | find /i "DELAYED" >nul && (
            set "SERVICE_START_TYPE=delayed-auto"
        ) || (
            set "SERVICE_START_TYPE=auto"
        )
    ) else if "!NUM!"=="3" (
        set "SERVICE_START_TYPE=demand"
    ) else if "!NUM!"=="4" (
        set "SERVICE_START_TYPE=disabled"
    )
)

echo !SERVICE_START_TYPE! > "%MARKER%"

rem -----------------  stop & delete both service names
net stop "%LEGACY_NAME%"     >nul 2>&1
sc  delete "%LEGACY_NAME%"   >nul 2>&1

net stop "%SERVICE_NAME%"    >nul 2>&1
sc  delete "%SERVICE_NAME%"  >nul 2>&1

endlocal
