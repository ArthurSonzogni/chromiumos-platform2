#!/bin/bash

# Copyright 2024 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# Camera diagnostics service currently deploys to "/usr/local" since it's only
# enabled in test image. This script copies those files to "/" and starts
# the service.

# Important: This requires rootfs to be writable.

SERVICE_NAME="cros-camera-diagnostics"

if status "${SERVICE_NAME}" | grep "start/running"; then
    echo "${SERVICE_NAME} is already running!"
    exit 0
fi

EXECUTABLE=/usr/bin/cros_camera_diagnostics_service
UPSTART_SCRIPT="/etc/init/${SERVICE_NAME}.conf"
MINIJAIL_CONFIG=/usr/share/minijail/cros-camera-diagnostics.conf
SECCOMP_POLICY=/usr/share/policy/cros-camera-diagnostics.policy

# Copy the files from /usr/local.
cp "/usr/local${EXECUTABLE}" "${EXECUTABLE}"
cp "/usr/local${UPSTART_SCRIPT}" "${UPSTART_SCRIPT}"
cp "/usr/local${MINIJAIL_CONFIG}" "${MINIJAIL_CONFIG}"
cp "/usr/local${SECCOMP_POLICY}" "${SECCOMP_POLICY}"

# Restore the SELinux policy. Otherwise, we will need to reboot.
restorecon -v "${EXECUTABLE}"

# Finally, start the service.
start "${SERVICE_NAME}"
