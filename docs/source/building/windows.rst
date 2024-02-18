Windows
=======

Requirements
------------
First you need to install `MSYS2 <https://www.msys2.org>`__, then startup "MSYS2 UCRT64" and execute the following
codes.

Update all packages:
   .. code-block:: bash

      pacman -Suy

Install dependencies:
   .. code-block:: bash

      pacman -S \
        mingw-w64-ucrt-x86_64-boost
        mingw-w64-ucrt-x86_64-cmake
        mingw-w64-ucrt-x86_64-curl
        mingw-w64-ucrt-x86_64-miniupnpc
        mingw-w64-ucrt-x86_64-nlohmann-json
        mingw-w64-ucrt-x86_64-nodejs
        mingw-w64-ucrt-x86_64-onevpl
        mingw-w64-ucrt-x86_64-openssl
        mingw-w64-ucrt-x86_64-opus
        mingw-w64-ucrt-x86_64-toolchain

Build
-----
.. attention:: Ensure you are in the build directory created during the clone step earlier before continuing.

.. code-block:: bash

   cmake -GNinja ..
   ninja

   cpack -G NSIS  # optionally, create a windows installer
   cpack -G ZIP  # optionally, create a windows standalone package
