"""
..
   _locale.py

Functions related to building, initializing, updating, and compiling localization translations.

Borrowed from RetroArcher.
"""
# standard imports
import argparse
import datetime
import os
import subprocess

project_name = 'Sunshine'
project_owner = 'LizardByte'

script_dir = os.path.dirname(os.path.abspath(__file__))
root_dir = os.path.dirname(script_dir)
locale_dir = os.path.join(root_dir, 'locale')
project_dir = os.path.join(root_dir, 'src')

year = datetime.datetime.now().year

# target locales
target_locales = [
    'de',  # German
    'en',  # English
    'en_GB',  # English (United Kingdom)
    'en_US',  # English (United States)
    'es',  # Spanish
    'fr',  # French
    'it',  # Italian
    'ja',  # Japanese
    'pt',  # Portuguese
    'ru',  # Russian
    'sv',  # Swedish
    'zh',  # Chinese
]


def x_extract():
    """Executes `xgettext extraction` in subprocess."""

    pot_filepath = os.path.join(locale_dir, f'{project_name.lower()}.po')

    commands = [
        'xgettext',
        '--keyword=translate:1,1t',
        '--keyword=translate:1c,2,2t',
        '--keyword=translate:1,2,3t',
        '--keyword=translate:1c,2,3,4t',
        '--keyword=gettext:1',
        '--keyword=pgettext:1c,2',
        '--keyword=ngettext:1,2',
        '--keyword=npgettext:1c,2,3',
        f'--default-domain={project_name.lower()}',
        f'--output={pot_filepath}',
        '--language=C++',
        '--boost',
        '--from-code=utf-8',
        '-F',
        f'--msgid-bugs-address=github.com/{project_owner.lower()}/{project_name.lower()}',
        f'--copyright-holder={project_owner}',
        f'--package-name={project_name}',
        '--package-version=v0'
    ]

    extensions = ['cpp', 'h', 'm', 'mm']

    # find input files
    for root, dirs, files in os.walk(project_dir, topdown=True):
        for name in files:
            filename = os.path.join(root, name)
            extension = filename.rsplit('.', 1)[-1]
            if extension in extensions:  # append input files
                commands.append(filename)

    print(commands)
    subprocess.check_output(args=commands, cwd=root_dir)

    try:
        # fix header
        body = ""
        with open(file=pot_filepath, mode='r') as file:
            for line in file.readlines():
                if line != '"Language: \\n"\n':  # do not include this line
                    if line == '# SOME DESCRIPTIVE TITLE.\n':
                        body += f'# Translations template for {project_name}.\n'
                    elif line.startswith('#') and 'YEAR' in line:
                        body += line.replace('YEAR', str(year))
                    elif line.startswith('#') and 'PACKAGE' in line:
                        body += line.replace('PACKAGE', project_name)
                    else:
                        body += line

        # rewrite pot file with updated header
        with open(file=pot_filepath, mode='w+') as file:
            file.write(body)
    except FileNotFoundError:
        pass


def babel_init(locale_code: str):
    """Executes `pybabel init` in subprocess.

    :param locale_code: str - locale code
    """
    commands = [
        'pybabel',
        'init',
        '-i', os.path.join(locale_dir, f'{project_name.lower()}.po'),
        '-d', locale_dir,
        '-D', project_name.lower(),
        '-l', locale_code
    ]

    print(commands)
    subprocess.check_output(args=commands, cwd=root_dir)


def babel_update():
    """Executes `pybabel update` in subprocess."""
    commands = [
        'pybabel',
        'update',
        '-i', os.path.join(locale_dir, f'{project_name.lower()}.po'),
        '-d', locale_dir,
        '-D', project_name.lower(),
        '--update-header-comment'
    ]

    print(commands)
    subprocess.check_output(args=commands, cwd=root_dir)


def babel_compile():
    """Executes `pybabel compile` in subprocess."""
    commands = [
        'pybabel',
        'compile',
        '-d', locale_dir,
        '-D', project_name.lower()
    ]

    print(commands)
    subprocess.check_output(args=commands, cwd=root_dir)


if __name__ == '__main__':
    # Set up and gather command line arguments
    parser = argparse.ArgumentParser(
        description='Script helps update locale translations. Translations must be done manually.')

    parser.add_argument('--extract', action='store_true', help='Extract messages from c++ files.')
    parser.add_argument('--init', action='store_true', help='Initialize any new locales specified in target locales.')
    parser.add_argument('--update', action='store_true', help='Update existing locales.')
    parser.add_argument('--compile', action='store_true', help='Compile translated locales.')

    args = parser.parse_args()

    if args.extract:
        x_extract()

    if args.init:
        for locale_id in target_locales:
            if not os.path.isdir(os.path.join(locale_dir, locale_id)):
                babel_init(locale_code=locale_id)

    if args.update:
        babel_update()

    if args.compile:
        babel_compile()
