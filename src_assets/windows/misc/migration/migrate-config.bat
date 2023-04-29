@echo off

rem Get sunshine root directory
for %%I in ("%~dp0\..") do set "OLD_DIR=%%~fI"

rem Create the config directory if it didn't already exist
set "NEW_DIR=%OLD_DIR%\config"
if not exist "%NEW_DIR%\" mkdir "%NEW_DIR%"

rem Migrate all files that aren't already present in the config dir
if exist "%OLD_DIR%\apps.json" (
    if not exist "%NEW_DIR%\apps.json" (
        move "%OLD_DIR%\apps.json" "%NEW_DIR%\apps.json"
    )
)
if exist "%OLD_DIR%\sunshine.conf" (
    if not exist "%NEW_DIR%\sunshine.conf" (
        move "%OLD_DIR%\sunshine.conf" "%NEW_DIR%\sunshine.conf"
    )
)
if exist "%OLD_DIR%\sunshine_state.json" (
    if not exist "%NEW_DIR%\sunshine_state.json" (
        move "%OLD_DIR%\sunshine_state.json" "%NEW_DIR%\sunshine_state.json"
    )
)

rem Migrate the credentials directory
if exist "%OLD_DIR%\credentials\" (
    if not exist "%NEW_DIR%\credentials\" (
        move "%OLD_DIR%\credentials" "%NEW_DIR%\"
    )
)

rem Create the credentials directory if it wasn't migrated or already existing
if not exist "%NEW_DIR%\credentials\" mkdir "%NEW_DIR%\credentials"

rem Disallow read access to the credentials directory for normal users
icacls "%NEW_DIR%\credentials" /inheritance:r
icacls "%NEW_DIR%\credentials" /grant:r Administrators:(OI)(CI)(F)

rem Migrate the covers directory
if exist "%OLD_DIR%\covers\" (
    if not exist "%NEW_DIR%\covers\" (
        move "%OLD_DIR%\covers" "%NEW_DIR%\"

        rem Fix apps.json image path values that point at the old covers directory
        powershell -c "(Get-Content '%NEW_DIR%\apps.json').replace('.\/covers\/', '.\/config\/covers\/') | Set-Content '%NEW_DIR%\apps.json'"
    )
)

rem Remove log files
del "%OLD_DIR%\*.txt"
del "%OLD_DIR%\*.log"
