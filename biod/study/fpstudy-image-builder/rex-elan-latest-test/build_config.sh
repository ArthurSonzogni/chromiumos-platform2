#!/bin/bash
# Copyright 2024 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

BOARD=rex
ENABLE_ENCRYPTION=false

# If set, pull in latests changes for the fingerpritn_study packge from the
# specified revison.
# FINGERPRINT_STUDY_CHANGES="cros/main"

# Given to build-image to either buid a base/basic image or a test
# image with all dev tools.
# IMAGE_TYPE=base
IMAGE_TYPE=test
#IMAGE_OPTS=( "--noenable_rootfs_verification" )

BRANCH=main
# BRANCH=stable
# BRANCH=release-R116-15509.B

KEYS=(
    "${out}/chromeos-fpstudy-public-device.gpg"
    "${out}/chromeos-fpstudy-recipients.txt"
)

BUILD_FLAGS+=( --chrome )

# elan: Create ChromeOS image for study
# http://b/323057415

config_apply_src_changes() {
    :
    # Enable new optional encryption
    #cherry_pick src/platform2 cros refs/changes/86/2643986/26

    #WORKON_PKGS+=( chromeos-base/chromeos-firmware-fpmcu )
    # chromeos-base/chromeos-firmware-fpmcu: Switch to fpmcu-firmware-binaries
    # https://crrev.com/i/7085313/4
    #cherry_pick src/private-overlays/chromeos-overlay cros-internal refs/changes/13/7085313/4
    # chromeos-base/chromeos-firmware-fpmcu: Add support for buccaneer
    # https://crrev.com/i/6993271/14
    #cherry_pick src/private-overlays/chromeos-overlay cros-internal refs/changes/71/6993271/14

    # WORKON_PKGS+=( chromeos-base/chromeos-config )
    WORKON_PKGS+=( chromeos-base/chromeos-config-bsp-private )
    # DNS: Force karis to use buccaneer
    # https://crrev.com/i/7108658
    cherry_pick src/project/rex/karis cros-internal refs/changes/58/7108658/4
}
