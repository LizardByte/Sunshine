@echo off

set SERVICE_NAME=sunshinesvc

net stop %SERVICE_NAME%

sc delete %SERVICE_NAME%
