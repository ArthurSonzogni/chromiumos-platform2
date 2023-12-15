#!/bin/bash
# Copyright 2023 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# Script to initialize Crosier environment.
# This script should run on device/VM.

set -e

# Switch to script directory.
HERE="$(dirname "$(realpath -e "${BASH_SOURCE[0]}")")"
cd "${HERE}"

# Link and copy chrome libs and other files to correct locations
ln -s /opt/google/chrome/* .
ln -s /usr/local/libexec/chrome-binary-tests/libtest_trace_processor.so .
mkdir -p ../../chrome/test/data/chromeos
mv web_handwriting ../../chrome/test/data/chromeos/

# Set permissions and folder owners
chown -R chronos: ../../chrome
chown -R chronos: ..
