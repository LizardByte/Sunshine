Localization
============
Sunshine and related LizardByte projects are being localized into various languages. The default language is
`en` (English).

 .. image:: https://app.lizardbyte.dev/uno/crowdin/LizardByte_graph.svg

CrowdIn
-------
The translations occur on `CrowdIn <https://translate.lizardbyte.dev/>`__. Anyone is free to contribute to
localization there.

**Translations Basics**
   - The brand names `LizardByte` and `Sunshine` should never be translated.
   - Other brand names should never be translated.
     Examples:

     - AMD
     - Nvidia

**CrowdIn Integration**
   How does it work?

   When a change is made to sunshine source code, a workflow generates new translation templates
   that get pushed to CrowdIn automatically.

   When translations are updated on CrowdIn, a push gets made to the `l10n_nightly` branch and a PR is made against the
   `nightly` branch. Once PR is merged, all updated translations are part of the project and will be included in the
   next release.

Extraction
----------
There should be minimal cases where strings need to be extracted from source code; however it may be necessary in some
situations. For example if a system tray icon is added it should be localized as it is user interfacing.

- Wrap the string to be extracted in a function as shown.
   .. code-block:: cpp

      #include <boost/locale.hpp>
      #include <string>

      std::string msg = boost::locale::translate("Hello world!");

.. tip:: More examples can be found in the documentation for
   `boost locale <https://www.boost.org/doc/libs/1_70_0/libs/locale/doc/html/messages_formatting.html>`__.

.. warning:: This is for information only. Contributors should never include manually updated template files, or
   manually compiled language files in Pull Requests.

Strings are automatically extracted from the code to the `locale/sunshine.po` template file. The generated file is
used by CrowdIn to generate language specific template files. The file is generated using the
`.github/workflows/localize.yml` workflow and is run on any push event into the `nightly` branch. Jobs are only run if
any of the following paths are modified.

.. code-block:: yaml

   - 'src/**'

When testing locally it may be desirable to manually extract, initialize, update, and compile strings. Python is
required for this, along with the python dependencies in the `./scripts/requirements.txt` file. Additionally,
`xgettext <https://www.gnu.org/software/gettext/>`__ must be installed.

**Extract, initialize, and update**
   .. code-block:: bash

      python ./scripts/_locale.py --extract --init --update

**Compile**
   .. code-block:: bash

      python ./scripts/_locale.py --compile
