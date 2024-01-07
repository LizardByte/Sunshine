macOS
=====

Requirements
------------
macOS Big Sur and Xcode 12.5+

Use either `MacPorts <https://www.macports.org>`__ or `Homebrew <https://brew.sh>`__

MacPorts
""""""""
Install Requirements
   .. code-block:: bash

      sudo port install avahi boost180 cmake curl libopus miniupnpc npm9 pkgconfig

Homebrew
""""""""
Install Requirements
   .. code-block:: bash

      brew install boost cmake miniupnpc node opus pkg-config
      # if there are issues with an SSL header that is not found:
      cd /usr/local/include
      ln -s ../opt/openssl/include/openssl .

Build
-----
.. attention:: Ensure you are in the build directory created during the clone step earlier before continuing.

.. code-block:: bash

   cmake ..
   make -j ${nproc}

   cpack -G DragNDrop  # optionally, create a macOS dmg package

If cmake fails complaining to find Boost, try to set the path explicitly.
  ``cmake -DBOOST_ROOT=[boost path] ..``, e.g., ``cmake -DBOOST_ROOT=/opt/local/libexec/boost/1.80 ..``
