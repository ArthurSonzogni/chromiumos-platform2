#!/bin/sh
# Copyright 2023 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

CRASH_DUMP_DIR=/var/spool/kdump
MOUNT_POINT="$(mktemp -d)"

exec 1>/dev/kmsg

mount_kdump_dir() {
  local vg_name kdump_partition
  vg_name="$(pvdisplay | grep 'VG Name' | awk '{print $3}')"
  kdump_partition="/dev/${vg_name}/kdump"

  if ! lvdisplay "${kdump_partition}" >/dev/null; then
    echo "Kdump: no kdump partition"
    exit 0
  fi
  lvchange -ay "${kdump_partition}"

  mkdir -p "${MOUNT_POINT}"
  mount -n -t ext4 "${kdump_partition}" "${MOUNT_POINT}"
}

umount_kdump_dir() {
  sync
  umount "${MOUNT_POINT}"
  rmdir "${MOUNT_POINT}"
}

collect_crash_dump() {
  mount_kdump_dir

  mkdir -p "${CRASH_DUMP_DIR}"
  if [ -n "$(find "${MOUNT_POINT}" -type f)" ]; then
    echo "Kdump: collecting crash dumps:"

    find "${MOUNT_POINT}" -type f -exec mv -v {} "${CRASH_DUMP_DIR}" \;
  fi

  umount_kdump_dir
}

main() {
  if [ $# -ne 0 ]; then
    echo "Kdump: $0 doesn't take any arguments"
    exit 1
  fi

  collect_crash_dump
}

main "$@"
