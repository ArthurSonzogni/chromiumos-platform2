#!/bin/bash
# Copyright 2023 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

set -eux -o pipefail

main() {
  install_deps

  # shellcheck disable=SC2154
  local src_root="${KOKORO_ARTIFACTS_DIR}/git/platform2/vm_tools/reference_vm"
  local result_dir="${src_root}/out"

  mkdir -p "${result_dir}"
  cd "${src_root}"

  timestamp="$(date --utc +%Y%m%d_%H%M%S)"
  image_path="refvm-${timestamp}.img"
  sudo "${src_root}/build.py" \
    --debian-release=bookworm \
    -o "${image_path}"

  zstd -16 "${image_path}"
  sudo rm -f "${image_path}"
}

install_deps() {
  # If additional dependencies are required, please also note them in README.md.
  sudo apt-get update
  sudo DEBIAN_FRONTEND=noninteractive apt-get -q -y install \
    eatmydata fai-setup-storage lvm2 python3-requests python3-yaml
}

main "$@"
