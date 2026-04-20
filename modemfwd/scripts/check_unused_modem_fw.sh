#!/bin/bash
# Copyright 2026 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
#
# Validate all firmware payloads referenced in the modemfwd-helpers package
# are also used in a *-9999 DLC ebuild. Alternatively, you can run the
# 'tast run ${DUT} cellular.ModemHelperManifestVerification' command to
# execute a more comprehensive test.
declare -i unused_fw_count=0

SEARCH_PATTERN="\${MIRROR_PATH}/"
EBUILD_PATTERN="chromeos-base/modemfwd-helpers/"
EBUILD_PATTERN+="modemfwd-helpers-+([0-9]).+([0-9]).+([0-9]).ebuild"

# Enable extglob.
shopt -s extglob

# We need unquoted expansion of EBUILD_PATTERN to perform pathname expansion
# (globbing) to find all matching ebuild files.
# shellcheck disable=SC2206
ebuild_files=(${EBUILD_PATTERN})

# Disable extglob after it's been used.
shopt -u extglob

echo "Checking ${ebuild_files[*]} ..."

# Check if any ebuild files were found.
if [ "${#ebuild_files[@]}" -eq 0 ]; then
  echo >&2 "PRESUBMIT ERROR: No ebuild files found matching ${EBUILD_PATTERN}"
  # Exit with an error code to block submission/upload.
  exit 1
fi

firmware_list=$(grep "${SEARCH_PATTERN}" "${ebuild_files[0]}")

while read -r line || [[ -n "${line}" ]]; do
  # Trim space, tab, newline and " characters
  fw_name=$(echo "${line}" | tr -d " \t\n\"")
  # Trim "${MIRROR_PATH}/" prefix
  fw_name="${fw_name#"${SEARCH_PATTERN}"}"

  count=$(grep -r --include="modem-fw-dlc*9999.ebuild" "${fw_name}" . | wc -l)

  # Find modem firmware that is referenced somewhere
  if [[ ${count} -eq 0 ]]; then
    unused_fw_count+=1
    echo >&2 "${unused_fw_count}: ${fw_name}"
  fi
done <<< "${firmware_list}"

if [[ ${unused_fw_count} -ge 1 ]]; then
  if [[ ${unused_fw_count} -eq 1 ]]; then
    echo >&2 "Found ${unused_fw_count} entry"
    echo >&2 "Please remove above entry, re-gen manifest and try again"
  else
    echo >&2 "Found ${unused_fw_count} entries"
    echo >&2 "Please remove above entries, re-gen manifest and try again"
  fi
  exit 1
fi

echo "No un-used modem firmware found!"
exit 0
