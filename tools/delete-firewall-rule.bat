@echo off

set RULE_NAME=SunshineStream

rem Delete the rule
netsh advfirewall firewall delete rule name=%RULE_NAME%
