#!/bin/sh

cp /app/share/sunshine/udev/rules.d/85-sunshine.rules $HOME/.config/sunshine
flatpak-spawn --host pkexec sh -c "sudo usermod -a -G input $USER && cp $HOME/.config/sunshine/85-sunshine.rules /etc/udev/rules.d"
rm $HOME/.config/sunshine/85-sunshine.rules
echo Restart computer for mouse permission to take effect.
