# Introduction
Sunshine is a Gamestream host for Moonlight

[![AppVeyor Build Status](https://ci.appveyor.com/api/projects/status/cgrtw2g3fq9b0b70/branch/master?svg=true)](https://ci.appveyor.com/project/loki-47-6F-64/sunshine/branch/master)
[![Downloads](https://img.shields.io/github/downloads/Loki-47-6F-64/sunshine/total)](https://github.com/Loki-47-6F-64/sunshine/releases)

You may wish to simply build sunshine from source, without bloating your OS with development files.
These scripts will create a docker images that have the necessary packages. As a result, removing the development files after you're done is a single command away.
These scripts use docker under the hood, as such, they can only be used to compile the Linux version


#### Requirements

```
sudo apt install docker
```

#### instructions

You'll require one of the following Dockerfiles:
* Dockerfile-2004 --> Ubuntu 20.04
* Dockerfile-2104 --> Ubuntu 21.04
* Dockerfile-debian --> Debian Bullseye

Depending on your system, the build-* scripts may need root privilleges

First, the docker container needs to be created:
```
cd scripts
./build-container.sh -f Dockerfile-<name>
```

Then, the sources will be compiled and the debian package generated:
```
./build-sunshine -p -s ..
```
You can run `build-sunshine -p -s ..` again as long as the docker container exists.

```
git pull
./build-sunshine -p -s ..
```

Optionally, the docker container can be removed after you're finished:
```
./build-container.sh -c delete
```

Finally install the resulting package:
```
sudo apt install -f sunshine-build/sunshine.deb
```

