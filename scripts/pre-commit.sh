#!/bin/bash
# (C) 2013-2015 MPC-HC authors
# (C) 2023 Sunshine
#
# This file is part of Sunshine.
#
# Sunshine is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 3 of the License, or
# (at your option) any later version.
#
# Sunshine is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program. If not, see <http://www.gnu.org/licenses/>.

# Applies clang-format before commit, unstaged changes are left unstaged
# To enable the hook, copy this file to .git/hooks/pre-commit (no extension)

# internal variables
clang_format_directories=(src tools)
clang_format_extensions=(h c cpp m mm)

if [[ "$OSTYPE" == 'cygwin' ]]; then
  set -o igncr
fi

in_list() {
  one="$1"
  shift
  for valid in "$@"; do
    [[ "$one" == "$valid" ]] && return 0
  done
  return 1
}

apply_clang_format() {
  return_code=0
  if (( $# > 0 )); then
    clang-format -i "$@"
    return_code=$?
  fi
  return $return_code
}

# loop through all new and modified files
clang_format_files=()
buffer=$(git diff --cached --name-only --diff-filter=ACMR -- "${clang_format_directories[@]}")
[[ -n "$buffer" ]] &&
while read -r file; do
  ext="${file##*.}"

  if in_list "$ext" "${clang_format_extensions[@]}"; then
    clang_format_files=("${clang_format_files[@]}" "$file")
  fi
done <<< "$buffer" # process substitution is not implemented in msys bash

# do the actual work here
exit_code=0
if (( ${#clang_format_files[@]} > 0 )); then
  clang_format_path=$(which clang-format 2>&1)
  if (( $? == 0 )); then
    # stash unstaged, apply format to staged
    stash_ref=$(git stash create) &&
    git checkout -- "${clang_format_files[@]}" &&
    apply_clang_format "${clang_format_files[@]}" &&
    git add "${clang_format_files[@]}" || exit_code=1

    if [[ -n "$stash_ref" ]]; then
      if (( exit_code == 0 )); then
        # restore unstaged and apply format again
        git restore --source="$stash_ref" -- "${clang_format_files[@]}" &&
        apply_clang_format "${clang_format_files[@]}" || exit_code=1
      else
        # just restore unstaged on failure
        git restore --source="$stash_ref" -- "${clang_format_files[@]}"
      fi
    fi
  else
    echo "Error: clang-format was not found"
    exit_code=1
  fi
fi

# if there were problems, exit nonzero
if (( exit_code != 0 )); then
  echo "To ignore this and commit anyway use 'git commit --no-verify'"
fi
exit $exit_code
