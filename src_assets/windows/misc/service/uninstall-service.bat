@echo off

rem Stop and delete the legacy SunshineSvc service
net stop sunshinesvc
sc delete sunshinesvc

rem Stop and delete the new SunshineService service
net stop SunshineService
sc delete SunshineService
