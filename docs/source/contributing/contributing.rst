Contributing
============

Read our contribution guide in our organization level
`docs <https://lizardbyte.readthedocs.io/en/latest/developers/contributing.html>`__.

Web UI
------
The Web UI uses `Vite <https://vitejs.dev/>`__ as its build system, to handle the integration of the NPM libraries.

The HTML pages used by the Web UI are found in ``src_assets/common/assets/web``.

`EJS <https://www.npmjs.com/package/vite-plugin-ejs>`__ is used as a templating system for the pages (check ``template_header.html`` and ``template_header_main.html``).

The Style System is provided by `Bootstrap <https://getbootstrap.com/>`__.

The JS framework used by the more interactive pages is `Vue <https://vuejs.org/>`__.

Building
^^^^^^^^
Sunshine already builds the UI as part of its build process, but you can make faster changes by starting vite manually.

.. code-block:: bash

   npm run dev