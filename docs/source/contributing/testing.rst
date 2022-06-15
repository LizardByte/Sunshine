:github_url: https://github.com/RetroArcher/RetroArcher/tree/nightly/docs/source/contributing/testing.rst

Testing
=======

Clang Format
------------
Source code is tested against the `.clang-format` file for linting errors. The workflow file responsible for clang
format testing is `.github/workflows/clang.yml`.

Test clang-format locally.
   .. Todo:: This documentation needs to be improved.

   .. code-block:: bash

      clang-format ...

Sphinx
------
Sunshine uses `Sphinx <https://www.sphinx-doc.org/en/master/>`_ for documentation building. Sphinx is included
in the `./scripts/requirements.txt` file. Python is required to build sphinx docs. Installation and setup of python
will not be covered here.

The config file for Sphinx is `docs/source/conf.py`. This is already included in the repo and should not be modified.

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
