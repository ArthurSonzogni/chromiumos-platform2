#!/bin/bash
# Copyright 2024 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

BOARD=brya
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
BRANCH=release-R116-15509.B

KEYS=(
    "${out}/chromeos-fpstudy-public-device.gpg"
    "${out}/chromeos-fpstudy-recipients.txt"
)

config_apply_src_changes() {
    :
    WORKON_PKGS+=( chromeos-base/chromeos-init )
    # Blame Firmware Updaters
    cherry_pick src/platform2 cros refs/changes/80/4706580/6
    # Disable AX211 Updates - see b/292164679 and b/291935222
    # cherry_pick src/platform2 cros refs/changes/32/4706132/3
    #WORKON_PKGS+=( net-wireless/ax211-updater )
    cherry_pick src/third_party/chromiumos-overlay cros refs/changes/74/4705974/3

    # Enable multi fp-board parameters
    WORKON_PKGS+=( chromeos-base/fingerprint_study )
    cherry_pick src/platform2 cros refs/changes/79/4729779/5
    cherry_pick src/platform2 cros refs/changes/31/4730631/6
    cherry_pick src/third_party/chromiumos-overlay cros refs/changes/08/4730508/2
    # Enable new optional encryption
    cherry_pick src/platform2 cros refs/changes/86/2643986/26
}
