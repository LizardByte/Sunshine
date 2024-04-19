How to not stream Discord call audio
====================================

#. Set your normal `Sound Output` volume to 100%

   .. image:: ../../../images/discord_calls_01.png

#. Start Sunshine

#. Set `Sound Output` to `sink-sunshine-stereo` (if it isn't automatic)

   .. image:: ../../../images/discord_calls_02.png

#. In Discord - `Right Click` - `Deafen` - Select your normal `Output Device`

   This is also where you will need to adjust output volume for Discord calls

   .. image:: ../../../images/discord_calls_03.png

#. Open `qpwgraph`

   .. image:: ../../../images/discord_calls_04.png

#. Connect `sunshine [sunshine-record]` to your normal `Output Device`

   * Drag `monitor_FL` to `playback_FL`

   * Drag `monitor_FR` to `playback_FR`

   .. image:: ../../../images/discord_calls_05.png
