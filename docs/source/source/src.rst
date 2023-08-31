src
===
We are in process of improving the source code documentation. Code should be documented using Doxygen syntax.
Some examples exist in `main.h` and `main.cpp`. In order for documentation within the code to appear in the
rendered docs, the definition of the object must be in a header file, although the documentation itself can (and
should) be in the source file.

Example Documentation Blocks
----------------------------

**file.h**

.. code-block:: c

   // functions
   int main(int argc, char *argv[]);

**file.cpp** (with markdown)

.. code-block:: cpp

   /**
    * @brief Main application entry point.
    * @param argc The number of arguments.
    * @param argv The arguments.
    *
    * EXAMPLES:
    * ```cpp
    * main(1, const char* args[] = {"hello", "markdown", nullptr});
    * ```
    */
   int main(int argc, char *argv[]) {
     // do stuff
   }

**file.cpp** (with ReStructuredText)

.. code-block:: cpp

   /**
    * @brief Main application entry point.
    * @param argc The number of arguments.
    * @param argv The arguments.
    * @rst
    * EXAMPLES:
    *
    * .. code-block:: cpp
    *    main(1, const char* args[] = {"hello", "rst", nullptr});
    * @endrst
    */
   int main(int argc, char *argv[]) {
     // do stuff
   }

Code
----

.. toctree::
   :maxdepth: 2
   :caption: src

   src/main
   src/audio
   src/cbs
   src/config
   src/confighttp
   src/crypto
   src/httpcommon
   src/input
   src/move_by_copy
   src/network
   src/nvhttp
   src/process
   src/round_robin
   src/rtsp
   src/stream
   src/sync
   src/system_tray
   src/task_pool
   src/thread_pool
   src/thread_safe
   src/upnp
   src/utility
   src/uuid
   src/video
   src/platform
