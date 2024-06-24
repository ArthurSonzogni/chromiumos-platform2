#!/bin/bash

# Copyright 2024 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

MODFILE_PATH="/var/lib/misc/pciguard_insecure_mode"

err() {
  logger -p ERR "pciguard-insecuremode: $*"
}

info() {
  logger -p INFO "pciguard-insecuremode: $*"
}

insecure_mode() {
  if [[ ! -f "${MODFILE_PATH}" ]]; then
    if touch "${MODFILE_PATH}"; then
      info "created insecure mode file; reboot needed"
    else
      err "can't create insecure mode file"
      exit 1
    fi
  else
    info "already in insecure mode; reboot may be needed"
  fi

  exit 0
}

secure_mode() {
  if [[ -f "${MODFILE_PATH}" ]]; then
    if rm -f "${MODFILE_PATH}"; then
      info "remove insecure mode file; reboot is needed"
    else
      err "can't remove insecure mode file"
      exit 1
    fi
  else
    info "already in secure mode; reboot may be needed"
  fi

  exit 0
}

usage() {
  cat <<EOF
Usage: $0 [-i|-s|-d|-h]
Disable/Enable PCIGuard on startup.

Arguments :
-i|--insecure-mode : put the device in insecure mode
-s|--secure-mode   : put the device in secure mode
-d|--default       : put the device in secure mode
-h|--help          : show this message and exit
EOF
}

main() {
  while [[ $# -gt 0 ]]; do
    case $1 in
      -i|--insecure-mode)
        insecure_mode
        shift
        ;;
      -s|--secure-mode)
        secure_mode
        shift
        ;;
      -d|--default)
        secure_mode
        shift
        ;;
      -h|--help)
        usage
        shift
        exit 0
        ;;
      *)
        echo "$0: error: Unknown option" >&2
        exit 1
        ;;
    esac
  done
}

main "$@"
