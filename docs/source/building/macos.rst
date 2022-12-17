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

      sudo port install boost cmake libopus npm9

Homebrew
""""""""
Install Requirements
   .. code-block:: bash

      brew install boost cmake node opus
      # if there are issues with an SSL header that is not found:
      cd /usr/local/include
      ln -s ../opt/openssl/include/openssl .

npm dependencies
----------------
Install npm dependencies.
   .. code-block:: bash

      pushd ./src_assets/common/assets/web
      npm install
      popd

Build
-----
.. Attention:: Ensure you are in the build directory created during the clone step earlier before continuing.

.. code-block:: bash

   cmake ..
   make -j ${nproc}

   cpack -G DragNDrop  # optionally, create a macOS dmg package

If cmake fails complaining to find Boost, try to set the path explicitly.
  ``cmake -DBOOST_ROOT=[boost path] ..``, e.g., ``cmake -DBOOST_ROOT=/opt/local/libexec/boost/1.80 ..``
