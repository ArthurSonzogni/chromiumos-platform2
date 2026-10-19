#!/bin/bash

# Copyright 2020 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# Logger wrapper.
log_message() {
  logger -t temp_logger "$@"
}

get_fans_speed() {
  if [ ! -e /dev/cros_ec ]; then
    return
  fi

  local cmd="/usr/sbin/ectool pwmgetfanrpm all"
  local fanspeed
  fanspeed=$(${cmd})
  local res=$?
  local logstr=""

  if [ "${res}" -ne 0 ]; then
    return
  fi

  while read -r entry; do
    local entry_stripped
    entry_stripped="${entry// /}"
    logstr="${logstr} ${entry_stripped}"
  done <<< "${fanspeed}"

  echo "${logstr}"
}

# Output xx:yyC where xx is temp sensor id and yy is the actual temperature.
get_sensor_temp() {
  local zone="$1"
  local name=""
  local temp=""

  if [ ! -r "${zone}" ]; then
    return
  fi
  read -r temp < "${zone}"
  # Log nothing for a zone whose temp can't be read.
  if [ -z "${temp}" ]; then
    return
  fi
  IFS= read -r name < "${zone%/*}/type"
  printf "%s:%dC\n" "${name// /_}" "$((temp / 1000))"
}

# Glob all temp sensors sysfs and output temperatures with get_sensor_temp().
get_all_sensor_temps() {
  local logstr=""
  local tempstr=""

  for zone in /sys/class/thermal/thermal_zone*/temp; do
    tempstr=$(get_sensor_temp "${zone}")
    logstr="${logstr} ${tempstr}"
  done

  echo "${logstr}"
}

# Read PL1 from powercap sysfs (Intel only), output nothing otherwise.
get_pl1() {
  local pl1="/sys/class/powercap/intel-rapl:0/constraint_0_power_limit_uw"
  local pl1uw=0

  if [ ! -r "${pl1}" ]; then
    return
  fi
  read -r pl1uw < "${pl1}"
  printf "PL1:%.3fW\n" "$((pl1uw))e-6"
}

main() {
  if [ $# -ne 0 ]; then
    echo "Usage: $0" >&2
    exit 1
  fi

  if [ ! -d "/sys/class/thermal/thermal_zone0" ]; then
    log_message "Exiting temp_logger, system does not have any temp sensor."
    exit 0
  fi

  while true; do
    log_message "$(get_all_sensor_temps)" "$(get_pl1)" "$(get_fans_speed)"
    sleep 60
  done
}

main "$@"
