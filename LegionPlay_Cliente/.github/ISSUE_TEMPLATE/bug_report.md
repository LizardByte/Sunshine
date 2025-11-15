---
name: Bug report
about: Follow the troubleshooting guide before reporting a bug

---
**READ ME FIRST!**
If you're here because something basic is not working (like gamepad input, video, or similar), it's probably something specific to your setup, so make sure you've gone through the Troubleshooting Guide first: https://github.com/moonlight-stream/moonlight-docs/wiki/Troubleshooting

If you still have trouble with basic functionality after following the guide, join our Discord server where there are many other volunteers who can help (or direct you back here if it looks like a Moonlight bug after all). https://moonlight-stream.org/discord

**Describe the bug**
A clear and concise description of what the bug is.

**Steps to reproduce**
Any special steps that are required for the bug to appear.

**Screenshots**
If applicable, add screenshots to help explain your problem. If the issue is related to video glitching or poor quality, please include screenshots.

**Affected games**
List the games you've tried that exhibit the issue. To see if the issue is game-specific, try streaming Steam Big Picture with Moonlight and see if the issue persists there.

**Other Moonlight clients**
- Does the issue occur when using Moonlight on iOS or Android?

**Moonlight settings (please complete the following information)**
- Have any settings been adjusted from defaults?
- If so, which settings have been changed?
- Does the problem still occur after reverting settings back to default?

**Gamepad-related issues (please complete if problem is gamepad-related)**
- Do you have any gamepads connected to your host PC directly?
- Does the problem still remain if you stream the desktop and use https://html5gamepad.com to test your gamepad?
  - Instructions for streaming the desktop can be found here: https://github.com/moonlight-stream/moonlight-docs/wiki/Setup-Guide

**Client PC details (please complete the following information)**
 - OS: [e.g. Windows 10 1809]
 - Moonlight Version: [e.g. v0.9.0]
 - GPU: [e.g. Intel HD Graphics 520]
 - Linux package type (if applicable): [e.g. Flatpak]

**Server PC details (please complete the following information)**
 - OS: [e.g. Windows 10 1809]
 - Sunshine or GeForce Experience version: [e.g. Sunshine v0.21.0]
 - GPU: [e.g. AMD Radeon RX 7900 XT]
 - GPU driver: [e.g. 24.1.1]

**Moonlight Logs (please attach)**
- On Windows, `Moonlight-###.log` files can be found in `%TEMP%`. Simply type that into the File Explorer path field to navigate there.
- On macOS, `Moonlight-###.log` files can be found in `/tmp`. In Finder, press Cmd+Shift+G, then type `/tmp` to navigate there.
- On Linux with the Flatpak, logs print to the terminal when running with the command: `flatpak run com.moonlight_stream.Moonlight`
- On Linux with the Snap, logs print to the terminal when running with the command: `moonlight`

**Additional context**
Anything else you think may be relevant to the issue
