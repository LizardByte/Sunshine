@echo off

set CERTIFICATE="%~dp0Virtual_Display_Driver.cer"

certutil -addstore -f root %CERTIFICATE%
certutil -addstore -f TrustedPublisher %CERTIFICATE%
pause
