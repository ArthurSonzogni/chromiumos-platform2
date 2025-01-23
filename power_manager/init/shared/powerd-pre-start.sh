#!/bin/sh -u
# Copyright 2016 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# Declare vars that we know are set before the script is run.
: "${UPSTART_RUN:=}"
: "${LOG_DIR:=}"
: "${POWER_RUN_DIR:=}"
: "${POWER_VARLIB_DIR:=}"
: "${PREFS_DIR:=}"
: "${ROOT_RUN_DIR:=}"
: "${ROOT_SPOOL_DIR:=}"
: "${VPD_CACHE_FILE:=}"
: "${MAX_NITS_PREF:=}"

# This script is shared by Upstart and systemd. All variables used by this
# script must be defined either in powerd.conf file for Upstart or
# powerd.service file for systemd.

# For Upstart create dirs and set the proper permissions.
# For systemd, systemd-tmpfiles handles this using powerd_directories.conf.
# We use the "UPSTART_RUN" variable, that is only defined by the Upstart conf
# to distinguish between these situations.
if [ -n "${UPSTART_RUN}" ]; then
  mkdir -p "${LOG_DIR}" "${POWER_RUN_DIR}" "${POWER_VARLIB_DIR}" "${PREFS_DIR}"
  chown -R power:power "${LOG_DIR}" "${POWER_RUN_DIR}" "${POWER_VARLIB_DIR}" \
    "${PREFS_DIR}"
  chmod 755 "${LOG_DIR}" "${POWER_RUN_DIR}" "${POWER_VARLIB_DIR}" "${PREFS_DIR}"

  mkdir -p "${ROOT_RUN_DIR}" "${ROOT_SPOOL_DIR}"
fi

# Read the real maximum backlight luminance (i.e. not the value reported by
# the driver) from VPD and pass it to powerd via a pref file.
if [ -e /sys/firmware/vpd/ro/panel_backlight_max_nits ]; then
  PREF_FILE="${PREFS_DIR}/${MAX_NITS_PREF}"
  cp /sys/firmware/vpd/ro/panel_backlight_max_nits "${PREF_FILE}"
  [ -x "/sbin/restorecon" ] && restorecon "${PREF_FILE}"
  chown power:power "${PREF_FILE}"
  chmod 644 "${PREF_FILE}"
fi

# Change ownership of files used by powerd.
# clear $@ and store each file into it.
set --

# Test for existence to skip over wildcards that didn't match anything.
for FILE in \
    /sys/power/pm_test \
    /proc/acpi/wakeup \
    /sys/class/backlight/*/* \
    /sys/class/leds/*:kbd_backlight/* \
    /sys/class/power_supply/*/charge_control_limit_max \
    /sys/module/printk/parameters/console_suspend \
    /sys/power/mem_sleep \
    /sys/power/sync_on_suspend \
    /dev/snapshot; do
  if [ -e "${FILE}" ]; then
    set -- "$@" "${FILE}"
  fi
done

# NOTE: beware of byte size and argument limit for $@ in case
# the size of iteration above grows too big.
chown power:power "$@" || true

# wakeup files that are used by powerd lives in nested directory,
# use find to find all files and pass it to chown.
find /sys/devices/ -path "*/power/wakeup" -print0 | \
  xargs -0 chown power:power || true

if [ -e "/sys/power/pm_print_times" ]; then
  echo 1 > /sys/power/pm_print_times
fi

if [ -e "/sys/power/pm_debug_messages" ]; then
  echo 1 > /sys/power/pm_debug_messages
fi

if [ -e "/usr/share/cros/init/optional/powerd-pre-start-wilco.sh" ]; then
  /usr/share/cros/init/optional/powerd-pre-start-wilco.sh || true
fi
