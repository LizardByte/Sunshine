#/bin/bash -e

usage() {
	echo "Usage: $0"
	echo "	-d: Generate a debug build"
	echo "	-p: Generate a debian package"
	echo "	-u: The input device is not a TTY"
	echo "	-n name: Docker container name --> default [sunshine]"
	echo "	-s path/to/sources/sunshine: Use local sources instead of a git repository"
	echo "	-c path/to/cmake/binary/dir: Store cmake output on host OS"
}

# Attempt to turn relative paths into absolute paths
absolute_path() {
	RELATIVE_PATH=$1
	if which realpath >/dev/null 2>/dev/null
	then
		RELATIVE_PATH=$(realpath $RELATIVE_PATH)
	else
		echo "Warning: realpath is not installed on your system, ensure [$1] is absolute"
	fi

	RETURN=$RELATIVE_PATH
}

CMAKE_BUILD_TYPE="-e CMAKE_BUILD_TYPE=Release"
SUNSHINE_PACKAGE_BUILD=OFF
SUNSHINE_GIT_URL=https://github.com/loki-47-6F-64/sunshine.git
CONTAINER_NAME=sunshine

# Docker will fail if ctrl+c is passed through and the input is not a tty
DOCKER_INTERACTIVE=-ti

while getopts ":dpuhc:s:n:" arg; do
	case ${arg} in
		u)
			echo "Input device is not a TTY"
			USERNAME="$USER"
			unset DOCKER_INTERACTIVE
			;;
		d)
			echo "Creating debug build"
			CMAKE_BUILD_TYPE="-e CMAKE_BUILD_TYPE=Debug"
			;;
		p)
			echo "Creating package build"
			SUNSHINE_PACKAGE_BUILD=ON
			SUNSHINE_ASSETS_DIR="-e SUNSHINE_ASSETS_DIR=/etc/sunshine"
			SUNSHINE_EXECUTABLE_PATH="-e SUNSHINE_EXECUTABLE_PATH=/usr/bin/sunshine"
			;;
		s)
			absolute_path "$OPTARG"
			OPTARG="$RETURN"
			echo "Using sources from $OPTARG"
			SUNSHINE_ROOT="-v $OPTARG:/root/sunshine"
			;;
		c)
			[ "$USERNAME" == "" ] && USERNAME=$(logname)

			absolute_path "$OPTARG"
			OPTARG="$RETURN"

			echo "Using $OPTARG as cmake binary dir"
			if [[ ! -d $OPTARG ]]
			then
				echo "cmake binary dir doesn't exist, a new one will be created."
				mkdir -p "$OPTARG"
				[ "$USERNAME" == "$USER"] || chown $USERNAME:$USERNAME "$OPTARG"
			fi

			CMAKE_ROOT="-v $OPTARG:/root/sunshine-build"
			;;
		n)
			echo "Container name: $OPTARG"
			CONTAINER_NAME=$OPTARG
			;;
		h)
			usage
			exit 0
			;;
	esac
done

[ "$USERNAME" = "" ] && USERNAME=$(logname)

BUILD_DIR="$PWD/$CONTAINER_NAME-build"
[ "$SUNSHINE_ASSETS_DIR" = "" ] &&  SUNSHINE_ASSETS_DIR="-e SUNSHINE_ASSETS_DIR=$BUILD_DIR/assets"
[ "$SUNSHINE_EXECUTABLE_PATH" = "" ] && SUNSHINE_EXECUTABLE_PATH="-e SUNSHINE_EXECUTABLE_PATH=$BUILD_DIR/sunshine"

echo "docker run $DOCKER_INTERACTIVE --privileged $SUNSHINE_ROOT $CMAKE_ROOT $SUNSHINE_ASSETS_DIR $SUNSHINE_EXECUTABLE_PATH $CMAKE_BUILD_TYPE --name $CONTAINER_NAME $CONTAINER_NAME"
docker run $DOCKER_INTERACTIVE --privileged $SUNSHINE_ROOT $CMAKE_ROOT $SUNSHINE_ASSETS_DIR $SUNSHINE_EXECUTABLE_PATH $CMAKE_BUILD_TYPE --name $CONTAINER_NAME $CONTAINER_NAME

exit_code=$?

if [ $exit_code -eq 0 ]
then
	mkdir -p $BUILD_DIR
	case $SUNSHINE_PACKAGE_BUILD in
		ON)
			echo "Downloading package to: $BUILD_DIR/$CONTAINER_NAME.deb"
			docker cp $CONTAINER_NAME:/root/sunshine-build/package-deb/sunshine.deb "$BUILD_DIR/$CONTAINER_NAME.deb"
			;;
		*)
			echo "Downloading binary and assets to: $BUILD_DIR"
			docker cp $CONTAINER_NAME:/root/sunshine/assets "$BUILD_DIR"
			docker cp $CONTAINER_NAME:/root/sunshine-build/sunshine "$BUILD_DIR"
			;;
	esac
	echo "chown --recursive $USERNAME:$USERNAME $BUILD_DIR"
	chown --recursive $USERNAME:$USERNAME "$BUILD_DIR"
fi

echo "Removing docker container $CONTAINER_NAME"
docker rm $CONTAINER_NAME
