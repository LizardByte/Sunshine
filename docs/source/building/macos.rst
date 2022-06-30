:github_url: https://github.com/SunshineStream/Sunshine/tree/nightly/docs/source/building/macos.rst

macOS
=====

Requirements
------------
macOS Big Sur and Xcode 12.5+

Use either `MacPorts <https://www.macports.org>`_ or `Homebrew <https://brew.sh>`_

MacPorts
""""""""
Install Requirements
   .. code-block:: bash

      sudo port install cmake boost ffmpeg libopus

Homebrew
""""""""
Install Requirements
   .. code-block:: bash

      brew install boost cmake ffmpeg opus
      # if there are issues with an SSL header that is not found:
      cd /usr/local/include
      ln -s ../opt/openssl/include/openssl .

Build
-----
.. Attention:: Ensure you are in the build directory created during the clone step earlier before continuing.

   .. code-block:: bash

      cmake ..
      make -j ${nproc}

      cpack -G DragNDrop  # optionally, create a macOS dmg package

If cmake fails complaining to find Boost, try to set the path explicitly.

  ``cmake -DBOOST_ROOT=[boost path] ..``, e.g., ``cmake -DBOOST_ROOT=/opt/local/libexec/boost/1.76 ..``

