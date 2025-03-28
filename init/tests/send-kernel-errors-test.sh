#!/bin/bash
# Copyright 2017 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

DISK_TEMP_TEMPLATE=test_send_kernel.XXXXXX
DISK_TEMP=$(mktemp -d --tmpdir "${DISK_TEMP_TEMPLATE}")

. chromeos-send-kernel-errors --test || exit

declare -i id

# Mock input command.
dmesg() {
  cat "tests/test_dmesg_${id}"
}

# Mock output command.
metrics_client() {
  echo "metrics_client: $*"
}

run_test() {
  local name=$1
  local out="${DISK_TEMP}/${name}.out"
  local exp_result
  id=$2
  exp_result="tests/${name}_${id}.golden"
  ${name} > "${out}"
  diff "${out}" "${exp_result}"
  if [[ $? -ne 0 ]]; then
    echo "test_${name} failed"
    exit 1
  fi
}

run_test gather_battery_errors 2

rm -rf "${DISK_TEMP}"
