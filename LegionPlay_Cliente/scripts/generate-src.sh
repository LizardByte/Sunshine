# Run from root of the Git repo to archive a source tarball

fail()
{
	echo "$1" 1>&2
	exit 1
}

git diff-index --quiet HEAD -- || fail "Source archives must not have unstaged changes!"

BUILD_ROOT=$PWD/build
ARCHIVE_FOLDER=$BUILD_ROOT/source
VERSION=`cat app/version.txt`

echo Cleaning output directories
rm -rf $ARCHIVE_FOLDER
mkdir $BUILD_ROOT
mkdir $ARCHIVE_FOLDER

scripts/git-archive-all.sh --format tar.gz $ARCHIVE_FOLDER/MoonlightSrc-$VERSION.tar.gz || fail "Archive failed"

echo Archive successful