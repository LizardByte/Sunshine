#!/bin/sh

mkdir -p ~/.config/systemd
mkdir -p ~/.config/systemd/user
cp /app/share/sunshine/systemd/user/sunshine.service $HOME/.config/systemd/user/sunshine.service
echo Sunshine User Service has been installed.
