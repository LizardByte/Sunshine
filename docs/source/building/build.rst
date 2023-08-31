Build
=====
Sunshine binaries are built using `CMake <https://cmake.org/>`__. Cross compilation is not
supported. That means the binaries must be built on the target operating system and architecture.

Building Locally
----------------

Clone
^^^^^
Ensure `git <https://git-scm.com/>`__ is installed and run the following:
   .. code-block:: bash

      git clone https://github.com/lizardbyte/sunshine.git --recurse-submodules
      cd sunshine && mkdir build && cd build

Compile
^^^^^^^
See the section specific to your OS.

- :ref:`Linux <building/linux:linux>`
- :ref:`macOS <building/macos:macos>`
- :ref:`Windows <building/windows:windows>`

Remote Build
------------
It may be beneficial to build remotely in some cases. This will enable easier building on different operating systems.

#. Fork the project
#. Activate workflows
#. Trigger the `CI` workflow manually
#. Download the artifacts/binaries from the workflow run summary
