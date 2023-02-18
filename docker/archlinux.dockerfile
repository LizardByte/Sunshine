# syntax=docker/dockerfile:1.4
# artifacts: true
# platforms: linux/amd64
# archlinux does not have an arm64 base image
ARG BASE=archlinux
ARG TAG=base-devel
FROM ${BASE}:${TAG} AS sunshine-base

# install dependencies
RUN <<_DEPS
#!/bin/bash
set -e
pacman -Syu --noconfirm \
  archlinux-keyring \
  git
_DEPS

# Setup builder user, arch prevents running makepkg as root
RUN useradd -m builder && \
    echo 'builder ALL=(ALL) NOPASSWD: ALL' >> /etc/sudoers
WORKDIR /home/builder
USER builder

# install paru
WORKDIR /tmp
RUN git clone https://aur.archlinux.org/paru.git
WORKDIR /tmp/paru
RUN makepkg -si --noconfirm

# install optional dependencies
RUN paru -Syu --noconfirm \
  cuda \
  libcap \
  libdrm

# switch back to root user, hadolint will complain if last user is root
# hadolint ignore=DL3002
USER root

FROM sunshine-base as sunshine-build

ARG BUILD_VERSION
ARG COMMIT
ARG CLONE_URL
# note: BUILD_VERSION may be blank

SHELL ["/bin/bash", "-o", "pipefail", "-c"]
# install dependencies
RUN <<_DEPS
#!/bin/bash
set -e
pacman -Syu --noconfirm \
  base-devel \
  cmake \
  namcap
_DEPS

# Setup builder user
USER builder

# copy repository
WORKDIR /build/sunshine/
COPY .. .

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
  -DSUNSHINE_CONFIGURE_AUR=ON \
  -DSUNSHINE_SUB_VERSION="${sub_version}" \
  -DGITHUB_CLONE_URL="${CLONE_URL}" \
  -DGITHUB_COMMIT="${COMMIT}" \
  -DSUNSHINE_CONFIGURE_ONLY=ON \
  /build/sunshine
_MAKE

WORKDIR /build/sunshine/pkg
RUN mv /build/sunshine/build/PKGBUILD .

# namcap and build PKGBUILD file
RUN <<_PKGBUILD
#!/bin/bash
set -e
namcap -i PKGBUILD
makepkg -si --noconfirm
ls -a
_PKGBUILD

FROM scratch as artifacts

COPY --from=sunshine-build /build/sunshine/pkg/PKGBUILD /PKGBUILD
COPY --from=sunshine-build /build/sunshine/pkg/sunshine*.pkg.tar.zst /sunshine.pkg.tar.zst

FROM sunshine-base as uploader

# most of this stage is borrowed from
# https://github.com/KSXGitHub/github-actions-deploy-aur/blob/master/build.sh

ARG BUILD_VERSION
ARG RELEASE
ARG TARGETPLATFORM

# Setup builder user
WORKDIR /home/builder
USER builder

# hadolint ignore=SC3010
RUN <<_SSH_CONFIG
#!/bin/bash
set -e
if [[ "${TARGETPLATFORM}" == 'linux/amd64' ]]; then
  echo "Host aur.archlinux.org"; echo "  IdentityFile ~/.ssh/aur"; echo "  User aur" >>~/.ssh/config
fi
_SSH_CONFIG

# create and apply secrets, hadolint is giving a false positive
# hadolint ignore=SC1133
RUN --mount=type=secret,id=AUR_EMAIL,target=/secrets/AUR_EMAIL \
    --mount=type=secret,id=AUR_SSH_PRIVATE_KEY,target=/secrets/AUR_SSH_PRIVATE_KEY \
    --mount=type=secret,id=AUR_USERNAME,target=/secrets/AUR_USERNAME && \
    cat /secrets/AUR_SSH_PRIVATE_KEY >~/.ssh/aur && \
    git config --global user.name "$(cat /secrets/AUR_USERNAME)" && \
    git config --global user.email "$(cat /secrets/AUR_EMAIL)"

WORKDIR /tmp

# hadolint ignore=SC3010
RUN <<_AUR_SETUP
#!/bin/bash
set -e

if [[ "${TARGETPLATFORM}" == 'linux/amd64' ]]; then
  # Adding aur.archlinux.org to known hosts
  ssh_keyscan_types="rsa,dsa,ecdsa,ed25519"
  ssh-keyscan -v -t "$ssh_keyscan_types" aur.archlinux.org >>~/.ssh/known_hosts

  # Importing private key
  chmod -vR 600 ~/.ssh/aur*
  ssh-keygen -vy -f ~/.ssh/aur >~/.ssh/aur.pub

  # Clone AUR package
  mkdir -p /tmp/local-repo
  git clone -v "https://aur.archlinux.org/sunshine.git" /tmp/local-repo

  # Copy built package
  COPY --from=artifacts /PRKBUILD /tmp/local-repo/
fi
_AUR_SETUP

WORKDIR /tmp/local-repo
# aur upload if release event
# hadolint ignore=SC3010
RUN <<_AUR_UPLOAD
#!/bin/bash
set -e
if [[ "${RELEASE}" == "true" && "${TARGETPLATFORM}" == 'linux/amd64' ]]; then
  # update package checksums
  updpkgsums

  # generate srcinfo
  makepkg --printsrcinfo >.SRCINFO

  # commit changes
  git add --all

  # check if there are any changes and commit/push
  if [[ $(git diff-index --quiet HEAD) != "" ]]; then
    git commit -m "${BUILD_VERSION}"
    git remote add aur "https://aur.archlinux.org/sunshine.git"
    git push -v aur master
  fi
fi
_AUR_UPLOAD

# remove secrets
RUN rm -rf /secrets

FROM sunshine-base as sunshine

COPY --from=artifacts /sunshine*.pkg.tar.zst /sunshine.pkg.tar.zst

# install sunshine
RUN <<_INSTALL_SUNSHINE
#!/bin/bash
set -e
pacman -U --noconfirm \
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
# first delete the builder
userdel -r builder

# then create the lizard user
groupadd -f -g "${PGID}" "${UNAME}"
useradd -lm -d ${HOME} -s /bin/bash -g "${PGID}" -G input -u "${PUID}" "${UNAME}"
mkdir -p ${HOME}/.config/sunshine
ln -s ${HOME}/.config/sunshine /config
chown -R ${UNAME} ${HOME}
_SETUP_USER

USER ${UNAME}
WORKDIR ${HOME}

# entrypoint
ENTRYPOINT ["/usr/bin/sunshine"]
