#!/bin/bash

# Copyright 2024 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

DEBUG_FS_PATH="/sys/kernel/debug"
TCPDUMP_CAPTURE_PATH="/tmp/"
USB_UTILS_LOG_PATH="/tmp/usb_utils.log"
RED='\033[0;31m'
GREEN='\033[0;32m'
NC='\033[0m' # No Color

declare -a dd_modules=("*8152*" "*8169*" "*rndis*" "*cdc*" "*igc*" "*igb*" \
  "*atlantic*" "*net*" "*core*" "*bus*" "*device*" "*hci*" "*usb*")

info_red() {
  echo -e "${RED}$*${NC}"
}

info_green() {
  echo -e "${GREEN}$*${NC}"
}

usage() {
  cat <<EOF
Usage: $0 [-i|-s|-d|-h]
Disable/Enable extra USB debug information. The extra information will be
present in ${TCPDUMP_CAPTURE_PATH} and /var/log/messages.

Arguments :
-e|--enable    : enable extra USB debug information.
-d|--disable   : cleanup the unwanted files created for debug.
-h|--help      : show this message and exit.
EOF
}

enable_usb_debug() {

  info_red "!!!!!!!!!!! THIS SCRIPT CAN GENERATE A LOT OF LOGS !!!!!!!!!!!"
  info_red "Please collect all /var/log/messages files, \
   including the archived ones "

  mount -t debugfs none_debugs "${DEBUG_FS_PATH}"

  modprobe usbmon

  usbmon_interfaces=$(tcpdump --list-interfaces | \
    grep -i usbmon | cut -d '.' -f 2 | cut -d ' ' -f 1)

  # Iterate over each usbmon interface
  while IFS= read -r interface; do
    info_green "Start USB dump for: ${interface}, \
      file will be ${TCPDUMP_CAPTURE_PATH}${interface}.pcap"

    tcpdump -i "${interface}" -s0 -w \
    "${TCPDUMP_CAPTURE_PATH}${interface}.pcap" >> "${USB_UTILS_LOG_PATH}" 2>&1 &
  done <<< "${usbmon_interfaces}"

  if [[ -f "${DEBUG_FS_PATH}/dynamic_debug/control" ]]; then
    # Iterate over the modules arrays
    for module in "${dd_modules[@]}"; do
      echo -n "module ${module} +p" > "${DEBUG_FS_PATH}/dynamic_debug/control"
      info_green "Dynamic debug was enabled for: ${module}"
    done
  else
    info_red "Dynamic debug missing; use a kernel \
      compiled with dynamic debug enabled!"
  fi

  exit 0
}

disable_usb_debug() {
  pgrep "tcpdump" | xargs -L 1 sudo kill -9

  if [[ -f "${DEBUG_FS_PATH}/dynamic_debug/control" ]]; then
    echo " -p" > "${DEBUG_FS_PATH}/dynamic_debug/control"
  else
    info_green "Dynamic debug was not enabled, \
      nothing to disable in dynamic debug"
  fi

  rmmod usbmon

  umount debugfs

  exit 0
}

main() {
  while [[ $# -gt 0 ]]; do
    case $1 in
      -e|--enable)
        enable_usb_debug
        shift
        ;;
      -d|--disable)
        disable_usb_debug
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
