# syntax=docker/dockerfile:1.4
# artifacts: true
# platforms: linux/amd64
# archlinux does not have an arm64 base image
# no-cache-filters: artifacts,sunshine
ARG BASE=archlinux
ARG TAG=base-devel
FROM ${BASE}:${TAG} AS sunshine-base

# install dependencies
RUN <<_DEPS
#!/bin/bash
set -e
pacman -Syu --disable-download-timeout --needed --noconfirm \
  archlinux-keyring
_DEPS

# Setup builder user, arch prevents running makepkg as root
RUN useradd -m builder && \
    echo 'builder ALL=(ALL) NOPASSWD: ALL' >> /etc/sudoers
# makepkg is used in sunshine-build and uploader build stages

FROM sunshine-base as sunshine-build

ARG BRANCH
ARG BUILD_VERSION
ARG COMMIT
ARG CLONE_URL
# note: BUILD_VERSION may be blank

ENV BRANCH=${BRANCH}
ENV BUILD_VERSION=${BUILD_VERSION}
ENV COMMIT=${COMMIT}

SHELL ["/bin/bash", "-o", "pipefail", "-c"]
# install dependencies
# cuda is an optional build-time dependency for PKGBUILD
RUN <<_DEPS
#!/bin/bash
set -e
pacman -Syu --disable-download-timeout --needed --noconfirm \
  base-devel \
  cmake \
  cuda \
  git \
  namcap \
  xorg-server-xvfb
_DEPS

# Setup builder user
USER builder

# copy repository
WORKDIR /build/sunshine/
COPY --link .. .

# setup build directory
WORKDIR /build/sunshine/build

# configure PKGBUILD file
RUN <<_MAKE
#!/bin/bash
set -e
if [[ "${BUILD_VERSION}" == '' ]]; then
  sub_version=".r${COMMIT}"
else
  sub_version=""
fi
cmake \
  -DSUNSHINE_CONFIGURE_PKGBUILD=ON \
  -DSUNSHINE_SUB_VERSION="${sub_version}" \
  -DGITHUB_CLONE_URL="${CLONE_URL}" \
  -DGITHUB_BRANCH=${BRANCH} \
  -DGITHUB_BUILD_VERSION=${BUILD_VERSION} \
  -DGITHUB_COMMIT="${COMMIT}" \
  -DSUNSHINE_CONFIGURE_ONLY=ON \
  /build/sunshine
_MAKE

WORKDIR /build/sunshine/pkg
RUN mv /build/sunshine/build/PKGBUILD .
RUN mv /build/sunshine/build/sunshine.install .

# namcap and build PKGBUILD file
RUN <<_PKGBUILD
#!/bin/bash
set -e
export DISPLAY=:1
Xvfb ${DISPLAY} -screen 0 1024x768x24 &
namcap -i PKGBUILD
makepkg -si --noconfirm
rm -f /build/sunshine/pkg/sunshine-debug*.pkg.tar.zst
ls -a
_PKGBUILD

FROM scratch as artifacts

COPY --link --from=sunshine-build /build/sunshine/pkg/sunshine*.pkg.tar.zst /sunshine.pkg.tar.zst

FROM sunshine-base as sunshine

# copy from uploader instead of artifacts or uploader stage will not run
COPY --link --from=artifacts /sunshine.pkg.tar.zst /

# install sunshine
RUN <<_INSTALL_SUNSHINE
#!/bin/bash
set -e
# update keyring to prevent cached keyring errors
pacman -Syu --disable-download-timeout --needed --noconfirm \
  archlinux-keyring
pacman -U --disable-download-timeout --needed --noconfirm \
  /sunshine.pkg.tar.zst
_INSTALL_SUNSHINE

# network setup
EXPOSE 47984-47990/tcp
EXPOSE 48010
EXPOSE 47998-48000/udp

# setup user
ARG PGID=1000
ENV PGID=${PGID}
ARG PUID=1000
ENV PUID=${PUID}
ENV TZ="UTC"
ARG UNAME=lizard
ENV UNAME=${UNAME}

ENV HOME=/home/$UNAME

# setup user
RUN <<_SETUP_USER
#!/bin/bash
set -e
# first delete the builder user
userdel -r builder

# then create the lizard user
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
