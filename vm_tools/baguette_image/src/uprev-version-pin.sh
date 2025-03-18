#!/usr/bin/env bash
# Copyright 2025 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
set -e

cd "$(dirname "$0")"

VERSION_FILE="../../concierge/baguette_version.h"
GS_URL_DIR="gs://cros-containers/baguette/images"
AMD64_VERSION_REGEX='.*baguette_rootfs_amd64_([^\.]*)\.img\.zstd'

# update_pin name value
#
# In VERSION_FILE, updates the C++ constant `name` to the string `value`
update_pin() {
    local name="$1"
    local value="$2"
    echo "Updating ${name} = ${value}"
    # Replace the pattern `$name[] = "FOO"` with the new $value
    sed -i -E \
        "s/(.*${name}\[\] = \")[^\"]*(\".*)/\1${value}\2/" \
        "${VERSION_FILE}"
}

# sha256_of_gs_url gs://foo
#
# Prints the sha256 checksum of the file at the provided url
sha256_of_gs_url() {
    local url="$1"
    gsutil cp "${url}" - | sha256sum | cut -b -64

}

main() {
    echo "Looking for latest image version"
    latest_amd64_url=$( \
        gsutil ls "${GS_URL_DIR}"/baguette_rootfs_amd64_\* \
        | tail -n 1 \
    )
    latest_version=$( \
        echo "${latest_amd64_url}" \
        | sed -E "s/${AMD64_VERSION_REGEX}/\1/" \
    )
    echo "Found version ${latest_version} at ${latest_amd64_url}"

    if grep -q "${latest_version}" "${VERSION_FILE}"; then
        echo "Already at the latest version. Nothing to do."
        exit 0
    fi

    echo "Calculating checksums"
    amd64_sha256="$(sha256_of_gs_url "${latest_amd64_url}")"
    arm64_sha256="$(sha256_of_gs_url "${latest_amd64_url//amd64/arm64}")"

    update_pin "kBaguetteVersion" "${latest_version}"
    update_pin "kBaguetteSHA256X86" "${amd64_sha256}"
    update_pin "kBaguetteSHA256Arm" "${arm64_sha256}"

}

main
