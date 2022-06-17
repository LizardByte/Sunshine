@echo off

set RULE_NAME=SunshineStream
set PROGRAM_BIN="%~dp0sunshine.exe"

rem Add the rule
netsh advfirewall firewall add rule name=%RULE_NAME% dir=in action=allow program=%PROGRAM_BIN% enable=yes