#!/bin/bash
# Copyright 2024 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# ENABLE_ENCRYPTION=true
ENABLE_ENCRYPTION=false
BOARD=hatch

# If set, pull in latests changes for the fingerpritn_study packge from the
# specified revison.
# FINGERPRINT_STUDY_CHANGES="cros/main"

# Given to build-image to either buid a base/basic image or a test
# image with all dev tools.
# IMAGE_TYPE=base
IMAGE_TYPE=test
IMAGE_OPTS="--noenable_rootfs_verification"

# R89 has enough changes to fingerprint study related thing for this to work.
# BRANCH=main
BRANCH=stable
# BRANCH=release-R116-15509.B
# BRANCH=release-R115-15474.B
# BRANCH=release-R114-15437.B
# BRANCH=release-R90-13816.B
# BRANCH=release-R89-13729.B
# BRANCH=release-R88-13597.B
# BRANCH=release-R87-13505.B

KEYS=(
    "${out}/chromeos-fpstudy-public-device.gpg"
    "${out}/chromeos-fpstudy-recipients.txt"
)


config_apply_src_changes() {
    # Enable flash_fp_mcu on base image
    #cherry_pick $dir cros refs/changes/06/2661206/1

    # Fix flash_fp_mcu stm32mon dependency
    #cherry_pick $dir cros refs/changes/10/2678810/3
    #WORKON_PKGS+=( ec-utils-test )

    # Grab Elan binary
    #cherry_pick src/platform/ec/private/fingerprint/elan cros-internal refs/changes/87/3571687/1
    # Setup chromeos-firmware-fpmcu to pull in Elan bloonchipper binary
    #cherry_pick src/private-overlays/chromeos-overlay cros-internal refs/changes/89/3571689/2
    #WORKON_PKGS+=( chromeos-base/chromeos-firmware-fpmcu )`
    true
}