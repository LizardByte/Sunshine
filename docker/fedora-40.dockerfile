# syntax=docker/dockerfile:1.4
# artifacts: true
# platforms: linux/amd64
# platforms_pr: linux/amd64
# no-cache-filters: sunshine-base,artifacts,sunshine
ARG BASE=fedora
ARG TAG=40
FROM ${BASE}:${TAG} AS sunshine-base

FROM sunshine-base as sunshine-build

ARG BRANCH
ARG BUILD_VERSION
ARG COMMIT
# note: BUILD_VERSION may be blank

ENV BRANCH=${BRANCH}
ENV BUILD_VERSION=${BUILD_VERSION}
ENV COMMIT=${COMMIT}

SHELL ["/bin/bash", "-o", "pipefail", "-c"]

# copy repository
WORKDIR /build/sunshine/
COPY --link .. .

# cmake and cpack
RUN <<_BUILD
#!/bin/bash
set -e
chmod +x ./scripts/linux_build.sh
./scripts/linux_build.sh --sudo-off
dnf clean all
rm -rf /var/cache/yum
_BUILD

# run tests
WORKDIR /build/sunshine/build/tests
# hadolint ignore=SC1091
RUN <<_TEST
#!/bin/bash
set -e
export DISPLAY=:1
Xvfb ${DISPLAY} -screen 0 1024x768x24 &
./test_sunshine --gtest_color=yes
_TEST

FROM scratch AS artifacts
ARG BASE
ARG TAG
ARG TARGETARCH
COPY --link --from=sunshine-build /build/sunshine/build/cpack_artifacts/Sunshine.rpm /sunshine-${BASE}-${TAG}-${TARGETARCH}.rpm

FROM sunshine-base as sunshine

# copy deb from builder
COPY --link --from=artifacts /sunshine*.rpm /sunshine.rpm

# install sunshine
RUN <<_INSTALL_SUNSHINE
#!/bin/bash
set -e
dnf -y update
dnf -y install /sunshine.rpm
dnf clean all
rm -rf /var/cache/yum
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
