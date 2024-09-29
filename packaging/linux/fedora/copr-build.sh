#! /bin/sh -x

# This file is not used directly in this repository. It should be uploaded to Copr whenever the file changes.
# Replicate to each package in "pulls", "beta", and "stable" Copr repositories

set -e
resultdir="${COPR_RESULTDIR}"
git clone "https://github.com/${COPR_OWNER}/${COPR_PACKAGE}.git" --depth 1
cd "${COPR_PACKAGE}"

# optional part; drop if building against 'master' is enough
if test -z "$REVISION"; then
  # the hook_payload file contains webhook JSON payload (copr creates it for
  # us); it is created only if the build is triggered by Custom webhook.
  if test -f "$resultdir"/hook_payload; then
    git clone https://github.com/praiskup/copr-ci-tooling \
      "$resultdir/cct" --depth 1
    export PATH="$resultdir/cct:$PATH"

    echo "---"
    cat "$resultdir"/hook_payload
    echo "---"

    # use jq to get the pr_id from the hook_payload
    PR=$(jq -r '.pr_id' "$resultdir"/hook_payload)
    if [ -z "$PR" ]; then
      BRANCH="master"
    else
      BRANCH="pr/${PR}"
    fi

    copr-travis-checkout "$resultdir"/hook_payload
  fi
else
  git checkout "$REVISION"
fi

# initialize the submodules
git submodule update --init --recursive

# get the tag of this commit IF it has one
TAG=$(git tag --points-at HEAD | head -n1)
if [ -z "$TAG" ]; then
  TAG="0.0.$PR"
fi
TAG=$(echo "$TAG" | sed 's/^v//')  # remove v prefix from the tag
echo "TAG=$TAG"

# get the commit
COMMIT=$(git rev-parse HEAD)
echo "COMMIT=$COMMIT"

# move spec file to the correct location
mv packaging/linux/fedora/*.spec "${resultdir}"

# use sed to replace these values in the spec file
sed -i "s|%global build_version 0|%global build_version ${TAG}|" "${resultdir}"/*.spec
sed -i "s|%global branch 0|%global branch ${BRANCH}|" "${resultdir}"/*.spec
sed -i "s|%global commit 0|%global commit ${COMMIT}|" "${resultdir}"/*.spec

# create a tarball of the source code
tar -czf "${resultdir}/tarball.tar.gz" .
