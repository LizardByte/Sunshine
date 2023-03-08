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
Sunshine uses `Sphinx <https://www.sphinx-doc.org/en/master/>`_ for documentation building. Sphinx, along with other
required python dependencies are included in the `./docs/requirements.txt` file. Python is required to build
sphinx docs. Installation and setup of python will not be covered here.

Doxygen is used to generate the XML files required by Sphinx. Doxygen can be obtained from
`Doxygen downloads <https://www.doxygen.nl/download.html>`_. Ensure that the `doxygen` executable is in your path.

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

Unit Testing
------------
.. Todo:: Sunshine does not currently have any unit tests. If you would like to help us improve please get in contact
   with us, or make a PR with suggested changes.
