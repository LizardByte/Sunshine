Testing
=======

Clang Format
------------
Source code is tested against the `.clang-format` file for linting errors. The workflow file responsible for clang
format testing is `.github/workflows/cpp-clang-format-lint.yml`.

Test clang-format locally.
   .. code-block:: bash

      find ./ -iname *.cpp -o -iname *.h -iname *.m -iname *.mm | xargs clang-format -i

Sphinx
------
.. note:: Documentation is now a cmake target and this is all handled by the cmake build system. When compiling docs
   as a target, simply install Python, Doxygen, and Graphviz. The installation of python dependencies will be handled
   automatically inside a virtual environment. The following instructions are for manual building of the docs.

Sunshine uses `Sphinx <https://www.sphinx-doc.org/en/master/>`__ for documentation building. Sphinx, along with other
required python dependencies are included in the `./docs/requirements.txt` file. Python is required to build
sphinx docs. Installation and setup of python will not be covered here.

Doxygen is used to generate the XML files required by Sphinx. Doxygen can be obtained from
`Doxygen downloads <https://www.doxygen.nl/download.html>`__. Ensure that the `doxygen` executable is in your path.

.. seealso::
   Sphinx is configured to use the graphviz extension. To obtain the dot executable from the Graphviz library,
   see the `library’s downloads section <https://graphviz.org/download/>`__.


The config file for Sphinx is `docs/source/conf.py`. This is already included in the repo and should not be modified.

The config file for Doxygen is `docs/Doxyfile`. This is already included in the repo and should not be modified.

Test with Sphinx
   .. code-block:: bash

      cd docs
      make html

   Alternatively

   .. code-block:: bash

      cd docs
      sphinx-build -b html source build

Lint with rstcheck
   .. code-block:: bash

      rstcheck -r .

Check formatting with rstfmt
   .. code-block:: bash

      rstfmt --check --diff -w 120 .

Format inplace with rstfmt
   .. code-block:: bash

      rstfmt -w 120 .

Unit Testing
------------
Sunshine uses `Google Test <https://github.com/google/googletest>`__ for unit testing. Google Test is included in the
repo as a submodule. The test sources are located in the `./tests` directory.

The tests need to be compiled into an executable, and then run. The tests are built using the normal build process, but
can be disabled by setting the `BUILD_TESTS` CMake option to `OFF`.

To run the tests, execute the following command from the build directory:

.. tab:: Linux

   .. code-block:: bash

      pushd tests
      ./test_sunshine
      popd

.. tab:: macOS

   .. code-block:: bash

      pushd tests
      ./test_sunshine
      popd

.. tab:: Windows

   .. code-block:: bash

      pushd tests
      test_sunshine.exe
      popd

To see all available options, run the tests with the `--help` option.

.. tab:: Linux

   .. code-block:: bash

      pushd tests
      ./test_sunshine --help
      popd

.. tab:: macOS

   .. code-block:: bash

      pushd tests
      ./test_sunshine --help
      popd

.. tab:: Windows

   .. code-block:: bash

      pushd tests
      test_sunshine.exe --help
      popd

Some tests rely on Python to run. CMake will search for Python and enable the docs tests if it is found, otherwise
cmake will fail. You can manually disable the tests by setting the `TESTS_ENABLE_PYTHON_TESTS` CMake option to
`OFF`.

.. tip::

   See the googletest `FAQ <https://google.github.io/googletest/faq.html>`__ for more information on how to use
   Google Test.

We use `gcovr <https://www.gcovr.com/>`__ to generate code coverage reports,
and `Codecov <https://about.codecov.io/>`__ to analyze the reports for all PRs and commits.

Codecov will fail a PR if the total coverage is reduced too much, or if not enough of the diff is covered by tests.
In some cases, the code cannot be covered when running the tests inside of GitHub runners. For example, any test that
needs access to the GPU will not be able to run. In these cases, the coverage can be omitted by adding comments to the
code. See the `gcovr documentation <https://gcovr.com/en/stable/guide/exclusion-markers.html#exclusion-markers>`__ for
more information.

Even if your changes cannot be covered in the CI, we still encourage you to write the tests for them. This will allow
maintainers to run the tests locally.
