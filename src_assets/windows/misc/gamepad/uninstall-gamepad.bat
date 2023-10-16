@echo off

rem Use wmic to get the uninstall Virtual Gamepad
wmic product where name="ViGEm Bus Driver" call uninstall
