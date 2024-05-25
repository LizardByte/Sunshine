Discord call audio cancellation with Voicemeeter (Standard)
===========================================================

Voicemeeter
^^^^^^^^^^^

#. Click Hardware Out
#. Set the physical device you recieve audio to as your Hardware Out with MME
#. Turn on BUS A for the Virtual Input

Windows
^^^^^^^

#. Open the sound settings
#. Set your default Playback as Voicemeeter Input

.. note:: Run audio in the background to find the device that your Virtual Input is using
   (Voicemeeter In #), you will see the bar to the right of the device have green bars
   going up and down. This device will be referred to as Voicemeeter Input.

Discord
^^^^^^^

#. Open the settings
#. Go to Voice & Video
#. Set your Output Device as the physical you receive audio to

.. note:: It is usually the same device you set for Hardware Out in Voicemeeter.

Sunshine
^^^^^^^^

#. Go to Configuration 
#. Go to the Audio/Video tab
#. Set Virtual Sink as Voicemeeter Input

.. note:: This should be the device you set as default previously in Playback.
