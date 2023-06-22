#!/bin/bash
# Copyright 2024 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# Usage: drivefs::is-drive-file <file> && echo yes || echo no
drivefs::is-drive-file() {
  local file="$1"

  # Returns "local-#####" if the file hasn't been uploaded yet,
  # but it will still return something.
  getfattr --only-values -n user.drive.id "${file}" >/dev/null 2>&1
  return $?
}

# Usage: drivefs::wait-file-uploaded [files...]
drivefs::wait-file-uploaded() {
  local file

  for file; do
    if [[ ! -e "${file}" ]]; then
      echo "Error - file '${file}' doesn't exist."
      return 1
    fi
    if ! drivefs::is-drive-file "${file}"; then
      return 0
    fi

    echo -n "Waiting for file '${file}' to be fully committed to DriveFS."
    while [[ "$(getfattr --only-values -n user.drive.uncommitted "${file}")" -eq 1 ]]; do
      echo -n "."
      #echo -n "File ${file} Progress: "
      # Looks like user.drive.progress might be depricated.
      # http://cl/465558162
      #getfattr -n user.drive.progress "${file}"
      sleep 5
    done
    echo "Done"
  done
}

main() {
  local cmd="$1"
  shift

  case "${cmd}" in
  isdrive)
    drivefs::is-drive-file "$@"
    exit $?
    ;;
  wait)
    drivefs::wait-file-uploaded "$@"
    exit $?
    ;;
  *|-h|--help)
    echo "Usage: drivefs-utils.sh <wait> [files...]"
    exit 0
    ;;
  esac
}

main "$@"
