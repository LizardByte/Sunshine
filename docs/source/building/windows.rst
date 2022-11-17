Windows
=======

Requirements
------------
First you need to install `MSYS2 <https://www.msys2.org>`_, then startup "MSYS2 MinGW 64-bit" and install the
following packages using:

.. code-block:: bash

   pacman -S mingw-w64-x86_64-binutils mingw-w64-x86_64-openssl mingw-w64-x86_64-cmake \
   mingw-w64-x86_64-toolchain mingw-w64-x86_64-opus mingw-w64-x86_64-x265 mingw-w64-x86_64-boost \
   git mingw-w64-x86_64-make cmake make gcc

Build
-----
.. Attention:: Ensure you are in the build directory created during the clone step earlier before continuing.

.. code-block:: bash

   cmake -G"Unix Makefiles" ..
   cmake -G"MinGW Makefiles" ..  # alternatively

   mingw32-make

   cpack -G NSIS  # optionally, create a windows installer
   cpack -G ZIP  # optionally, create a windows standalone package
