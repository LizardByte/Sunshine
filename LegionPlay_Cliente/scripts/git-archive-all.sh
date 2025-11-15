#!/bin/bash -
#
# File:        git-archive-all.sh
#
# Description: A utility script that builds an archive file(s) of all
#              git repositories and submodules in the current path.
#              Useful for creating a single tarfile of a git super-
#              project that contains other submodules.
#
# Examples:    Use git-archive-all.sh to create archive distributions
#              from git repositories. To use, simply do:
#
#                  cd $GIT_DIR; git-archive-all.sh
#
#              where $GIT_DIR is the root of your git superproject.
#
# License:     GPL3+
#
###############################################################################
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 3 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
#
###############################################################################

# DEBUGGING
set -e # Exit on error (i.e., "be strict").
set -C # noclobber

# TRAP SIGNALS
trap 'cleanup' QUIT EXIT

# For security reasons, explicitly set the internal field separator
# to newline, space, tab
OLD_IFS=$IFS
IFS="$(printf '\n \t')"

function cleanup () {
    rm -f $TMPFILE
    rm -f $TMPLIST
    rm -f $TOARCHIVE
    IFS="$OLD_IFS"
}

function usage () {
    echo "Usage is as follows:"
    echo
    echo "$PROGRAM <--version>"
    echo "    Prints the program version number on a line by itself and exits."
    echo
    echo "$PROGRAM <--usage|--help|-?>"
    echo "    Prints this usage output and exits."
    echo
    echo "$PROGRAM [--format <fmt>] [--prefix <path>] [--verbose|-v] [--separate|-s]"
    echo "         [--worktree-attributes] [--tree-ish|-t <tree-ish>] [output_file]"
    echo "    Creates an archive for the entire git superproject, and its submodules"
    echo "    using the passed parameters, described below."
    echo
    echo "    If '--format' is specified, the archive is created with the named"
    echo "    git archiver backend. Obviously, this must be a backend that git archive"
    echo "    understands. The format defaults to 'tar' if not specified."
    echo
    echo "    If '--prefix' is specified, the archive's superproject and all submodules"
    echo "    are created with the <path> prefix named. The default is to not use one."
    echo
    echo "    If '--worktree-attributes' is specified, the individual archive commands will"
    echo "    look for attributes in .gitattributes in the working directory too."
    echo
    echo "    If '--separate' or '-s' is specified, individual archives will be created"
    echo "    for each of the superproject itself and its submodules. The default is to"
    echo "    concatenate individual archives into one larger archive."
    echo
    echo "    If '--tree-ish' is specified, the archive will be created based on whatever"
    echo "    you define the tree-ish to be. Branch names, commit hash, etc. are acceptable."
    echo "    Defaults to HEAD if not specified. See git archive's documentation for more"
    echo "    information on what a tree-ish is."
    echo
    echo "    If 'output_file' is specified, the resulting archive is created as the"
    echo "    file named. This parameter is essentially a path that must be writeable."
    echo "    When combined with '--separate' ('-s') this path must refer to a directory."
    echo "    Without this parameter or when combined with '--separate' the resulting"
    echo "    archive(s) are named with a dot-separated path of the archived directory and"
    echo "    a file extension equal to their format (e.g., 'superdir.submodule1dir.tar')."
    echo
    echo "    The special value '-' (single dash) is treated as STDOUT and, when used, the"
    echo "    --separate option is ignored. Use a double-dash to separate the outfile from"
    echo "    the value of previous options. For example, to write a .zip file to STDOUT:"
    echo
    echo "        ./$PROGRAM --format zip -- -"
    echo
    echo "    If '--verbose' or '-v' is specified, progress will be printed."
}

function version () {
    echo "$PROGRAM version $VERSION"
}

# Internal variables and initializations.
readonly PROGRAM=`basename "$0"`
readonly VERSION=0.3

SEPARATE=0
VERBOSE=0

TARCMD=`command -v gtar || command -v gnutar || command -v tar`
FORMAT=tar
PREFIX=
TREEISH=HEAD
ARCHIVE_OPTS=

# RETURN VALUES/EXIT STATUS CODES
readonly E_BAD_OPTION=254
readonly E_UNKNOWN=255

# Process command-line arguments.
while test $# -gt 0; do
    if [ x"$1" == x"--" ]; then
        # detect argument termination
        shift
        break
    fi
    case $1 in
        --format )
            shift
            FORMAT="$1"
            shift
            ;;

        --prefix )
            shift
            PREFIX="$1"
            shift
            ;;

        --worktree-attributes )
            ARCHIVE_OPTS+=" $1"
            shift
            ;;

        --separate | -s )
            shift
            SEPARATE=1
            ;;

        --tree-ish | -t )
            shift
            TREEISH="$1"
            shift
            ;;

        --version )
            version
            exit
            ;;

        --verbose | -v )
            shift
            VERBOSE=1
            ;;

        -? | --usage | --help )
            usage
            exit
            ;;

        -* )
            echo "Unrecognized option: $1" >&2
            usage
            exit $E_BAD_OPTION
            ;;

        * )
            break
            ;;
    esac
done

OLD_PWD="`pwd`"
TMPDIR=${TMPDIR:-/tmp}
TMPFILE=`mktemp "$TMPDIR/$PROGRAM.XXXXXX"` # Create a place to store our work's progress
TMPLIST=`mktemp "$TMPDIR/$PROGRAM.submodules.XXXXXX"`
TOARCHIVE=`mktemp "$TMPDIR/$PROGRAM.toarchive.XXXXXX"`
OUT_FILE=$OLD_PWD # assume "this directory" without a name change by default

if [ ! -z "$1" ]; then
    OUT_FILE="$1"
    if [ "-" == "$OUT_FILE" ]; then
        SEPARATE=0
    fi
    shift
fi

# Validate parameters; error early, error often.
if [ "-" == "$OUT_FILE" -o $SEPARATE -ne 1 ] && [ "$FORMAT" == "tar" -a `$TARCMD --help | grep -q -- "--concatenate"; echo $?` -ne 0 ]; then
    echo "Your 'tar' does not support the '--concatenate' option, which we need"
    echo "to produce a single tarfile. Either install a compatible tar (such as"
    echo "gnutar), or invoke $PROGRAM with the '--separate' option."
    exit
elif [ $SEPARATE -eq 1 -a ! -d "$OUT_FILE" ]; then
    echo "When creating multiple archives, your destination must be a directory."
    echo "If it's not, you risk being surprised when your files are overwritten."
    exit
elif [ `git config -l | grep -q '^core\.bare=true'; echo $?` -eq 0 ]; then
    echo "$PROGRAM must be run from a git working copy (i.e., not a bare repository)."
    exit
fi

# Create the superproject's git-archive
if [ $VERBOSE -eq 1 ]; then
    echo -n "creating superproject archive..."
fi
rm -f $TMPDIR/$(basename "$(pwd)").$FORMAT
git archive --format=$FORMAT --prefix="$PREFIX" $ARCHIVE_OPTS $TREEISH > $TMPDIR/$(basename "$(pwd)").$FORMAT
if [ $VERBOSE -eq 1 ]; then
    echo "done"
fi
echo $TMPDIR/$(basename "$(pwd)").$FORMAT >| $TMPFILE # clobber on purpose
superfile=`head -n 1 $TMPFILE`

if [ $VERBOSE -eq 1 ]; then
    echo -n "looking for subprojects..."
fi
# find all '.git' dirs, these show us the remaining to-be-archived dirs
# we only want directories that are below the current directory
find . -mindepth 2 -name '.git' -type d -print | sed -e 's/^\.\///' -e 's/\.git$//' >> $TOARCHIVE
# as of version 1.7.8, git places the submodule .git directories under the superprojects .git dir
# the submodules get a .git file that points to their .git dir. we need to find all of these too
find . -mindepth 2 -name '.git' -type f -print | xargs grep -l "gitdir" | sed -e 's/^\.\///' -e 's/\.git$//' >> $TOARCHIVE
if [ $VERBOSE -eq 1 ]; then
    echo "done"
    echo "  found:"
    cat $TOARCHIVE | while read arch
    do
      echo "    $arch"
    done
fi

if [ $VERBOSE -eq 1 ]; then
    echo -n "archiving submodules..."
fi
git submodule >>"$TMPLIST"
while read path; do
    TREEISH=$(grep "^ .*${path%/} " "$TMPLIST" | cut -d ' ' -f 2) # git submodule does not list trailing slashes in $path
    cd "$path"
    rm -f "$TMPDIR"/"$(echo "$path" | sed -e 's/\//./g')"$FORMAT
    git archive --format=$FORMAT --prefix="${PREFIX}$path" $ARCHIVE_OPTS ${TREEISH:-HEAD} > "$TMPDIR"/"$(echo "$path" | sed -e 's/\//./g')"$FORMAT
    if [ $FORMAT == 'zip' ]; then
        # delete the empty directory entry; zipped submodules won't unzip if we don't do this
        zip -d "$(tail -n 1 $TMPFILE)" "${PREFIX}${path%/}" >/dev/null 2>&1 || true # remove trailing '/'
    fi
    echo "$TMPDIR"/"$(echo "$path" | sed -e 's/\//./g')"$FORMAT >> $TMPFILE
    cd "$OLD_PWD"
done < $TOARCHIVE
if [ $VERBOSE -eq 1 ]; then
    echo "done"
fi

if [ $VERBOSE -eq 1 ]; then
    echo -n "concatenating archives into single archive..."
fi
# Concatenate archives into a super-archive.
if [ $SEPARATE -eq 0 -o "-" == "$OUT_FILE" ]; then
    if [ $FORMAT == 'tar.gz' ]; then
        gunzip $superfile
        superfile=${superfile:0: -3} # Remove '.gz'
        sed -e '1d' $TMPFILE | while read file; do
            gunzip $file
            file=${file:0: -3}
            $TARCMD --concatenate -f "$superfile" "$file" && rm -f "$file"
        done
        gzip $superfile
        superfile=$superfile.gz
    elif [ $FORMAT == 'tar' ]; then
        sed -e '1d' $TMPFILE | while read file; do
            $TARCMD --concatenate -f "$superfile" "$file" && rm -f "$file"
        done
    elif [ $FORMAT == 'zip' ]; then
        sed -e '1d' $TMPFILE | while read file; do
            # zip incorrectly stores the full path, so cd and then grow
            cd `dirname "$file"`
            zip -g "$superfile" `basename "$file"` && rm -f "$file"
        done
        cd "$OLD_PWD"
    fi

    echo "$superfile" >| $TMPFILE # clobber on purpose
fi
if [ $VERBOSE -eq 1 ]; then
    echo "done"
fi

if [ $VERBOSE -eq 1 ]; then
    echo -n "moving archive to $OUT_FILE..."
fi
while read file; do
    if [ "-" == "$OUT_FILE" ]; then
        cat "$file" && rm -f "$file"
    else
        mv "$file" "$OUT_FILE"
    fi
done < $TMPFILE
if [ $VERBOSE -eq 1 ]; then
    echo "done"
fi
