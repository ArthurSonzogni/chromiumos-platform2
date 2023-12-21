#!/bin/bash
# Copyright 2023 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# Script to initialize Crosier environment.
# This script runs on device/VM.
#
# The directory structure after initialization should be:
#   /usr/local/libexec/crosier/... - Crosier testing directory
#     -> chromeos_integration_tests - test binary
#     -> test_sudo_helper* - sudo helper script
#     -> symlink to all Chrome libs from /opt/google/chrome/*
#     -> symlink to libtest_trace_processor.so
#     -> test_fonts
#   /usr/local/chrome/test/data/chromeos/web_handwriting - handwriting libs

set -e

# Switch to script directory.
HERE="$(dirname "$(realpath -e "${BASH_SOURCE[0]}")")"
cd "${HERE}"

# Uncompress the test binary if it's still compressed.
COMPRESSED_NAME="chromeos_integration_tests.tar.bz2"
if [[ -e "${COMPRESSED_NAME}" ]]; then
  tar xfj "${COMPRESSED_NAME}"
  rm "${COMPRESSED_NAME}"
fi

# Link and copy chrome libs and other files to correct locations.
if [[ ! -L "./chrome" ]]; then
  ln -s /opt/google/chrome/* .
  ln -s /usr/local/libexec/chrome-binary-tests/libtest_trace_processor.so .
fi

if [[ -d ./web_handwriting ]]; then
  mkdir -p ../../chrome/test/data/chromeos
  mv web_handwriting ../../chrome/test/data/chromeos/
fi

# Set permissions and folder owners.
chown -R chronos: ../../chrome
chown -R chronos: ..
