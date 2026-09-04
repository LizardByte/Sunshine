variable "CI_PR" {
  default = false
  description = "Build only the pull-request platform set."
}

group "default" {
  targets = [
    "clion-toolchain",
    "debian-trixie",
    "ubuntu-22-04",
    "ubuntu-24-04",
    "ubuntu-26-04",
  ]
}

# The shared Docker workflow exports /artifacts from these targets and uploads
# the files as workflow artifacts.
group "artifacts" {
  targets = [
    "debian-trixie",
    "ubuntu-22-04",
    "ubuntu-24-04",
    "ubuntu-26-04",
  ]
}

target "_sunshine" {
  context = "."
  dockerfile = "docker/debian.dockerfile"
  no-cache = true
  platforms = CI_PR ? ["linux/amd64"] : [
    "linux/amd64",
    "linux/arm64/v8",
  ]
}

target "clion-toolchain" {
  context = "."
  dockerfile = "docker/clion-toolchain.dockerfile"
  no-cache = true
  platforms = ["linux/amd64"]
}

target "debian-trixie" {
  inherits = ["_sunshine"]
  args = {
    BASE = "debian"
    CUDA_PATCHES = "true"
    TAG = "trixie"
  }
  labels = {
    "dev.lizardbyte.image.variant" = "debian-trixie"
  }
}

target "ubuntu-22-04" {
  inherits = ["_sunshine"]
  args = {
    BASE = "ubuntu"
    TAG = "22.04"
    UBUNTU_TEST_REPO = "true"
  }
  labels = {
    "dev.lizardbyte.image.variant" = "ubuntu-22.04"
  }
}

target "ubuntu-24-04" {
  inherits = ["_sunshine"]
  args = {
    BASE = "ubuntu"
    TAG = "24.04"
  }
  labels = {
    "dev.lizardbyte.image.variant" = "ubuntu-24.04"
  }
}

target "ubuntu-26-04" {
  inherits = ["_sunshine"]
  args = {
    BASE = "ubuntu"
    TAG = "26.04"
  }
  labels = {
    "dev.lizardbyte.image.variant" = "ubuntu-26.04"
  }
}
