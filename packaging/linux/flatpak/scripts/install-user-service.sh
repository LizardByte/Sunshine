#!/bin/sh

mkdir -p ~/.config/systemd
mkdir -p ~/.config/systemd/user
cp /app/share/sunshine/systemd/user/sunshine.service $HOME/.config/systemd/user/sunshine.service
echo Sunshine User Service has been installed.
echo Use [systemctl --user start sunshine] to start the Sunshine service.
echo Use [systemctl --user enable sunshine] to autostart the service on login.
