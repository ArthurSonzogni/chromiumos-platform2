#!/bin/bash
# Copyright 2026 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# Manual unit test script for update-cros-list.
#
# Tests repository availability checking and fallback behavior across:
#   - Normal milestone upgrade (available repo)
#   - Fallback to (tools_version - 1) when tools_version is unavailable (404)
#   - Retaining existing version if already at fallback milestone
#   - Defaulting to tools_version when neither version is available
#   - No-op when local milestone is already >= tools_version

set -eu -o pipefail

SCRIPT_DIR="$(dirname "$(realpath "$0")")"
TARGET_SCRIPT="${1:-${SCRIPT_DIR}/data/usr/local/bin/update-cros-list}"

if [[ ! -f "${TARGET_SCRIPT}" ]]; then
  echo "Error: Target script not found: ${TARGET_SCRIPT}" >&2
  exit 1
fi

echo "Testing target script: ${TARGET_SCRIPT}"

tmpdir="$(mktemp -d)"
trap 'rm -rf "${tmpdir}"' EXIT

cros_list="${tmpdir}/cros.list"
lsb_release="${tmpdir}/lsb-release"

assert_cros_list_version() {
  local expected="$1"
  local actual
  actual="$(grep -o -P 'cros-packages(-staging)?/\K\d+' "${cros_list}")"
  if [[ "${actual}" != "${expected}" ]]; then
    echo "FAIL: Expected milestone ${expected}, but got ${actual}" >&2
    exit 1
  fi
}

set_cros_list() {
  local version="$1"
  local base="https://storage.googleapis.com/cros-packages-staging"
  echo "deb [signed-by=/usr/share/keyrings/cros.gpg] ${base}/${version}/" \
    "trixie main" > "${cros_list}"
}

run_update() {
  SOURCES_PATH="${cros_list}" \
  VM_TOOLS_LSB_RELEASE="${lsb_release}" \
  bash "${TARGET_SCRIPT}"
}

# Case 1: Normal upgrade when target milestone repository exists.
echo "=== Test 1: Normal upgrade (156 -> 157, 157 is available) ==="
set_cros_list "156"
echo "CHROMEOS_RELEASE_CHROME_MILESTONE=157" > "${lsb_release}"
run_update
assert_cros_list_version "157"
echo "PASS: Successfully upgraded to 157"

# Case 2: Fallback to (tools_version - 1) when tools_version is unavailable.
# 158 is not published yet, 157 is available.
echo "=== Test 2: Fallback when tools_version is unavailable (157 -> 158) ==="
set_cros_list "157"
echo "CHROMEOS_RELEASE_CHROME_MILESTONE=158" > "${lsb_release}"
run_update
assert_cros_list_version "157"
echo "PASS: Kept fallback milestone 157"

# Case 3: Upgrade from an older image version to the fallback milestone.
# Image has 153, host has 158 (404), fallback is 157.
echo "=== Test 3: Upgrade from older image to fallback (153 -> 158) ==="
set_cros_list "153"
echo "CHROMEOS_RELEASE_CHROME_MILESTONE=158" > "${lsb_release}"
run_update
assert_cros_list_version "157"
echo "PASS: Upgraded from 153 to fallback milestone 157"

# Case 4: Defaulting to tools_version when neither version is available.
# 999 and 998 are both 404.
echo "=== Test 4: Defaulting when neither version is available (156 -> 999) ==="
set_cros_list "156"
echo "CHROMEOS_RELEASE_CHROME_MILESTONE=999" > "${lsb_release}"
run_update
assert_cros_list_version "999"
echo "PASS: Defaulted to tools_version 999"

# Case 5: Already up-to-date (no-op).
echo "=== Test 5: Already up-to-date (157 -> 157) ==="
set_cros_list "157"
echo "CHROMEOS_RELEASE_CHROME_MILESTONE=157" > "${lsb_release}"
run_update
assert_cros_list_version "157"
echo "PASS: Kept milestone 157 without modification"

echo ""
echo "All 5 tests passed successfully!"
