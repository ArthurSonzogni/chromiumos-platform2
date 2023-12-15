#!/bin/bash
# Copyright 2023 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# Script to run Crosier tests.
# This script should run on device/VM.

# Switch to script directory.
HERE="$(dirname "$(realpath -e "${BASH_SOURCE[0]}")")"
cd "${HERE}" || exit

TEST_BIN="chromeos_integration_tests"

export HOME=/usr/local/tmp
export TMPDIR=/usr/local/tmp

# Start SUDO helper server.
TEST_SUDO_HELPER_PATH=$(mktemp)
./test_sudo_helper.py --socket-path="${TEST_SUDO_HELPER_PATH}" &
TEST_SUDO_HELPER_PID=$!

# Create specfile for the test binary
echo "${TEST_BIN}" -- u:object_r:chrome_browser_exec:s0 > "${TEST_BIN}.specfile"
setfiles -F "${TEST_BIN}.specfile" "${TEST_BIN}"

# Run tests
stop ui
dbus-send --system --type=method_call --dest=org.chromium.PowerManager \
  /org/chromium/PowerManager org.chromium.PowerManager.HandleUserActivity \
  int32:0
# shellcheck disable=SC2145
sudo -E -u chronos -- /bin/bash -c "LD_LIBRARY_PATH=./ ./${TEST_BIN} \
  --test-launcher-shard-index=0 --test-launcher-total-shards=1 \
  --test-sudo-helper-socket-path=${TEST_SUDO_HELPER_PATH} \
  --enable-pixel-output-in-tests $@"
status=$?
start ui

# Stop SUDO helper server.
pkill -P "${TEST_SUDO_HELPER_PID}"
kill "${TEST_SUDO_HELPER_PID}"
unlink "${TEST_SUDO_HELPER_PATH}"

exit "${status}"
