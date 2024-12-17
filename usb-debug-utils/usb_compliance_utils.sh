#!/bin/bash

# Copyright 2024 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

DEVICE_PATH="/sys/bus/usb/devices"
DRIVER_PATH="/sys/bus/usb/drivers"

usage() {
  cat <<EOF
Usage: $0 [options]
Enable and use the lvs driver for USB link layer tests.

Arguments:
-e|--enable <busnum>           enable the lvs driver on the provided bus
-d|--disable <busnum>          disable lvs and re-bind the hub driver
-g|--get_desc <busnum>         send GetDescriptor from the root hub
-r|--repeat_get_desc <busnum>  send GetDescriptor from the root hub every 50ms
-u|--u3_entry <busnum>         suspend the link to U3
-U|--u3_exit <busnum>          exit U3 link state
EOF
}

check_bus() {
  busnum=$1
  if ! [[ ${busnum} =~ ^[0-9]+$ ]]; then
    return 1
  fi

  # Check for USB 3.0 root hub with given bus number.
  root_hub="${DEVICE_PATH}/usb${busnum}"
  if ! [ -e "${root_hub}" ]; then
    return 1
  fi

  vid=$(cat "${root_hub}/idVendor")
  pid=$(cat "${root_hub}/idProduct")
  if [ "${vid}" != "1d6b" ] || [ "${pid}" != "0003" ]; then
    return 1
  fi

  return 0
}

enable_lvstest() {
  busnum=$1
  if ! modprobe lvstest; then
    echo "Failed to load lvs driver."
    return 1
  fi

  # The linux root hub will typically have one interface bound to the hub
  # driver. Check for its existence and confirm the hub driver is bound.
  intf="${DEVICE_PATH}/usb${busnum}/${busnum}-0:1.0"
  driver=$(basename "$(readlink "${intf}/driver")")
  if [[ ${driver} == "lvs" ]]; then
    echo "The lvs driver is already enabled on bus $1."
    return 1
  elif [[ ${driver} != "hub" ]]; then
    echo "Unable to find the root hub interface."
    return 1
  fi

  # Bind the lvs driver to the root hub.
  echo "${busnum}-0:1.0" > "${DRIVER_PATH}/hub/unbind"
  echo "1D6B 3" > "${DRIVER_PATH}/lvs/new_id"

  # Confirm the root hub is bound to the lvs driver.
  driver=$(basename "$(readlink "${intf}/driver")")
  if [[ ${driver} != "lvs" ]]; then
    echo "Failed to bind lvs driver."
    return 1
  fi

  echo "Enabled lvs driver on bus ${busnum}."
  return 0
}

disable_lvstest() {
  busnum=$1
  intf="${DEVICE_PATH}/usb${busnum}/${busnum}-0:1.0"
  driver=$(basename "$(readlink "${intf}/driver")")
  if [[ ${driver} != "lvs" ]]; then
    echo "The lvs driver is not enabled on bus ${busnum}."
    return 1
  fi

  # Re-bind the hub driver to the root hub.
  echo "${busnum}-0:1.0" > "${DRIVER_PATH}/lvs/unbind"
  echo "${busnum}-0:1.0" > "${DRIVER_PATH}/hub/bind"

  # Check the root hub now uses the hub driver.
  driver=$(basename "$(readlink "${intf}/driver")")
  if [[ ${driver} != "hub" ]]; then
    echo "Failed to re-bind hub driver."
    return 1
  fi

  echo "Disabled lvs driver on bus ${busnum}."
  return 0
}

sysfs_write() {
  busnum=$1
  filename=$2
  sysfs_path="${DEVICE_PATH}/usb${busnum}/${busnum}-0:1.0/${filename}"
  if ! [ -e "${sysfs_path}" ]; then
    echo "Unable to access ${filename}. Enable lvstest on bus ${busnum}."
    return 1
  fi

  err=$( ( echo > "${sysfs_path}" ) 2>&1 )
  if [[ "${err}" != "" ]]; then
    echo "Operation failed: ${err}"
    return 1
  fi

  echo "${filename} access completed successfully."
  return 0
}

repeat_sysfs_write() {
  busnum=$1
  filename=$2
  interval=$3
  sysfs_path="${DEVICE_PATH}/usb${busnum}/${busnum}-0:1.0/${filename}"
  if ! [ -e "${sysfs_path}" ]; then
    echo "Unable to access ${filename}. Enable lvstest on bus ${busnum}."
    return 1
  fi

  i=0
  echo "Attempting repeated writes to ${filename}"
  err=$( ( echo > "${sysfs_path}" ) 2>&1 )
  while [[ ${err} == "" ]]
  do
    i=$((i + 1))
    sleep "${interval}"
    err=$( ( echo > "${sysfs_path}" ) 2>&1 )
  done

  if [[ ${i} == 0 ]]; then
    echo "Operation failed: ${err}"
  else
    echo "Completed ${i} writes to ${filename}"
  fi

  return 0
}


main() {
  if [[ $# -eq 2 ]]; then

    # Confirm provided bus supports the lvs driver.
    check_bus "$2"
    if ! check_bus "$2"; then
      echo "Unable to find USB 3.0 root hub on bus $2."
      exit 1
    fi

    case $1 in
      -e|--enable)
        enable_lvstest "$2"
        exit $?
        ;;
      -d|--disable)
        disable_lvstest "$2"
        exit $?
        ;;
      -g|--get_desc)
        sysfs_write "$2" "get_dev_desc"
        exit $?
        ;;
      -u|--u3_entry)
        sysfs_write "$2" "u3_entry"
        exit $?
        ;;
      -U|--u3_exit)
        sysfs_write "$2" "u3_exit"
        exit $?
        ;;
      -r|--repeat_get_desc)
        repeat_sysfs_write "$2" "get_dev_desc" 0.05
        exit $?
        ;;
    esac
  fi

  # Invalid options received.
  usage
  exit 1
}

main "$@"
