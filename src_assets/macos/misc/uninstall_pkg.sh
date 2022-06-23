#!/bin/bash -e
set -e

FILES=(pkgutil --files org.macports.Sunshine)

remove_config=True
remove_apps=True

for n in {1..2}; do
    echo "Loop: $n"
    for file in "${FILES[@]}"; do
        if [[ $file == *sunshine.conf ]]; then
            if [ $remove_config == True ]; then
                while true; do
                    read -p -r "Do you wish to remove 'sunshine.conf'?" yn
                    case $yn in
                        [Yy]* ) rm --force "$file"; break;;
                        [Nn]* ) remove_config=False; break;;
                        * ) echo "Please answer yes or no.";;
                    esac
                done
            fi
        fi
        if [[ $file == *apps.json ]]; then
            if [ $remove_apps == True ]; then
                while true; do
                    read -p -r "Do you wish to remove 'apps.conf'?" yn
                    case $yn in
                        [Yy]* ) rm --force "$file"; break;;
                        [Nn]* ) remove_apps=False; break;;
                        * ) echo "Please answer yes or no.";;
                    esac
                done
            fi
        fi

        rm --force --dir "$file"
    done
done
