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

      sudo port install cmake curl doxygen graphviz libopus miniupnpc npm9 pkgconfig python311 py311-pip

Homebrew
""""""""
Install Requirements
   .. code-block:: bash

      brew install cmake doxygen graphviz icu4c miniupnpc node openssl@3 opus pkg-config python@3.11

If there are issues with an SSL header that is not found:
   .. tab:: Intel

      .. code-block:: bash

         pushd /usr/local/include
         ln -s ../opt/openssl/include/openssl .
         popd

   .. tab:: Apple Silicon

      .. code-block:: bash

         pushd /opt/homebrew/include
         ln -s ../opt/openssl/include/openssl .
         popd

Build
-----
.. attention:: Ensure you are in the build directory created during the clone step earlier before continuing.

.. code-block:: bash

   cmake ..
   make -j $(sysctl -n hw.ncpu)

   cpack -G DragNDrop  # optionally, create a macOS dmg package

If cmake fails complaining to find Boost, try to set the path explicitly.
  ``cmake -DBOOST_ROOT=[boost path] ..``, e.g., ``cmake -DBOOST_ROOT=/opt/local/libexec/boost/1.80 ..``
