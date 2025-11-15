BUILD_CONFIG="release"
QT_514_COMMIT="609d4aaccb503298e7fa9cef45e0ddc4c4afd63c"

fail()
{
	echo "$1" 1>&2
	exit 1
}

if [ "$STEAMLINK_SDK_PATH" == "" ]; then
  fail "You must set STEAMLINK_SDK_PATH to build for Steam Link"
fi

BUILD_ROOT=$PWD/build
SOURCE_ROOT=$PWD
BUILD_FOLDER=$BUILD_ROOT/build-$BUILD_CONFIG
DEPLOY_FOLDER=$BUILD_ROOT/deploy-$BUILD_CONFIG
INSTALLER_FOLDER=$BUILD_ROOT/installer-$BUILD_CONFIG

if [ -n "$CI_VERSION" ]; then
  VERSION=$CI_VERSION
else
  VERSION=`cat $SOURCE_ROOT/app/version.txt`
fi

echo Cleaning output directories
rm -rf $BUILD_FOLDER
rm -rf $DEPLOY_FOLDER
rm -rf $INSTALLER_FOLDER
mkdir $BUILD_ROOT
mkdir $BUILD_FOLDER
mkdir $DEPLOY_FOLDER
mkdir $INSTALLER_FOLDER

echo Initializing Steam Link SDK
source $STEAMLINK_SDK_PATH/setenv.sh || fail "SL SDK initialization failed!"

echo Configuring the project
pushd $BUILD_FOLDER
qmake $SOURCE_ROOT/moonlight-qt.pro QMAKE_CFLAGS_ISYSTEM= || fail "Qmake failed!"
popd

echo Compiling Moonlight in $BUILD_CONFIG configuration
pushd $BUILD_FOLDER
make -j$(nproc) $(echo "$BUILD_CONFIG" | tr '[:upper:]' '[:lower:]') || fail "Make failed!"
popd

echo Creating app bundle
mkdir -p $DEPLOY_FOLDER/steamlink/apps/moonlight/bin
cp $BUILD_FOLDER/app/moonlight $DEPLOY_FOLDER/steamlink/apps/moonlight/bin/ || fail "Binary copy failed!"
cp $SOURCE_ROOT/app/deploy/steamlink/* $DEPLOY_FOLDER/steamlink/apps/moonlight/ || fail "Metadata copy failed!"
pushd $DEPLOY_FOLDER
zip -r $INSTALLER_FOLDER/Moonlight-SteamLink-$VERSION.zip . || fail "Zip failed!"
popd

echo Build completed