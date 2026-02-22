# CPM Package Lock
# This file should be committed to version control

# The first argument of CPMDeclarePackage can be freely chosen and is used as argument in CPMGetPackage.
# The NAME argument should be package name that would also be used in a find_package call.
# Ideally, both are the same, which might not always be possible: https://github.com/cpm-cmake/CPM.cmake/issues/603
# This is needed to support CPM_USE_LOCAL_PACKAGES

# TODO: update dependencies with renovate
# https://joht.github.io/johtizen/automation/2022/08/03/keep-your-cpp-dependencies-up-to-date.html

# Boost
set(BOOST_VERSION 1.89.0)
CPMDeclarePackage(Boost
        NAME Boost
        VERSION ${BOOST_VERSION}
        URL https://github.com/boostorg/boost/releases/download/boost-${BOOST_VERSION}/boost-${BOOST_VERSION}-cmake.tar.xz  # cmake-lint: disable=C0301
        URL_HASH SHA256=67acec02d0d118b5de9eb441f5fb707b3a1cdd884be00ca24b9a73c995511f74
        DOWNLOAD_ONLY YES  # boost is a bit complicated so we handle it separately
)
