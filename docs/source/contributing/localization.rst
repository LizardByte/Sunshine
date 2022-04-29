:github_url: https://github.com/SunshineStream/Sunshine/tree/nightly/docs/source/contributing/localization.rst

Localization
============
Sunshine is being localized into various languages. The default language is `en` (English) and is highlighted green.

.. image:: https://img.shields.io/badge/dynamic/json?color=blue&label=de&style=for-the-badge&query=%24.progress.0.data.translationProgress&url=https%3A%2F%2Fbadges.awesome-crowdin.com%2Fstats-15178612-503956.json
.. image:: https://img.shields.io/badge/dynamic/json?color=green&label=en&style=for-the-badge&query=%24.progress.1.data.translationProgress&url=https%3A%2F%2Fbadges.awesome-crowdin.com%2Fstats-15178612-503956.json
.. image:: https://img.shields.io/badge/dynamic/json?color=blue&label=en-GB&style=for-the-badge&query=%24.progress.2.data.translationProgress&url=https%3A%2F%2Fbadges.awesome-crowdin.com%2Fstats-15178612-503956.json
.. image:: https://img.shields.io/badge/dynamic/json?color=blue&label=en-US&style=for-the-badge&query=%24.progress.3.data.translationProgress&url=https%3A%2F%2Fbadges.awesome-crowdin.com%2Fstats-15178612-503956.json
.. image:: https://img.shields.io/badge/dynamic/json?color=blue&label=es-ES&style=for-the-badge&query=%24.progress.4.data.translationProgress&url=https%3A%2F%2Fbadges.awesome-crowdin.com%2Fstats-15178612-503956.json
.. image:: https://img.shields.io/badge/dynamic/json?color=blue&label=fr&style=for-the-badge&query=%24.progress.5.data.translationProgress&url=https%3A%2F%2Fbadges.awesome-crowdin.com%2Fstats-15178612-503956.json
.. image:: https://img.shields.io/badge/dynamic/json?color=blue&label=it&style=for-the-badge&query=%24.progress.6.data.translationProgress&url=https%3A%2F%2Fbadges.awesome-crowdin.com%2Fstats-15178612-503956.json
.. image:: https://img.shields.io/badge/dynamic/json?color=blue&label=ru&style=for-the-badge&query=%24.progress.7.data.translationProgress&url=https%3A%2F%2Fbadges.awesome-crowdin.com%2Fstats-15178612-503956.json

Graph
   .. image:: https://badges.awesome-crowdin.com/translation-15178612-503956.png

CrowdIn
-------
The translations occur on
`CrowdIn <https://crowdin.com/project/sunshinestream>`_. Feel free to contribute to localization there.
Only elements of the API are planned to be translated.

.. Attention:: The rest API has not yet been implemented.

Translations Basics
   - The brand names `SunshineStream` and `Sunshine` should never be translated.
   - Other brand names should never be translated.
     Examples:

       - AMD
       - Nvidia

CrowdIn Integration
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
         boost::locale::translate("Hello world!")

   .. Tip:: More examples can be found in the documentation for
      `boost locale <https://www.boost.org/doc/libs/1_70_0/libs/locale/doc/html/messages_formatting.html>`_.

.. Warning:: This is for information only. Contributors should never include manually updated template files, or
   manually compiled language files in Pull Requests.

Strings are automatically extracted from the code to the `locale/sunshine.po` template file. The generated file is
used by CrowdIn to generate language specific template files. The file is generated using the
`.github/workflows/localize.yml` workflow and is run on any push event into the `nightly` branch. Jobs are only run if
any of the following paths are modified.

   .. code-block:: yaml

      - 'sunshine/**'

When testing locally it may be desirable to manually extract, initialize, update, and compile strings. Python is
required for this, along with the python dependencies in the `./scripts/requirements.txt` file. Additionally,
`xgettext <https://www.gnu.org/software/gettext/>`_ must be installed.

   Extract, initialize, and update
      .. code-block:: bash

         python ./scripts/_locale.py --extract --init --update

   Compile
      .. code-block:: bash

         python ./scripts/_locale.py --compile
