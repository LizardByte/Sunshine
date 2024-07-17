Linux
=====

Hardware Encoding fails
-----------------------
Due to legal concerns, Mesa has disabled hardware decoding and encoding by default.

.. code-block:: text

   Error: Could not open codec [h264_vaapi]: Function not implemented

If you see the above error in the Sunshine logs, compiling `Mesa`
manually, may be required. See the official Mesa3D `Compiling and Installing <https://docs.mesa3d.org/install.html>`__
documentation for instructions.

.. important:: You must re-enable the disabled encoders. You can do so, by passing the following argument to the build
   system. You may also want to enable decoders, however that is not required for Sunshine and is not covered here.

   .. code-block:: bash

      -Dvideo-codecs=h264enc,h265enc

.. note:: Other build options are listed in the
   `meson options <https://gitlab.freedesktop.org/mesa/mesa/-/blob/main/meson_options.txt>`__ file.

KMS Streaming fails
-------------------
If screencasting fails with KMS, you may need to run the following to force unprivileged screencasting.

.. code-block:: bash

   sudo setcap -r $(readlink -f $(which sunshine))

.. note:: The above command will not work with the AppImage or Flatpak packages.
   Please refer to the :ref:`Setup guide <about/setup:Install>` for more
   specific instructions.

KMS streaming fails on Nvidia GPUs
----------------------------------
If KMS screen capture results in a black screen being streamed, you may need to
set the parameter ``modeset=1`` for Nvidia's kernel module. This can be done by
adding the following directive to the kernel command line:

.. code-block::

   nvidia_drm.modeset=1

Consult your distribution's documentation for details on how to do this. (Most
often grub is used to load the kernel and set its command line.)

AMD encoding latency issues
---------------------------
If you notice unexpectedly high encoding latencies (e.g. in Moolight's
performance overlay) or strong fluctuations thereof, this is due to
`missing support <https://gitlab.freedesktop.org/drm/amd/-/issues/3336>`_
in Mesa/libva for AMD's low latency encoder mode. This is particularly
problematic at higher resolutions (4K).

As of now this can only be fixed by recompiling Mesa with a test patch (see
url above) that unconditionally enables a low latency tuning mode for all
encoder workloads. This is for advanced users only and neither support nor
warranty are provided. Note also that this patch entails a higher power
consumption while encoding and may not be suitable for all use cases.

(A crude workaround is to *increase* the load on the de-/encoder by having it
de- or encode an additional arbitrary video stream in the background. This
ensures that the encoder's clock frequencies stay high and the latency
fluctuations disappear.)

Gamescope compatibility
-----------------------
Some users have reported stuttering issues when streaming games running within Gamescope.
