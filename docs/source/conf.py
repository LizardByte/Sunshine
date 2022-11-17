# Configuration file for the Sphinx documentation builder.
#
# This file only contains a selection of the most common options. For a full
# list see the documentation:
# https://www.sphinx-doc.org/en/master/usage/configuration.html

# standard imports
from datetime import datetime
import os
import re


# -- Path setup --------------------------------------------------------------

# If extensions (or modules to document with autodoc) are in another directory,
# add these directories to sys.path here. If the directory is relative to the
# documentation root, use os.path.abspath to make it absolute, like shown here.

script_dir = os.path.dirname(os.path.abspath(__file__))  # the directory of this file
source_dir = os.path.dirname(script_dir)  # the source folder directory
root_dir = os.path.dirname(source_dir)  # the root folder directory

# -- Project information -----------------------------------------------------
project = 'Sunshine'
copyright = f'{datetime.now ().year}, {project}'
author = 'ReenigneArcher'

# The full version, including alpha/beta/rc tags
with open(os.path.join(root_dir, 'CMakeLists.txt'), 'r') as f:
    version = re.search(r"project\(Sunshine VERSION ((\d+)\.(\d+)\.(\d+))", str(f.read())).group(1)
"""
To use cmake method for obtaining version instead of regex,
1. Within CMakeLists.txt add the following line without backticks:
   ``configure_file(docs/source/conf.py.in "${CMAKE_CURRENT_SOURCE_DIR}/docs/source/conf.py" @ONLY)``
2. Rename this file to ``conf.py.in``
3. Uncomment the next line
"""
# version = '@PROJECT_VERSION@'  # use this for cmake configure_file method

# -- General configuration ---------------------------------------------------

# Add any Sphinx extension module names here, as strings. They can be
# extensions coming with Sphinx (named 'sphinx.ext.*') or your custom
# ones.
extensions = [
    'm2r2',  # enable markdown files
    'sphinx.ext.autosectionlabel',
    'sphinx.ext.todo',  # enable to-do sections
    'sphinx.ext.viewcode',  # add links to view source code
    'sphinx_copybutton',  # add a copy button to code blocks
]

# Add any paths that contain templates here, relative to this directory.
# templates_path = ['_templates']

# List of patterns, relative to source directory, that match files and
# directories to ignore when looking for source files.
# This pattern also affects html_static_path and html_extra_path.
exclude_patterns = ['toc.rst']

# Extensions to include.
source_suffix = ['.rst', '.md']


# -- Options for HTML output -------------------------------------------------

# Add any paths that contain custom static files (such as style sheets) here,
# relative to this directory. They are copied after the builtin static files,
# so a file named "default.css" will overwrite the builtin "default.css".
# html_static_path = ['_static']

html_logo = os.path.join(root_dir, 'sunshine.png')

# The theme to use for HTML and HTML Help pages.  See the documentation for
# a list of builtin themes.
html_theme = 'furo'

html_theme_options = {
    "top_of_page_button": "edit",
    "source_edit_link": "https://github.com/lizardbyte/sunshine/tree/nightly/docs/source/{filename}",
}

# extension config options
autosectionlabel_prefix_document = True  # Make sure the target is unique
todo_include_todos = True
