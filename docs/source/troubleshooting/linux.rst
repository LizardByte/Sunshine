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

Gamescope compatibility
-----------------------
Some users have reported stuttering issues when streaming games running within Gamescope.
