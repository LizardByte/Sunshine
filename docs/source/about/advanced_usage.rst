Advanced Usage
==============
Sunshine will work with the default settings for most users. In some cases you may want to configure Sunshine further.

Performance Tips
----------------

AMD
^^^
In Windows, enabling `Enahanced Sync` in AMD's settings may help reduce the latency by an additional frame. This
applies to `amfenc` and `libx264`.

Nvidia
^^^^^^
Enabling `Fast Sync` in Nvidia settings may help reduce latency.

Configuration
-------------
The default location for the configuration file is listed below. You can use another location if you
choose, by passing in the full configuration file path as the first argument when you start Sunshine.

The default location of the ``apps.json`` is the same as the configuration file. You can use a custom
location by modifying the configuration file.

**Default File Location**

.. table::
   :widths: auto

   =========   ===========
   Value       Description
   =========   ===========
   Docker      /config/
   Linux       ~/.config/sunshine/
   macOS       ~/.config/sunshine/
   Windows     %ProgramFiles%\\Sunshine\\config
   =========   ===========

**Example**
   .. code-block:: bash

      sunshine ~/sunshine_config.conf

To manually configure sunshine you may edit the `conf` file in a text editor. Use the examples as reference.

.. Hint:: Some settings are not available within the web ui.

General
-------

sunshine_name
^^^^^^^^^^^^^

**Description**
   The name displayed by Moonlight

**Default**
   PC hostname

**Example**
   .. code-block:: text

      sunshine_name = Sunshine

min_log_level
^^^^^^^^^^^^^

**Description**
   The minimum log level printed to standard out.

**Choices**

.. table::
   :widths: auto

   =======   ===========
   Value     Description
   =======   ===========
   verbose   verbose logging
   debug     debug logging
   info      info logging
   warning   warning logging
   error     error logging
   fatal     fatal logging
   none      no logging
   =======   ===========

**Default**
   ``info``

**Example**
   .. code-block:: text

      min_log_level = info

log_path
^^^^^^^^

**Description**
   The path where the sunshine log is stored.

**Default**
   ``sunshine.log``

**Example**
   .. code-block:: text

      log_path = sunshine.log

Controls
--------

gamepad
^^^^^^^

**Description**
   The type of gamepad to emulate on the host.

   .. Caution:: Applies to Windows only.

**Choices**

.. table::
   :widths: auto

   =====     ===========
   Value     Description
   =====     ===========
   x360      xbox 360 controller
   ds4       dualshock controller (PS4)
   =====     ===========

**Default**
   ``x360``

**Example**
   .. code-block:: text

      gamepad = x360

back_button_timeout
^^^^^^^^^^^^^^^^^^^

**Description**
   If, after the timeout, the back/select button is still pressed down, Home/Guide button press is emulated.

   On Nvidia Shield, the home and power button are not passed to Moonlight.

   .. Tip:: If back_button_timeout < 0, then the Home/Guide button will not be emulated.

**Default**
   ``2000``

**Example**
   .. code-block:: text

      back_button_timeout = 2000

key_repeat_delay
^^^^^^^^^^^^^^^^

**Description**
   The initial delay in milliseconds before repeating keys. Controls how fast keys will repeat themselves.

**Default**
   ``500``

**Example**
   .. code-block:: text

      key_repeat_delay = 500

key_repeat_frequency
^^^^^^^^^^^^^^^^^^^^

**Description**
   How often keys repeat every second.

   .. Tip:: This configurable option supports decimals.

**Default**
   .. Todo:: Unknown

**Example**
   .. code-block:: text

      key_repeat_frequency = 24.9

keybindings
^^^^^^^^^^^

**Description**
   Sometimes it may be useful to map keybindings. Wayland won't allow clients to capture the Win Key for example.

   .. Tip:: See `virtual key codes <https://docs.microsoft.com/en-us/windows/win32/inputdev/virtual-key-codes>`_

   .. Hint:: keybindings needs to have a multiple of two elements.

**Default**
   None

**Example**
   .. code-block:: text

      keybindings = [
        0x10, 0xA0,
        0x11, 0xA2,
        0x12, 0xA4,
        0x4A, 0x4B
      ]

key_rightalt_to_key_win
^^^^^^^^^^^^^^^^^^^^^^^

**Description**
   It may be possible that you cannot send the Windows Key from Moonlight directly. In those cases it may be useful to
   make Sunshine think the Right Alt key is the Windows key.

**Default**
   None

**Example**
   .. code-block:: text

      key_rightalt_to_key_win = enabled

Display
-------

adapter_name
^^^^^^^^^^^^

**Description**
   Select the video card you want to stream.

   .. Tip:: To find the name of the appropriate values follow these instructions.

      **Linux + VA-API**
         Unlike with `amdvce` and `nvenc`, it doesn't matter if video encoding is done on a different GPU.

         .. code-block:: bash

            ls /dev/dri/renderD*  # to find all devices capable of VAAPI

            # replace ``renderD129`` with the device from above to lists the name and capabilities of the device
            vainfo --display drm --device /dev/dri/renderD129 | \
              grep -E "((VAProfileH264High|VAProfileHEVCMain|VAProfileHEVCMain10).*VAEntrypointEncSlice)|Driver version"

         To be supported by Sunshine, it needs to have at the very minimum:
         ``VAProfileH264High   : VAEntrypointEncSlice``

      .. Todo:: macOS

      **Windows**
         .. code-block:: batch

            tools\dxgi-info.exe

**Default**
   Sunshine will select the default video card.

**Examples**
   **Linux**
      .. code-block:: text

         adapter_name = /dev/dri/renderD128

   .. Todo:: macOS

   **Windows**
      .. code-block:: text

         adapter_name = Radeon RX 580 Series

output_name
^^^^^^^^^^^

**Description**
   Select the display number you want to stream.

   .. Tip:: To find the name of the appropriate values follow these instructions.

      **Linux**
         .. code-block:: bash

            xrandr --listmonitors

         Example output: ``0: +HDMI-1 1920/518x1200/324+0+0  HDMI-1``

         You need to use the value before the colon in the output, e.g. ``0``.

      .. Todo:: macOS

      **Windows**
         .. code-block:: batch

            tools\dxgi-info.exe

**Default**
   Sunshine will select the default display.

**Examples**
   **Linux**
      .. code-block:: text

         output_name = 0

   .. Todo:: macOS

   **Windows**
      .. code-block:: text

         output_name  = \\.\DISPLAY1

fps
^^^

**Description**
   The fps modes advertised by Sunshine.

   .. Note:: Some versions of Moonlight, such as Moonlight-nx (Switch), rely on this list to ensure that the requested
      fps is supported.

**Default**
   .. Todo:: Unknown

**Example**
   .. code-block:: text

      fps = [10, 30, 60, 90, 120]

resolutions
^^^^^^^^^^^

**Description**
   The resolutions advertised by Sunshine.

   .. Note:: Some versions of Moonlight, such as Moonlight-nx (Switch), rely on this list to ensure that the requested
      resolution is supported.

**Default**
   .. Todo:: Unknown

**Example**
   .. code-block:: text

      resolutions = [
        352x240,
        480x360,
        858x480,
        1280x720,
        1920x1080,
        2560x1080,
        3440x1440,
        1920x1200,
        3860x2160,
        3840x1600,
      ]

dwmflush
^^^^^^^^

**Description**
   Invoke DwmFlush() to sync screen capture to the Windows presentation interval.

   .. Caution:: Applies to Windows only. Alleviates visual stuttering during mouse movement.
      If enabled, this feature will automatically deactivate if the client framerate exceeds
      the host monitor's current refresh rate.

   .. Note:: If you disable this option, you may see video stuttering during mouse movement in certain scenarios.
      It is recommended to leave enabled when possible.

**Default**
   ``enabled``

**Example**
   .. code-block:: text

      dwmflush = enabled

Audio
-----

audio_sink
^^^^^^^^^^

**Description**
   The name of the audio sink used for audio loopback.

   .. Tip:: To find the name of the audio sink follow these instructions.

      **Linux + pulseaudio**
         .. code-block:: bash

            pacmd list-sinks | grep "name:"

      **Linux + pipewire**
         .. code-block:: bash

            pactl info | grep Source
            # in some causes you'd need to use the `Sink` device, if `Source` doesn't work, so try:
            pactl info | grep Sink

      **macOS**
         Sunshine can only access microphones on macOS due to system limitations. To stream system audio use
         `Soundflower <https://github.com/mattingalls/Soundflower>`_ or
         `BlackHole <https://github.com/ExistentialAudio/BlackHole>`_.

      **Windows**
         .. code-block:: batch

            tools\audio-info.exe

**Default**
   Sunshine will select the default audio device.

**Examples**
   **Linux**
      .. code-block:: text

         audio_sink = alsa_output.pci-0000_09_00.3.analog-stereo

   **macOS**
      .. code-block:: text

         audio_sink = BlackHole 2ch

   **Windows**
      .. code-block:: text

         audio_sink = {0.0.0.00000000}.{FD47D9CC-4218-4135-9CE2-0C195C87405B}

virtual_sink
^^^^^^^^^^^^

**Description**
   The audio device that's virtual, like Steam Streaming Speakers. This allows Sunshine to stream audio, while muting
   the speakers.

   .. Tip:: See `audio_sink`_!

**Default**
   .. Todo:: Unknown

**Example**
   .. code-block:: text

      virtual_sink = {0.0.0.00000000}.{8edba70c-1125-467c-b89c-15da389bc1d4}

Network
-------

external_ip
^^^^^^^^^^^

**Description**
   If no external IP address is given, Sunshine will attempt to automatically detect external ip-address.

**Default**
   Automatic

**Example**
   .. code-block:: text

      external_ip = 123.456.789.12

port
^^^^

**Description**
   Set the family of ports used by Sunshine. Changing this value will offset other ports per the table below.

.. table::
   :widths: auto

   ================ ============ ===========================
   Port Description Default Port Difference from config port
   ================ ============ ===========================
   HTTPS            47984 TCP    -5
   HTTP             47989 TCP    0
   Web              47990 TCP    +1
   RTSP             48010 TCP    +21
   Video            47998 UDP    +9
   Control          47999 UDP    +10
   Audio            48000 UDP    +11
   Mic (unused)     48002 UDP    +13
   ================ ============ ===========================

.. Attention:: Custom ports are only allowed on select Moonlight clients.

**Default**
   ``47989``

**Example**
   .. code-block:: text

      port = 47989

pkey
^^^^

**Description**
   The private key. This must be 2048 bits.

**Default**
   .. Todo:: Unknown

**Example**
   .. code-block:: text

      pkey = /dir/pkey.pem

cert
^^^^

**Description**
   The certificate. Must be signed with a 2048 bit key.

**Default**
   .. Todo:: Unknown

**Example**
   .. code-block:: text

      cert = /dir/cert.pem

origin_pin_allowed
^^^^^^^^^^^^^^^^^^

**Description**
   The origin of the remote endpoint address that is not denied for HTTP method /pin.

**Choices**

.. table::
   :widths: auto

   =====     ===========
   Value     Description
   =====     ===========
   pc        Only localhost may access /pin
   lan       Only LAN devices may access /pin
   wan       Anyone may access /pin
   =====     ===========

**Default**
   ``pc``

**Example**
   .. code-block:: text

      origin_pin_allowed = pc

origin_web_ui_allowed
^^^^^^^^^^^^^^^^^^^^^

**Description**
   The origin of the remote endpoint address that is not denied for HTTPS Web UI.

**Choices**

.. table::
   :widths: auto

   =====     ===========
   Value     Description
   =====     ===========
   pc        Only localhost may access the web ui
   lan       Only LAN devices may access the web ui
   wan       Anyone may access the web ui
   =====     ===========

**Default**
   ``lan``

**Example**
   .. code-block:: text

      origin_web_ui_allowed = lan

upnp
^^^^

**Description**
   Sunshine will attempt to open ports for streaming over the internet.

**Choices**

.. table::
   :widths: auto

   =====     ===========
   Value     Description
   =====     ===========
   on        enable UPnP
   off       disable UPnP
   =====     ===========

**Default**
   ``off``

**Example**
   .. code-block:: text

      upnp = on

ping_timeout
^^^^^^^^^^^^

**Description**
   How long to wait in milliseconds for data from Moonlight before shutting down the stream.

**Default**
   ``10000``

**Example**
   .. code-block:: text

      ping_timeout = 10000

Encoding
--------

channels
^^^^^^^^

**Description**
   This will generate distinct video streams, unlike simply broadcasting to multiple Clients.

   When multicasting, it could be useful to have different configurations for each connected Client.

   For instance:

   - Clients connected through WAN and LAN have different bitrate constraints.
   - Decoders may require different settings for color.

   .. Warning:: CPU usage increases for each distinct video stream generated.

**Default**
   ``1``

**Example**
   .. code-block:: text

      channels = 1

fec_percentage
^^^^^^^^^^^^^^

**Description**
   Percentage of error correcting packets per data packet in each video frame.

   .. Warning:: Higher values can correct for more network packet loss, but at the cost of increasing bandwidth usage.

**Default**
   ``20``

**Range**
   ``1-255``

**Example**
   .. code-block:: text

      fec_percentage = 20

qp
^^

**Description**
   Quantization Parameter. Some devices don't support Constant Bit Rate. For those devices, QP is used instead.

   .. Warning:: Higher value means more compression, but less quality.

**Default**
   ``28``

**Example**
   .. code-block:: text

      qp = 28

min_threads
^^^^^^^^^^^

**Description**
   Minimum number of threads used for software encoding.

   .. Note:: Increasing the value slightly reduces encoding efficiency, but the tradeoff is usually worth it to gain
      the use of more CPU cores for encoding. The ideal value is the lowest value that can reliably encode at your
      desired streaming settings on your hardware.

**Default**
   ``1``

**Example**
   .. code-block:: text

      min_threads = 1

hevc_mode
^^^^^^^^^

**Description**
   Allows the client to request HEVC Main or HEVC Main10 video streams.

   .. Warning:: HEVC is more CPU-intensive to encode, so enabling this may reduce performance when using software
      encoding.

**Choices**

.. table::
   :widths: auto

   =====     ===========
   Value     Description
   =====     ===========
   0         advertise support for HEVC based on encoder
   1         do not advertise support for HEVC
   2         advertise support for HEVC Main profile
   3         advertise support for HEVC Main and Main10 (HDR) profiles
   =====     ===========

**Default**
   ``0``

**Example**
   .. code-block:: text

      hevc_mode = 2

encoder
^^^^^^^

**Description**
   Force a specific encoder.

**Choices**

.. table::
   :widths: auto

   =========  ===========
   Value      Description
   =========  ===========
   nvenc      For NVIDIA graphics cards
   quicksync  For Intel graphics cards
   amdvce     For AMD graphics cards
   software   Encoding occurs on the CPU
   =========  ===========

**Default**
   Sunshine will use the first encoder that is available.

**Example**
   .. code-block:: text

      encoder = nvenc

sw_preset
^^^^^^^^^

**Description**
   The encoder preset to use.

   .. Note:: This option only applies when using software `encoder`_.

   .. Note:: From `FFmpeg <https://trac.ffmpeg.org/wiki/Encode/H.264#preset>`_.

         A preset is a collection of options that will provide a certain encoding speed to compression ratio. A slower
         preset will provide better compression (compression is quality per filesize). This means that, for example, if
         you target a certain file size or constant bit rate, you will achieve better quality with a slower preset.
         Similarly, for constant quality encoding, you will simply save bitrate by choosing a slower preset.

         Use the slowest preset that you have patience for.

**Choices**

.. table::
   :widths: auto

   ========= ===========
   Value     Description
   ========= ===========
   ultrafast fastest
   superfast
   veryfast
   superfast
   faster
   fast
   medium
   slow
   slow
   slower
   veryslow  slowest
   ========= ===========

**Default**
   ``superfast``

**Example**
   .. code-block:: text

      sw_preset  = superfast

sw_tune
^^^^^^^

**Description**
   The tuning preset to use.

   .. Note:: This option only applies when using software `encoder`_.

   .. Note:: From `FFmpeg <https://trac.ffmpeg.org/wiki/Encode/H.264#preset>`_.

         You can optionally use -tune to change settings based upon the specifics of your input.

**Choices**

.. table::
   :widths: auto

   =========== ===========
   Value       Description
   =========== ===========
   film        use for high quality movie content; lowers deblocking
   animation   good for cartoons; uses higher deblocking and more reference frames
   grain       preserves the grain structure in old, grainy film material
   stillimage  good for slideshow-like content
   fastdecode  allows faster decoding by disabling certain filters
   zerolatency good for fast encoding and low-latency streaming
   =========== ===========

**Default**
   ``zerolatency``

**Example**
   .. code-block:: text

      sw_tune    = zerolatency

nv_preset
^^^^^^^^^

**Description**
   The encoder preset to use.

   .. Note:: This option only applies when using nvenc `encoder`_. For more information on the presets, see
      `nvenc preset migration guide <https://docs.nvidia.com/video-technologies/video-codec-sdk/nvenc-preset-migration-guide/>`_.

**Choices**

.. table::
   :widths: auto

   ========== ===========
   Value      Description
   ========== ===========
   p1         fastest (lowest quality)
   p2         faster (lower quality)
   p3         fast (low quality)
   p4         medium (default)
   p5         slow (good quality)
   p6         slower (better quality)
   p7         slowest (best quality)
   ========== ===========

**Default**
   ``p4``

**Example**
   .. code-block:: text

      nv_preset = p4

nv_tune
^^^^^^^

**Description**
   The encoder tuning profile.

   .. Note:: This option only applies when using nvenc `encoder`_.

**Choices**

.. table::
   :widths: auto

   ========== ===========
   Value      Description
   ========== ===========
   hq         high quality
   ll         low latency
   ull        ultra low latency
   lossless   lossless
   ========== ===========

**Default**
   ``ull``

**Example**
   .. code-block:: text

      nv_tune = ull

nv_rc
^^^^^

**Description**
   The encoder rate control.

   .. Note:: This option only applies when using nvenc `encoder`_.

**Choices**

.. table::
   :widths: auto

   ========== ===========
   Value      Description
   ========== ===========
   constqp    constant QP mode
   vbr        variable bitrate
   cbr        constant bitrate
   ========== ===========

**Default**
   ``cbr``

**Example**
   .. code-block:: text

      nv_rc = cbr

nv_coder
^^^^^^^^

**Description**
   The entropy encoding to use.

   .. Note:: This option only applies when using H264 with nvenc `encoder`_.

**Choices**

.. table::
   :widths: auto

   ========== ===========
   Value      Description
   ========== ===========
   auto       let ffmpeg decide
   cabac      context adaptive binary arithmetic coding - higher quality
   cavlc      context adaptive variable-length coding - faster decode
   ========== ===========

**Default**
   ``auto``

**Example**
   .. code-block:: text

      nv_coder = auto

qsv_preset
^^^^^^^^^^

**Description**
   The encoder preset to use.

   .. Note:: This option only applies when using quicksync `encoder`_.

**Choices**

.. table::
   :widths: auto

   ========== ===========
   Value      Description
   ========== ===========
   veryfast   fastest (lowest quality)
   faster     faster (lower quality)
   fast       fast (low quality)
   medium     medium (default)
   slow       slow (good quality)
   slower     slower (better quality)
   veryslow   slowest (best quality)
   ========== ===========

**Default**
   ``medium``

**Example**
   .. code-block:: text

      qsv_preset = medium

qsv_coder
^^^^^^^^^

**Description**
   The entropy encoding to use.

   .. Note:: This option only applies when using H264 with quicksync `encoder`_.

**Choices**

.. table::
   :widths: auto

   ========== ===========
   Value      Description
   ========== ===========
   auto       let ffmpeg decide
   cabac      context adaptive binary arithmetic coding - higher quality
   cavlc      context adaptive variable-length coding - faster decode
   ========== ===========

**Default**
   ``auto``

**Example**
   .. code-block:: text

      qsv_coder = auto

amd_quality
^^^^^^^^^^^

**Description**
   The encoder preset to use.

   .. Note:: This option only applies when using amdvce `encoder`_.

**Choices**

.. table::
   :widths: auto

   ========== ===========
   Value      Description
   ========== ===========
   speed      prefer speed
   balanced   balanced
   quality    prefer quality
   ========== ===========

**Default**
   ``balanced``

**Example**
   .. code-block:: text

      amd_quality = balanced

amd_rc
^^^^^^

**Description**
   The encoder rate control.

   .. Note:: This option only applies when using amdvce `encoder`_.

**Choices**

.. table::
   :widths: auto

   =========== ===========
   Value       Description
   =========== ===========
   cqp         constant qp mode
   cbr         constant bitrate
   vbr_latency variable bitrate, latency constrained
   vbr_peak    variable bitrate, peak constrained
   =========== ===========

**Default**
   ``vbr_latency``

**Example**
   .. code-block:: text

      amd_rc = vbr_latency

amd_coder
^^^^^^^^^

**Description**
   The entropy encoding to use.

   .. Note:: This option only applies when using H264 with amdvce `encoder`_.

**Choices**

.. table::
   :widths: auto

   ========== ===========
   Value      Description
   ========== ===========
   auto       let ffmpeg decide
   cabac      context adaptive variable-length coding - higher quality
   cavlc      context adaptive binary arithmetic coding - faster decode
   ========== ===========

**Default**
   ``auto``

**Example**
   .. code-block:: text

      amd_coder = auto

vt_software
^^^^^^^^^^^

**Description**
   Force Video Toolbox to use software encoding.

   .. Note:: This option only applies when using macOS.

**Choices**

.. table::
   :widths: auto

   ========== ===========
   Value      Description
   ========== ===========
   auto       let ffmpeg decide
   disabled   disable software encoding
   allowed    allow software encoding
   forced     force software encoding
   ========== ===========

**Default**
   ``auto``

**Example**
   .. code-block:: text

      vt_software = auto

vt_realtime
^^^^^^^^^^^

**Description**
   Realtime encoding.

   .. Note:: This option only applies when using macOS.

   .. Warning:: Disabling realtime encoding might result in a delayed frame encoding or frame drop.

**Default**
   ``enabled``

**Example**
   .. code-block:: text

      vt_realtime = enabled

vt_coder
^^^^^^^^

**Description**
   The entropy encoding to use.

   .. Note:: This option only applies when using macOS.

**Choices**

.. table::
   :widths: auto

   ========== ===========
   Value      Description
   ========== ===========
   auto       let ffmpeg decide
   cabac
   cavlc
   ========== ===========

**Default**
   ``auto``

**Example**
   .. code-block:: text

      vt_coder = auto

Advanced
--------

file_apps
^^^^^^^^^

**Description**
   The application configuration file path. The file contains a json formatted list of applications that can be started
   by Moonlight.

**Default**
   OS and package dependent

**Example**
   .. code-block:: text

      file_apps = apps.json

file_state
^^^^^^^^^^

**Description**
   The file where current state of Sunshine is stored.

**Default**
   ``sunshine_state.json``

**Example**
   .. code-block:: text

      file_state = sunshine_state.json

credentials_file
^^^^^^^^^^^^^^^^

**Description**
   The file where user credentials for the UI are stored.

**Default**
   ``sunshine_state.json``

**Example**
   .. code-block:: text

      credentials_file = sunshine_state.json
