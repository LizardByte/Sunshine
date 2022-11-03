#!/bin/sh

mkdir -p ~/.config/systemd
mkdir -p ~/.config/systemd/user
cp /app/share/sunshine/systemd/user/sunshine.service $HOME/.config/systemd/user/sunshine.service
sed -i 's/\/app\/bin\/sunshine/flatpak run dev.lizardbyte.sunshine\nExecStop=flatpak kill dev.lizardbyte.sunshine/g' $HOME/.config/systemd/user/sunshine.service
echo Sunshine User Service has been installed.
