#/bin/bash -e

usage() {
	echo "Usage: $0 [OPTIONS]"
	echo "	-c: command --> default [build]"
	echo "	  | delete  --> Delete the container, Dockerfile isn't mandatory"
	echo "	  | build   --> Build the container, Dockerfile is mandatory"
	echo "	  | compile --> Builds the container, then compiles it. Dockerfile is mandatory"
	echo ""
	echo "  -s: path: The path to the source for compilation"
	echo "	-n: name: Docker container name --> default [sunshine]"
	echo "	  --> all: Build/Compile/Delete all available docker containers"
	echo "	-f: Dockerfile: The name of the docker file"
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

CONTAINER_NAME=sunshine
COMMAND=BUILD

build_container() {
	CONTAINER_NAME=$1
	DOCKER_FILE=$2

	if [ ! -f "$DOCKER_FILE" ]
	then
		echo "Error: $DOCKER_FILE doesn't exist"
		exit 7
	fi

	echo "docker build . -t $CONTAINER_NAME -f $DOCKER_FILE"
	docker build . -t "$CONTAINER_NAME" -f "$DOCKER_FILE"
}

delete() {
	CONTAINER_NAME_UPPER=$(echo "$CONTAINER_NAME" | tr '[:lower:]' '[:upper:]')
	if [ "$CONTAINER_NAME_UPPER" = "ALL" ]
	then
		shopt -s nullglob
		for file in $(find . -maxdepth 1 -iname "Dockerfile-*" -type f)
		do
			CURRENT_CONTAINER="sunshine-$(echo $file | cut -c 14-)"

			if docker inspect "$CURRENT_CONTAINER" > /dev/null 2> /dev/null
			then
				echo "docker rmi $CURRENT_CONTAINER"
				docker rmi "$CURRENT_CONTAINER"
			fi
		done
		shopt -u nullglob #revert nullglob back to it's normal default state
	else
		if docker inspect "$CONTAINER_NAME" > /dev/null 2> /dev/null
		then
			echo "docker rmi $CONTAINER_NAME"
			docker rmi $CONTAINER_NAME
		fi
	fi
}

build() {
	CONTAINER_NAME_UPPER=$(echo "$CONTAINER_NAME" | tr '[:lower:]' '[:upper:]')
	if [ "$CONTAINER_NAME_UPPER" = "ALL" ]
	then
		shopt -s nullglob
		for file in $(find . -maxdepth 1 -iname "Dockerfile-*" -type f)
		do
			CURRENT_CONTAINER="sunshine-$(echo $file | cut -c 14-)"
			build_container "$CURRENT_CONTAINER" "$file"
		done
		shopt -u nullglob #revert nullglob back to it's normal default state
	else
		if [[ -z "$DOCKER_FILE" ]]
		then
			echo "Error: if container name isn't equal to 'all', you need to specify the Dockerfile"
			exit 6
		fi
	
		build_container "$CONTAINER_NAME" "$DOCKER_FILE"
	fi
}

abort() {
	echo "$1"
	exit 10
}

compile() {
	CONTAINER_NAME_UPPER=$(echo "$CONTAINER_NAME" | tr '[:lower:]' '[:upper:]')
	if [ "$CONTAINER_NAME_UPPER" = "ALL" ]
	then
		shopt -s nullglob

		# If any docker container doesn't exist, we cannot compile all of them
		for file in $(find . -maxdepth 1 -iname "Dockerfile-*" -type f)
		do
			CURRENT_CONTAINER="sunshine-$(echo $file | cut -c 14-)"

			# If container doesn't exist --> abort.
			docker inspect "$CURRENT_CONTAINER" > /dev/null 2> /dev/null || abort "Error: container image [$CURRENT_CONTAINER] doesn't exist"
		done

		for file in $(find . -maxdepth 1 -iname "Dockerfile-*" -type f)
		do
			CURRENT_CONTAINER="sunshine-$(echo $file | cut -c 14-)"

			echo "$PWD/build-sunshine.sh -p -n $CURRENT_CONTAINER $SUNSHINE_SOURCES"
			"$PWD/build-sunshine.sh" -p -n "$CURRENT_CONTAINER" $SUNSHINE_SOURCES
		done
		shopt -u nullglob #revert nullglob back to it's normal default state
	else
		# If container exists
		if docker inspect "$CONTAINER_NAME" > /dev/null 2> /dev/null
		then
			echo "$PWD/build-sunshine.sh -p -n $CONTAINER_NAME $SUNSHINE_SOURCES"
			"$PWD/build-sunshine.sh" -p -n "$CONTAINER_NAME" $SUNSHINE_SOURCES
		else
			echo "Error: container image [$CONTAINER_NAME] doesn't exist"
			exit 9
		fi
	fi
}

while getopts ":c:hn:f:s:" arg; do
	case ${arg} in
		s)
			SUNSHINE_SOURCES="-s $OPTARG"
			;;
		c)
			COMMAND=$(echo $OPTARG | tr '[:lower:]' '[:upper:]')
			;;
		n)
			echo "Container name: $OPTARG"
			CONTAINER_NAME="$OPTARG"
			;;
		f)
			echo "Using Dockerfile [$OPTARG]"
			DOCKER_FILE="$OPTARG"
			;;
		h)
			usage
			exit 0
			;;
	esac
done

echo "$0 set to $(echo $COMMAND | tr '[:upper:]' '[:lower:]')"

if [ "$COMMAND" = "BUILD" ]
then
	echo "Start building..."
	delete
	build
	echo "Done."
elif [ "$COMMAND" = "COMPILE" ]
then
	echo "Start compiling..."
	compile
	echo "Done."
elif [ "$COMMAND" = "DELETE" ]
then
	echo "Start deleting..."
	delete
	echo "Done."
else
	echo "Unknown command [$(echo $COMMAND | tr '[:upper:]' '[:lower:]')]"
	exit 4
fi
