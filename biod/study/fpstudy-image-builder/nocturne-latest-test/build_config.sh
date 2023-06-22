#!/bin/bash
# Copyright 2024 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

BOARD=nocturne
ENABLE_ENCRYPTION=false

# If set, pull in latests changes for the fingerpritn_study packge from the
# specified revison.
# FINGERPRINT_STUDY_CHANGES="cros/main"

# Given to build-image to either buid a base/basic image or a test
# image with all dev tools.
# IMAGE_TYPE=base
IMAGE_TYPE=test
IMAGE_OPTS="--noenable_rootfs_verification"

# BRANCH=stable
BRANCH=main
# BRANCH=release-R116-15509.B

KEYS=(
    "${out}/chromeos-fpstudy-public-device.gpg"
    "${out}/chromeos-fpstudy-recipients.txt"
)


config_apply_src_changes() {
    :
    # cherry_pick src/third_party/chromiumos-overlay cros refs/changes/05/4740205/2
}
