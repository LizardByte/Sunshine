# Configuration file for the Sphinx documentation builder.
#
# This file only contains a selection of the most common options. For a full
# list see the documentation:
# https://www.sphinx-doc.org/en/master/usage/configuration.html

# standard imports
from datetime import datetime
import os
import shutil
import subprocess
from typing import Mapping, Optional


# re-usable functions
def _run_subprocess(
        args_list: list,
        cwd: Optional[str] = None,
        env: Optional[Mapping] = None,
) -> bool:
    og_dir = os.getcwd()
    if cwd:
        os.chdir(cwd)
    process = subprocess.Popen(
        args=args_list,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        cwd=cwd,
        encoding='utf-8',
        env=env,
        errors='replace',
    )

    if cwd:
        os.chdir(og_dir)

    # Print stdout and stderr in real-time
    # https://stackoverflow.com/a/57970619/11214013
    while True:
        realtime_output = process.stdout.readline()

        if realtime_output == '' and process.poll() is not None:
            break

        if realtime_output:
            print(realtime_output.strip(), flush=True)

    process.stdout.close()

    exit_code = process.wait()

    if exit_code != 0:
        print(f'::error:: Process [{args_list}] failed with exit code', exit_code)
        raise RuntimeError(f'Process [{args_list}] failed with exit code {exit_code}')
    else:
        return True


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
    'm2r2',  # enable markdown files
    'sphinx.ext.autosectionlabel',
    'sphinx.ext.todo',  # enable to-do sections
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
    "source_edit_link": "https://github.com/lizardbyte/sunshine/tree/master/docs/source/{filename}",
}

# extension config options
autosectionlabel_prefix_document = True  # Make sure the target is unique
todo_include_todos = True

# disable epub mimetype warnings
# https://github.com/readthedocs/readthedocs.org/blob/eadf6ac6dc6abc760a91e1cb147cc3c5f37d1ea8/docs/conf.py#L235-L236
suppress_warnings = ["epub.unknown_project_files"]

doxygen_cmd = os.getenv('DOXY_PATH', 'doxygen')
print(f'doxygen command: {doxygen_cmd}')

# get doxygen version
doxy_version = _run_subprocess(
    args_list=[doxygen_cmd, '--version'],
    cwd=source_dir,
)

# create build directories, as doxygen fails to create it in macports and docker
directories = [
    os.path.join(source_dir, 'build'),
    os.path.join(source_dir, 'build', 'doxygen', 'doxyhtml'),
]
for d in directories:
    os.makedirs(
        name=d,
        exist_ok=True,
    )

# remove existing html files
# doxygen builds will not re-generated if the html directory already exists
html_dir = os.path.join(source_dir, 'build', 'html')
if os.path.exists(html_dir):
    shutil.rmtree(html_dir)

# run doxygen
doxy_proc = _run_subprocess(
    args_list=[doxygen_cmd, 'Doxyfile'],
    cwd=source_dir
)

# copy doxygen html files
html_extra_path = [
    os.path.join(source_dir, 'build', 'doxygen'),  # the final directory is omitted in order to have a proper path
]
