# Configuration file for the Sphinx documentation builder.
#
# This file only contains a selection of the most common options. For a full
# list see the documentation:
# https://www.sphinx-doc.org/en/master/usage/configuration.html

# standard imports
from datetime import datetime
import os
import subprocess


# -- Path setup --------------------------------------------------------------

# If extensions (or modules to document with autodoc) are in another directory,
# add these directories to sys.path here. If the directory is relative to the
# documentation root, use os.path.abspath to make it absolute, like shown here.

script_dir = os.path.dirname(os.path.abspath(__file__))  # the directory of this file
source_dir = os.path.dirname(script_dir)  # the source folder directory
root_dir = os.path.dirname(source_dir)  # the root folder directory

# -- Project information -----------------------------------------------------
project = 'Sunshine'
project_copyright = f'{datetime.now ().year}, {project}'
author = 'ReenigneArcher'

# The full version, including alpha/beta/rc tags
# https://docs.readthedocs.io/en/stable/reference/environment-variables.html#envvar-READTHEDOCS_VERSION
version = os.getenv('READTHEDOCS_VERSION', 'dirty')

# -- General configuration ---------------------------------------------------

# Add any Sphinx extension module names here, as strings. They can be
# extensions coming with Sphinx (named 'sphinx.ext.*') or your custom
# ones.
extensions = [
    'breathe',  # c++ support for sphinx with doxygen
    'm2r2',  # enable markdown files
    'sphinx.ext.autosectionlabel',
    'sphinx.ext.todo',  # enable to-do sections
    'sphinx.ext.graphviz',  # enable graphs for breathe
    'sphinx.ext.viewcode',  # add links to view source code
    'sphinx_copybutton',  # add a copy button to code blocks
    'sphinx_inline_tabs',  # add tabs
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

# images
html_favicon = os.path.join(root_dir, 'src_assets', 'common', 'assets', 'web', 'public', 'images', 'sunshine.ico')
html_logo = os.path.join(root_dir, 'sunshine.png')

# Add any paths that contain custom static files (such as style sheets) here,
# relative to this directory. They are copied after the builtin static files,
# so a file named "default.css" will overwrite the builtin "default.css".
# html_static_path = ['_static']

# The theme to use for HTML and HTML Help pages.  See the documentation for
# a list of builtin themes.
html_theme = 'furo'

html_theme_options = {
    "top_of_page_button": "edit",
    "source_edit_link": "https://github.com/lizardbyte/sunshine/tree/nightly/docs/source/{filename}",
}

# extension config options
autosectionlabel_prefix_document = True  # Make sure the target is unique
breathe_default_project = 'src'
breathe_implementation_filename_extensions = ['.c', '.cc', '.cpp', '.mm']
breathe_order_parameters_first = False
breathe_projects = dict(
    src="../build/doxyxml"
)
todo_include_todos = True

# disable epub mimetype warnings
# https://github.com/readthedocs/readthedocs.org/blob/eadf6ac6dc6abc760a91e1cb147cc3c5f37d1ea8/docs/conf.py#L235-L236
suppress_warnings = ["epub.unknown_project_files"]

# get doxygen version
doxy_proc = subprocess.run('doxygen --version', shell=True, cwd=source_dir, capture_output=True)
doxy_version = doxy_proc.stdout.decode('utf-8').strip()
print('doxygen version: ' + doxy_version)

# create build directories, as doxygen fails to create it in macports and docker
directories = [
    os.path.join(source_dir, 'build'),
    os.path.join(source_dir, 'build', 'doxyxml'),
]
for d in directories:
    os.makedirs(
        name=d,
        exist_ok=True,
    )

# run doxygen
doxy_proc = subprocess.run('doxygen Doxyfile', shell=True, cwd=source_dir)
if doxy_proc.returncode != 0:
    raise RuntimeError('doxygen failed with return code ' + str(doxy_proc.returncode))
