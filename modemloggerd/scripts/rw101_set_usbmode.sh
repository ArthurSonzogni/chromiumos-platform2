#!/bin/sh
# Copyright 2026 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# RW101 needs to be in mode 62 to expose the logging ports, but this is not the
# default mode of the device. This script does nothing if we are already in
# mode 62, and requests mode 62 and waits to verify  otherwise.

check_usb_mode() {
  /usr/bin/mmcli -m any --command="AT+GTUSBMODE?" | grep -q "+GTUSBMODE: 62"
}

# Exit early if we are already in mode 62.
if check_usb_mode; then
  echo "USB mode is already 62. No changes needed."
  exit 0
fi

echo "Attempting to switch RW101 into mode 62 for logging."
if ! /usr/bin/mmcli -m any --command="AT+GTUSBMODE=62"; then
  echo "Error: Failed to send GTUSBMODE=62 command." >&2
  exit 1
fi

echo "GTUSBMODE=62 command sent. Waiting for mode to update..."

elapsed=0
interval=3
timeout=30

# The modem removes itself from the usb bus and gets reprobed, which can take
# several seconds. We check until the modem reports the correct mode
while [ "${elapsed}" -lt "${timeout}" ]; do
  sleep "${interval}"
  elapsed=$((elapsed + interval))
  if check_usb_mode; then
    echo "USB mode changed to 62 after ${elapsed} seconds."
    exit 0
  fi
done

echo "Timeout: USB mode did not change to 62 after ${timeout} seconds." >&2
exit 1
