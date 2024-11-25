#!/bin/bash
# Copyright 2024 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
#
# Start the fingerprint study server, study_serve.py, on a host/non-Chromebook
# machine. It uses the mocked ectool, in mock-bin, to simulate finger presses.
#
# Run this inside the chroot, after having installed all dependencies for the
# fingerprint_study package to the host/sdk:
#
# sudo emerge chromeos-base/fingerprint_study

FINGER_COUNT=2
ENROLLMENT_COUNT=20
VERIFICATION_COUNT=15

PICTURE_DIR=./fpstudy-fingers
# If LOG_DIR is left empty, log to console
LOG_DIR=

# Find the fingerprint study base directory.
STUDY_DIR="$(dirname "${BASH_SOURCE[0]}")"

if [[ -n "${LOG_DIR}" ]]; then
  mkdir -p "${LOG_DIR}"
fi

mkdir -p "${PICTURE_DIR}"
echo -e "# This directory is for testing only.\n*" >"${PICTURE_DIR}/.gitignore"

PATH="${STUDY_DIR}/mock-bin:${PATH}"                                           \
  "${STUDY_DIR}/study_serve.py"                                                \
  --finger-count="${FINGER_COUNT}"                                             \
  --enrollment-count="${ENROLLMENT_COUNT}"                                     \
  --verification-count="${VERIFICATION_COUNT}"                                 \
  --picture-dir="${PICTURE_DIR}"                                               \
  --log-dir="${LOG_DIR}"                                                       \
  "$@"
