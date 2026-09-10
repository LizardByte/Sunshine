# CPM Package Lock
# This file should be committed to version control

# The first argument of CPMDeclarePackage can be freely chosen and is used as argument in CPMGetPackage.
# The NAME argument should be package name that would also be used in a find_package call.
# Ideally, both are the same, which might not always be possible: https://github.com/cpm-cmake/CPM.cmake/issues/603
# This is needed to support CPM_USE_LOCAL_PACKAGES

# Renovate-bot will update the versions and hashes in this file when a new version of a dependency is released.
# The comments above each dependency are used by renovate to identify the dependencies and extract the version numbers.
# See https://github.com/LizardByte/.github/blob/master/renovate-config.json5 for the configuration of renovate.
#
# Expected dependency structure for new entries:
# - Start each block with a human-readable comment, for example `# Example dependency`.
# - Follow it with consecutive renovate metadata comments.
# - The first metadata line must start with `# renovate:` and include `datasource=` and `depName=`.
# - Optional metadata keys are `packageName=`, `versioning=`, `extractVersion=`, and `registryUrl=`.
# - Optional metadata may stay on the `# renovate:` line or continue on the next consecutive `#` lines.
# - Keep metadata keys in this order: `datasource`, `depName`, `packageName`, `versioning`,
#   `extractVersion`, `registryUrl`.
# - After metadata, declare the tracked value with `set(NAME_VERSION ...)` or `set(NAME_TAG ...)`.
# - If the dependency also tracks a SHA256, keep `set(NAME_SHA256 ...)` immediately after the
#   matching `NAME_VERSION` or `NAME_TAG` line with no unrelated lines between them.
# - Keep `CPMDeclarePackage(...)` below the tracked values.
#
# Example layout:
# - `# Example dependency`
# - `# renovate: datasource=github-tags depName=owner/repo`
# - `# versioning=regex:^v(?<major>\d+)\.(?<minor>\d+)\.(?<patch>\d+)$`
# - `set(EXAMPLE_TAG v1.2.3)`
# - `set(EXAMPLE_SHA256 <sha256>)`
# - `CPMDeclarePackage(...)`

set(PATCH_DIRECTORY "${CMAKE_CURRENT_LIST_DIR}/patches")
