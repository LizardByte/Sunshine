#!/bin/bash -e
set -e

package_name=org.macports.Sunshine

echo "Removing files now..."
FILES=$(pkgutil --files $package_name --only-files)

remove_config=True
remove_apps=True

for file in ${FILES}; do
    file="/$file"
    remove_current=True
    if [[ $file == *sunshine.conf ]]; then
        if [[ $remove_config == True ]]; then
            while true; do
                read -p -r "Do you wish to remove 'sunshine.conf'?" yn
                case $yn in
                    [Yy]* ) echo "removing: $file"; rm -f "$file"; break;;
                    [Nn]* ) remove_config=False; remove_current=False; break;;
                    * ) echo "Please answer yes or no.";;
                esac
            done
        fi
    fi
    if [[ $file == *apps.json ]]; then
        if [[ $remove_apps == True ]]; then
            while true; do
                read -p -r "Do you wish to remove 'apps.conf'?" yn
                case $yn in
                    [Yy]* ) echo "removing: $file"; rm -f "$file"; break;;
                    [Nn]* ) remove_apps=False; remove_current=False; break;;
                    * ) echo "Please answer yes or no.";;
                esac
            done
        fi
    fi

    if [[ $remove_current == True ]]; then
        echo "removing: $file"
        rm -f "$file"
    fi
done

echo "Removing directories now..."
DIRECTORIES=$(pkgutil --files org.macports.Sunshine --only-dirs)

for dir in ${DIRECTORIES}; do
    dir="/$dir"
    echo "Checking if empty directory: $dir"

    # check if directory is empty... could just use ${DIRECTORIES} here if pkgutils added the `/` prefix
    empty_dir=$(find "$dir" -depth 0 -type d -empty)

    # remove the directory if it is empty
    if [[ $empty_dir != "" ]]; then  # prevent the loop from running and failing if no directories found
        for i in "${empty_dir}"; do  # don't split words as we already know this will be a single directory
            echo "Removing empty directory: ${i}"
            rmdir "${i}"
        done
    fi
done

echo "Forgetting Sunshine..."
pkgutil --forget $package_name

echo "Sunshine has been uninstalled..."
