#!/bin/sh

systemctl --user stop sunshine
rm $HOME/.config/systemd/user/sunshine.service
systemctl --user daemon-reload
echo Sunshine User Service has been uninstalled.
