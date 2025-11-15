#!/bin/sh

# The default HOME is not persistent, so override
# it to a path on the onboard flash. Otherwise our
# pairing data will be lost each reboot.
HOME=/usr/local/moonlight

# Enable Steam Controller detection in SDL
# https://github.com/moonlight-stream/moonlight-qt/issues/697
export SDL_ENABLE_STEAM_CONTROLLERS=1

# Renice PE_Single_CPU which seems to host A/V stuff
renice -10 -p $(pidof PE_Single_CPU)

# Renice Moonlight itself to avoid preemption by background tasks
# Write output to a logfile in /tmp
exec nice -n -10 ./bin/moonlight > /tmp/moonlight.log 2>&1
