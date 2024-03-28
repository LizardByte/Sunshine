# standard imports
import os
import subprocess

# variables
directories = [
    'src',
    'tests',
    'tools',
    os.path.join('third-party', 'glad'),
    os.path.join('third-party', 'nvfbc'),
]
file_types = [
    'cpp',
    'h',
    'm',
    'mm'
]


def clang_format(file: str):
    print(f'Formatting {file} ...')
    subprocess.run(['clang-format', '-i', file])


def main():
    """
    Main entry point.
    """
    # walk the directories
    for directory in directories:
        for root, dirs, files in os.walk(directory):
            for file in files:
                file_path = os.path.join(root, file)
                if os.path.isfile(file_path) and file.rsplit('.')[-1] in file_types:
                    clang_format(file=file_path)


if __name__ == '__main__':
    main()
