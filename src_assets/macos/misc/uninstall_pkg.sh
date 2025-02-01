#!/bin/bash -e

# note: this file was used to remove files when using the pkg/dmg, it is no longer used, but left for reference

set -e

package_name=org.macports.Sunshine

echo "Removing files now..."
FILES=$(pkgutil --files $package_name --only-files)

for file in ${FILES}; do
    file="/$file"
    echo "removing: $file"
    rm -f "$file"
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
