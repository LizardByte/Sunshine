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

   When translations are updated on CrowdIn, a push gets made to the `l10n_master` branch and a PR is made against the
   `master` branch. Once PR is merged, all updated translations are part of the project and will be included in the
   next release.

Extraction
----------

.. tab:: UI

   Sunshine uses `Vue I18n <https://vue-i18n.intlify.dev/>`__ for localizing the UI.
   The following is a simple example of how to use it.

   - Add the string to `src_assets/common/assets/web/public/assets/locale/en.json`, in English.
      .. code-block:: json

         {
           "index": {
             "welcome": "Hello, Sunshine!"
           }
         }

      .. note:: The json keys should be sorted alphabetically. You can use `jsonabc <https://novicelab.org/jsonabc/>`__
         to sort the keys.

      .. attention:: Due to the integration with Crowdin, it is important to only add strings to the `en.json` file,
         and to not modify any other language files. After the PR is merged, the translations can take place
         on `CrowdIn <https://translate.lizardbyte.dev/>`__. Once the translations are complete, a PR will be made
         to merge the translations into Sunshine.

   - Use the string in a Vue component.
      .. code-block:: html

         <template>
           <div>
             <p>{{ $t('index.welcome') }}</p>
           </div>
         </template>

   .. tip:: More formatting examples can be found in the
      `Vue I18n guide <https://kazupon.github.io/vue-i18n/guide/formatting.html>`__.

.. tab:: C++

   There should be minimal cases where strings need to be extracted from C++ source code; however it may be necessary in
   some situations. For example the system tray icon could be localized as it is user interfacing.

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
   `.github/workflows/localize.yml` workflow and is run on any push event into the `master` branch. Jobs are only run if
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

   .. attention:: Due to the integration with Crowdin, it is important to not include any extracted or compiled files in
      Pull Requests. The files are automatically generated and updated by the workflow. Once the PR is merged, the
      translations can take place on `CrowdIn <https://translate.lizardbyte.dev/>`__. Once the translations are
      complete, a PR will be made to merge the translations into Sunshine.
