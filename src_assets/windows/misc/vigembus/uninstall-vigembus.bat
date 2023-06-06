@echo off

rem Use wmic to get the uninstall ViGEmBus
wmic product where name="ViGEm Bus Driver" call uninstall
