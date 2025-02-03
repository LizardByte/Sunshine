# syntax=docker/dockerfile:1
# artifacts: true
# platforms: linux/amd64,linux/arm64/v8
# platforms_pr: linux/amd64,linux/arm64/v8
# no-cache-filters: sunshine-base,artifacts,sunshine
ARG BASE=debian
ARG TAG=bookworm
FROM ${BASE}:${TAG} AS sunshine-base
ENV DEBIAN_FRONTEND=noninteractive

ARG BASE
ARG TAG
FROM --platform=$BUILDPLATFORM ${BASE}:${TAG} AS sunshine-build
ENV DEBIAN_FRONTEND=noninteractive

ARG TARGETPLATFORM
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

if [[ "${BUILDPLATFORM}" != "${TARGETPLATFORM}" ]]; then
  cross_compile="--cross-compile"
else
  cross_compile=""
fi

case "${TARGETPLATFORM}" in
  linux/amd64)
    cc_target_tuple="x86_64-linux-gnu"
    cc_target_arch="amd64"
    ;;
  linux/arm64)
    cc_target_tuple="aarch64-linux-gnu"
    cc_target_arch="arm64"
    ;;
  *)
    echo "unsupported platform: ${TARGETPLATFORM}";
    exit 1
    ;;
esac

chmod +x ./scripts/linux_build.sh
./scripts/linux_build.sh \
  ${cross_compile} \
  --cc-target-tuple="${cc_target_tuple}" \
  --cc-target-arch="${cc_target_arch}" \
  --publisher-name='LizardByte' \
  --publisher-website='https://app.lizardbyte.dev' \
  --publisher-issue-url='https://app.lizardbyte.dev/support' \
  --sudo-off
apt-get clean
rm -rf /var/lib/apt/lists/*
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
COPY --link --from=sunshine-build /build/sunshine/build/cpack_artifacts/Sunshine.deb /sunshine-${BASE}-${TAG}-${TARGETARCH}.deb

FROM sunshine-base AS sunshine

# copy deb from builder
COPY --link --from=artifacts /sunshine*.deb /sunshine.deb

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
