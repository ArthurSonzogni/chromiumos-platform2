#!/bin/sh
# Copyright 2018 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# Functions for configuring process management policies in the chromiumos LSM.

# Path to the securityfs file for configuring process management security
# policies in the chromiumos LSM (used for kernel version <= 4.4).
# TODO(mortonm): Remove this and the corresponding line in
# add_process_mgmt_policy when all devices have been updated/backported to get
# the SafeSetID LSM functionality.
CHROMIUMOS_PROCESS_MGMT_POLICIES="/sys/kernel/security/chromiumos\
/process_management_policies/add_whitelist_policy"

# Path to the securityfs file for configuring process management security
# policies in the SafeSetID LSM (used for kernel version >= 4.14).
SAFESETID_PROCESS_MGMT_POLICIES="/sys/kernel/security/safesetid\
/whitelist_policy"

# Path to the securityfs file for configuring process management security
# policies in the SafeSetID LSM (used for kernel version >= 5.9).
SAFESETID_PROCESS_MGMT_POLICIES_UID="/sys/kernel/security/safesetid\
/uid_allowlist_policy"

# Path to securityfs file for configuring GID policies in the SafeSetID
# LSM. (only used/tested in kernels >= 5.9)
SAFESETID_PROCESS_MGMT_POLICIES_GID="/sys/kernel/security/safesetid\
/gid_allowlist_policy"

# Project-specific process management policies. Projects may add policies by
# adding a file under /usr/share/cros/startup/process_management_policies/
# for UID's and under /usr/share/cros/startup/gid_process_management_policies/
# for GID's, whose contents are one or more lines specifying a parent ID and a
# child ID that the parent can use for the purposes of process management.
# There should be one line for every mapping that is to be put in the
# allowlist. Lines in the file should use the following format:
# <UID>:<UID> or <GID>:<GID>
#
# For example, if the 'shill' user needs to use 'dhcp', 'openvpn' and 'ipsec'
# and 'syslog' for process management, the file would look like:
# 20104:224
# 20104:217
# 20104:212
# 20104:202

# _accumulate_policy_files takes in all the files contained in "${filepaths}"
# reads their contents, copies and appends them to a file determined by
# "${output}"
_accumulate_policy_files() {
  local file policy
  local accum=""
  local output="$1"
  shift
  for file in "$@"; do
    if [ -f "${file}" ]; then
      while read -r policy; do
        case "${policy}" in
        # Ignore blank lines.
        "") ;;
        # Ignore comments.
        "#"*) ;;
        # Don't record GID policies into CHROMIUMOS_PROCESS_MGMT_POLICIES.
        *)
          if [ -e "${CHROMIUMOS_PROCESS_MGMT_POLICIES}" ]; then
            if [ "${output}" != \
                  "${SAFESETID_PROCESS_MGMT_POLICIES_GID}" ]; then
              printf "%s" "${policy}" > "${CHROMIUMOS_PROCESS_MGMT_POLICIES}"
            fi
          else
            accum="${accum}${policy}\n"
          fi
          ;;
        esac
      done < "${file}"
    fi
  done
  printf "%b" "${accum}" > "${output}"
}

# Determine where securityfs files are placed.
# No inputs, checks for which securityfs file paths
# exist and accumulates files for securityfs.
configure_process_mgmt_security() {
  # For UID relevant files.
  if [ -e "${SAFESETID_PROCESS_MGMT_POLICIES_UID}" ]; then
    _accumulate_policy_files \
      "${SAFESETID_PROCESS_MGMT_POLICIES_UID}" \
      /usr/share/cros/startup/process_management_policies/*
  elif [ -e "${SAFESETID_PROCESS_MGMT_POLICIES}" ]; then
    _accumulate_policy_files \
      "${SAFESETID_PROCESS_MGMT_POLICIES}" \
      /usr/share/cros/startup/process_management_policies/*
  fi
  # For GID relevant files.
  if [ -e "${SAFESETID_PROCESS_MGMT_POLICIES_GID}" ]; then
    _accumulate_policy_files \
      "${SAFESETID_PROCESS_MGMT_POLICIES_GID}" \
      /usr/share/cros/startup/gid_process_management_policies/*
  fi
}
