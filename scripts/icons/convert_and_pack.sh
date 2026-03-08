#!/bin/bash

if ! [ -x "$(command -v ./go-png2ico)" ]; then
    echo "./go-png2ico not found"
    echo "download the executable from https://github.com/J-Siu/go-png2ico"
    echo "and drop it in this folder"
    exit 1
fi

if ! [ -x "$(command -v ./oxipng)" ]; then
    echo "./oxipng executable not found"
    echo "download the executable from https://github.com/shssoichiro/oxipng"
    echo "and drop it in this folder"
    exit 1
fi

if ! [ -x "$(command -v inkscape)" ]; then
    echo "inkscape executable not found"
    exit 1
fi

icon_base_sizes=(16 64)
icon_sizes_keys=() # associative array to prevent duplicates
icon_sizes_keys[256]=1

for icon_base_size in "${icon_base_sizes[@]}"; do
    # increment in 25% till 400%
    icon_size_increment=$((icon_base_size / 4))
    for ((i = 0; i <= 12; i++)); do
        icon_sizes_keys[icon_base_size + i * icon_size_increment]=1
    done
done

# convert to normal array
icon_sizes=("${!icon_sizes_keys[@]}")

echo "using icon sizes:"
# shellcheck disable=SC2068  # intentionally word split
echo ${icon_sizes[@]}

src_vectors=("../../src_assets/common/assets/web/public/images/sunshine-locked.svg"
             "../../src_assets/common/assets/web/public/images/sunshine-pausing.svg"
             "../../src_assets/common/assets/web/public/images/sunshine-playing.svg"
             "../../sunshine.svg")

echo "using sources vectors:"
# shellcheck disable=SC2068  # intentionally word split
echo ${src_vectors[@]}

for src_vector in "${src_vectors[@]}"; do
    file_name=$(basename "${src_vector}" .svg)
    png_files=()
    for icon_size in "${icon_sizes[@]}"; do
        png_file="${file_name}${icon_size}.png"
        echo "converting ${png_file}"
        inkscape -w "${icon_size}" -h "${icon_size}" "${src_vector}" --export-filename "${png_file}" &&
        ./oxipng -o max --strip safe --alpha "${png_file}" &&
        png_files+=("${png_file}")
    done

    echo "packing ${file_name}.ico"
    ./go-png2ico "${png_files[@]}" "${file_name}.ico"
done
