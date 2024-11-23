#!/bin/bash
# Copyright 2020 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
#
# Start the fingerprint study server, study_serve.py, on a host/non-Chromebook
# machine. It uses the mocked ectool, in mock-bin, to simulate finger presses.
#
# This can be run or sourced, which is why we don't choose to exec the final
# launch line.

FINGER_COUNT=2
ENROLLMENT_COUNT=20
VERIFICATION_COUNT=15

PICTURE_DIR=./fpstudy-fingers
# If LOG_DIR is left empty, log to console
LOG_DIR=

# Find the fingerprint study base directory.
STUDY_DIR="$(dirname "${BASH_SOURCE[0]}")"
FPSTUDY_VIRTENV="${STUDY_DIR}/.venv"

# Setup New Virtualenv
rm -rf "${FPSTUDY_VIRTENV}"
if ! "${STUDY_DIR}/python-venv-setup.sh" "${FPSTUDY_VIRTENV}"; then
  echo "Error - Failed to setup a python virtualenv." >&2
  exit 1
fi

if [[ -n "${LOG_DIR}" ]]; then
  mkdir -p "${LOG_DIR}"
fi

mkdir -p "${PICTURE_DIR}"
echo -e "# This directory is for testing only.\n*" >"${PICTURE_DIR}/.gitignore"

PATH="${STUDY_DIR}/mock-bin:${PATH}" "${FPSTUDY_VIRTENV}/bin/python3"          \
  "${STUDY_DIR}/study_serve.py"                                                \
  --finger-count="${FINGER_COUNT}"                                             \
  --enrollment-count="${ENROLLMENT_COUNT}"                                     \
  --verification-count="${VERIFICATION_COUNT}"                                 \
  --picture-dir="${PICTURE_DIR}"                                               \
  --log-dir="${LOG_DIR}"                                                       \
  "$@"
