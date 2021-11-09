#!/bin/bash
# Copyright 2021 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# Set up /run/chromeos-config during boot (assumes unibuild).
# Note: This is written to target busybox ash and bash, as it needs to
# run in recovery initramfs, where we only have busybox.

set -e

: "${SQUASHFS_IMAGE:=/usr/share/chromeos-config/configfs.img}"
: "${MOUNTPOINT:=/run/chromeos-config}"

eval "$(crosid)"
if [ "${CONFIG_INDEX}" = "unknown" ]; then
    echo 'No device identity matched.  Run "crosid -v" for explanation.' >&2
    exit 1
fi

mkdir -p "${MOUNTPOINT}/private" "${MOUNTPOINT}/v1"
mount -n -oro,nodev,noexec,nosuid "${SQUASHFS_IMAGE}" "${MOUNTPOINT}/private"
mount -n -obind,ro,nodev,noexec,nosuid \
    "${MOUNTPOINT}/private/v1/chromeos/configs/${CONFIG_INDEX}" \
    "${MOUNTPOINT}/v1"
