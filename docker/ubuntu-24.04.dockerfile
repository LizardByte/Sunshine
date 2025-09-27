# syntax=docker/dockerfile:1
# artifacts: true
# platforms: linux/amd64,linux/arm64/v8
# platforms_pr: linux/amd64,linux/arm64/v8
# no-cache-filters: sunshine-base,artifacts,sunshine
ARG BASE=ubuntu
ARG TAG=24.04
FROM ${BASE}:${TAG} AS sunshine-base

ENV DEBIAN_FRONTEND=noninteractive

FROM sunshine-base AS sunshine-deps

ARG BUILDPLATFORM
ARG TARGETPLATFORM

# Force this stage to run on the build platform for cross-compilation setup
FROM --platform=$BUILDPLATFORM ubuntu:24.04 AS sunshine-deps-native

ENV DEBIAN_FRONTEND=noninteractive

SHELL ["/bin/bash", "-o", "pipefail", "-c"]

# Copy only the build script first for better layer caching
WORKDIR /build/sunshine/
COPY --link scripts/linux_build.sh ./scripts/linux_build.sh

# Install dependencies first - this layer will be cached
RUN <<_DEPS
#!/bin/bash
set -e
chmod +x ./scripts/linux_build.sh

# Set up cross-compilation variables if building for different platform
if [ "${BUILDPLATFORM}" != "${TARGETPLATFORM}" ]; then
  cross_compile="--cross-compile"
  case "${TARGETPLATFORM}" in
    linux/amd64)
      target_arch="amd64"
      target_tuple="x86_64-linux-gnu"
      ;;
    linux/arm64)
      target_arch="arm64" 
      target_tuple="aarch64-linux-gnu"
      ;;
    *)
      echo "Unsupported target platform: ${TARGETPLATFORM}"
      exit 1
      ;;
  esac
  
  # Enable multiarch support for cross-compilation
  dpkg --add-architecture ${target_arch}
  apt-get update
  
  echo "Cross-compiling from ${BUILDPLATFORM} to ${TARGETPLATFORM}"
  echo "Target arch: ${target_arch}, Target triple: ${target_tuple}"
else
  cross_compile=""
  target_arch=$(dpkg --print-architecture)
  target_tuple=""
  echo "Native compilation for ${TARGETPLATFORM}"
fi

./scripts/linux_build.sh \
  --step=deps \
  --sudo-off \
  ${cross_compile} \
  ${target_arch:+--target-arch=${target_arch}} \
  ${target_tuple:+--target-tuple=${target_tuple}}
apt-get clean
rm -rf /var/lib/apt/lists/*
_DEPS

FROM --platform=$BUILDPLATFORM sunshine-deps-native AS sunshine-build

ARG BUILDPLATFORM
ARG TARGETPLATFORM
ARG BRANCH
ARG BUILD_VERSION
ARG COMMIT
# note: BUILD_VERSION may be blank

ENV BRANCH=${BRANCH}
ENV BUILD_VERSION=${BUILD_VERSION}
ENV COMMIT=${COMMIT}

# Now copy the full repository
COPY --link .. .

# Configure, validate, build and package
RUN <<_BUILD
#!/bin/bash
set -e

# Set up cross-compilation variables if building for different platform  
if [ "${BUILDPLATFORM}" != "${TARGETPLATFORM}" ]; then
  cross_compile="--cross-compile"
  case "${TARGETPLATFORM}" in
    linux/amd64)
      target_arch="amd64"
      target_tuple="x86_64-linux-gnu"
      ;;
    linux/arm64)
      target_arch="arm64"
      target_tuple="aarch64-linux-gnu"
      ;;
    *)
      echo "Unsupported target platform: ${TARGETPLATFORM}"
      exit 1
      ;;
  esac
  echo "Cross-compiling from ${BUILDPLATFORM} to ${TARGETPLATFORM}"
else
  cross_compile=""
  target_arch=""
  target_tuple=""
  echo "Native compilation for ${TARGETPLATFORM}"
fi

./scripts/linux_build.sh \
  --step=cmake \
  --publisher-name='LizardByte' \
  --publisher-website='https://app.lizardbyte.dev' \
  --publisher-issue-url='https://app.lizardbyte.dev/support' \
  --sudo-off \
  ${cross_compile} \
  ${target_arch:+--target-arch=${target_arch}} \
  ${target_tuple:+--target-tuple=${target_tuple}}

./scripts/linux_build.sh \
  --step=validation \
  --sudo-off \
  ${cross_compile} \
  ${target_arch:+--target-arch=${target_arch}} \
  ${target_tuple:+--target-tuple=${target_tuple}}

./scripts/linux_build.sh \
  --step=build \
  --sudo-off \
  ${cross_compile} \
  ${target_arch:+--target-arch=${target_arch}} \
  ${target_tuple:+--target-tuple=${target_tuple}}

./scripts/linux_build.sh \
  --step=package \
  --sudo-off \
  ${cross_compile} \
  ${target_arch:+--target-arch=${target_arch}} \
  ${target_tuple:+--target-tuple=${target_tuple}}
_BUILD

FROM --platform=$TARGETPLATFORM sunshine-build AS sunshine-test

# This stage runs on the target architecture to avoid qemu overhead for tests
ARG TARGETPLATFORM

WORKDIR /build/sunshine/build/tests

RUN <<_TEST
#!/bin/bash
set -e
export DISPLAY=:1
Xvfb ${DISPLAY} -screen 0 1024x768x24 &
./test_sunshine --gtest_color=yes
_TEST

FROM --platform=$TARGETPLATFORM sunshine-base AS sunshine

ARG BASE
ARG TAG
ARG TARGETARCH

# artifacts to be extracted in CI
COPY --link --from=sunshine-build /build/sunshine/build/cpack_artifacts/Sunshine.deb /artifacts/sunshine-${BASE}-${TAG}-${TARGETARCH}.deb

# copy deb from builder
COPY --link --from=sunshine-build /build/sunshine/build/cpack_artifacts/Sunshine.deb /sunshine.deb

# install sunshine
RUN <<_INSTALL_SUNSHINE
#!/bin/bash
set -e
apt-get update -y
apt-get install -y --no-install-recommends /sunshine.deb
apt-get clean
rm -rf /var/lib/apt/lists/*
_INSTALL_SUNSHINE

# network setup
EXPOSE 47984-47990/tcp
EXPOSE 48010
EXPOSE 47998-48000/udp

# setup user
ARG PGID=1001
ENV PGID=${PGID}
ARG PUID=1001
ENV PUID=${PUID}
ENV TZ="UTC"
ARG UNAME=lizard
ENV UNAME=${UNAME}

ENV HOME=/home/$UNAME

# setup user
RUN <<_SETUP_USER
#!/bin/bash
set -e
groupadd -f -g "${PGID}" "${UNAME}"
useradd -lm -d ${HOME} -s /bin/bash -g "${PGID}" -u "${PUID}" "${UNAME}"
mkdir -p ${HOME}/.config/sunshine
ln -s ${HOME}/.config/sunshine /config
chown -R ${UNAME} ${HOME}
_SETUP_USER

USER ${UNAME}
WORKDIR ${HOME}

# entrypoint
ENTRYPOINT ["/usr/bin/sunshine"]
