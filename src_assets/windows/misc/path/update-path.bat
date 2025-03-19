@echo off
setlocal EnableDelayedExpansion

rem Check if parameter is provided
if "%~1"=="" (
    echo Usage: %0 [add^|remove]
    echo   add    - Adds Sunshine directories to system PATH
    echo   remove - Removes Sunshine directories from system PATH
    exit /b 1
)

rem Get sunshine root directory
for %%I in ("%~dp0\..") do set "ROOT_DIR=%%~fI"
echo Sunshine root directory: !ROOT_DIR!

rem Define directories to add to path
set "PATHS_TO_MANAGE[0]=!ROOT_DIR!"
set "PATHS_TO_MANAGE[1]=!ROOT_DIR!\tools"

rem System path registry location
set "KEY_NAME=HKEY_LOCAL_MACHINE\SYSTEM\CurrentControlSet\Control\Session Manager\Environment"
set "VALUE_NAME=Path"

rem Get the current path
for /f "tokens=2*" %%A in ('reg query "%KEY_NAME%" /v "%VALUE_NAME%"') do set "CURRENT_PATH=%%B"
echo Current path: !CURRENT_PATH!

rem Check if adding to path
if /i "%~1"=="add" (
    set "NEW_PATH=!CURRENT_PATH!"

    rem Process each directory to add
    for /L %%i in (0,1,1) do (
        set "DIR_TO_ADD=!PATHS_TO_MANAGE[%%i]!"

        rem Check if path already contains this directory
        echo "!CURRENT_PATH!" | findstr /i /c:"!DIR_TO_ADD!" > nul
        if !ERRORLEVEL!==0 (
            echo !DIR_TO_ADD! already in path
        ) else (
            echo Adding to path: !DIR_TO_ADD!
            set "NEW_PATH=!NEW_PATH!;!DIR_TO_ADD!"
        )
    )

    rem Only update if path was changed
    if "!NEW_PATH!" neq "!CURRENT_PATH!" (
        rem Set the new path in the registry
        reg add "%KEY_NAME%" /v "%VALUE_NAME%" /t REG_EXPAND_SZ /d "!NEW_PATH!" /f
        if !ERRORLEVEL!==0 (
            echo Successfully added Sunshine directories to PATH
        ) else (
            echo Failed to add Sunshine directories to PATH
        )
    ) else (
        echo No changes needed to PATH
    )
    exit /b !ERRORLEVEL!
)

rem Check if removing from path
if /i "%~1"=="remove" (
    set "CHANGES_MADE=0"

    rem Process each directory to remove
    for /L %%i in (0,1,1) do (
        set "DIR_TO_REMOVE=!PATHS_TO_MANAGE[%%i]!"

        rem Check if path contains this directory
        echo "!CURRENT_PATH!" | findstr /i /c:"!DIR_TO_REMOVE!" > nul
        if !ERRORLEVEL!==0 (
            echo Removing from path: !DIR_TO_REMOVE!

            rem Build a new path by parsing and filtering the current path
            set "NEW_PATH="
            for %%p in ("!CURRENT_PATH:;=" "!") do (
                set "PART=%%~p"
                if /i "!PART!" NEQ "!DIR_TO_REMOVE!" (
                    if defined NEW_PATH (
                        set "NEW_PATH=!NEW_PATH!;!PART!"
                    ) else (
                        set "NEW_PATH=!PART!"
                    )
                )
            )

            set "CURRENT_PATH=!NEW_PATH!"
            set "CHANGES_MADE=1"
        ) else (
            echo !DIR_TO_REMOVE! not found in path
        )
    )

    rem Only update if path was changed
    if "!CHANGES_MADE!"=="1" (
        rem Set the new path in the registry
        reg add "%KEY_NAME%" /v "%VALUE_NAME%" /t REG_EXPAND_SZ /d "!CURRENT_PATH!" /f
        if !ERRORLEVEL!==0 (
            echo Successfully removed Sunshine directories from PATH
        ) else (
            echo Failed to remove Sunshine directories from PATH
        )
    ) else (
        echo No changes needed to PATH
    )
    exit /b !ERRORLEVEL!
)

echo Unknown parameter: %~1
echo Usage: %0 [add^|remove]
exit /b 1
