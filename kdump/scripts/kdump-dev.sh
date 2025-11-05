#!/bin/sh
# Copyright 2023 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

CRASH_DUMP_DIR="/var/spool/kdump"
MOUNT_POINT="$(mktemp -d)"
STAGING_DIR="/usr/local/kdump"

exec 1>/dev/kmsg

# mount_kdump_lv mounts the LVM kdump volume if it exists.
# It returns 0 on success, 1 on failure (e.g., no LVM, no volume).
mount_kdump_lv() {
  local vg_name
  vg_name="$(pvdisplay | grep 'VG Name' | awk '{print $3}')"
  if [ -z "${vg_name}" ]; then
    return 1
  fi

  local kdump_lv_path="/dev/${vg_name}/kdump"
  if ! lvdisplay "${kdump_lv_path}" >/dev/null 2>&1; then
    return 1
  fi

  lvchange -ay "${kdump_lv_path}"
  mount -n -t ext4 "${kdump_lv_path}" "${MOUNT_POINT}"
}

# collect_and_move_dumps collects crash dump files from a source directory.
collect_and_move_dumps() {
  local source_dir="$1"

  local found_files
  found_files="$(find "${source_dir}" -maxdepth 1 -type f)"
  if [ -z "${found_files}" ]; then
    return
  fi

  echo "Kdump: collecting crash dumps."
  mkdir -p "${CRASH_DUMP_DIR}"
  echo "${found_files}" | xargs -r mv -v -t "${CRASH_DUMP_DIR}"
  return
}

main() {
  if [ $# -ne 0 ]; then
    echo "Kdump: $0 doesn't take any arguments"
    exit 1
  fi

  if mount_kdump_lv; then
    collect_and_move_dumps "${MOUNT_POINT}"
    sync
    umount "${MOUNT_POINT}"
    rmdir "${MOUNT_POINT}"
  else
    collect_and_move_dumps "${STAGING_DIR}"
    rm -rf "${STAGING_DIR}"
  fi
}

main "$@"
