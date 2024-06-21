Source Code
===========
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

Source
------

.. doxygenindex::
   :allow-dot-graphs:

.. Ideally, we would use `doxygenfile` with `:allow-dot-graphs:`, but sphinx complains about duplicated namespaces...
