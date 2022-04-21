#!/bin/sh

# Copyright 2017 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
#
# This script provides various data about the internal disk to show on the
# chrome://system page. It will run once on startup to dump output to the file
# /var/log/storage_info.txt which will be read later by debugd when the user
# opens the chrome://system page.
. /usr/share/misc/chromeos-common.sh

SSD_CMD_0="hdparm -I"
SSD_CMD_1_NORMAL="smartctl -x"
SSD_CMD_1_ALTERNATE="smartctl -a -f brief"
SSD_CMD_MAX=1

# This match SanDisk SSD U100/i100 with any size with version *.xx.* when x < 54
# Seen Error with U100 10.52.01 / i100 CS.51.00 / U100 10.01.04.
MODEL_IGNORELIST_0="SanDisk_SSD_[iU]100.*"
VERSION_IGNORELIST_0="(CS|10)\.([01234].|5[0123])\..*"
MODEL_IGNORELIST_1="SanDisk_SDSA5GK-.*"
VERSION_IGNORELIST_1="CS.54.06"
MODEL_IGNORELIST_2="LITEON_LST-.*"
VERSION_IGNORELIST_2=".*"
MODEL_IGNORELIST_3="LITEON_CS1-SP.*"
VERSION_IGNORELIST_3=".*"
IGNORELIST_MAX=3

MMC_NAME_0="cid"
MMC_NAME_1="csd"
MMC_NAME_2="date"
MMC_NAME_3="enhanced_area_offset"
MMC_NAME_4="enhanced_area_size"
MMC_NAME_5="erase_size"
MMC_NAME_6="fwrev"
MMC_NAME_7="hwrev"
MMC_NAME_8="manfid"
MMC_NAME_9="name"
MMC_NAME_10="oemid"
MMC_NAME_11="preferred_erase_size"
MMC_NAME_12="prv"
MMC_NAME_13="raw_rpmb_size_mult"
MMC_NAME_14="rel_sectors"
MMC_NAME_15="serial"
MMC_NAME_MAX=15

NVME_CMD_0="smartctl -x"
NVME_CMD_MAX=0

UFS_DIR_NAME_0="string_descriptors"
UFS_DIR_NAME_1="health_descriptor"
UFS_DIR_NAME_2="device_descriptor"
UFS_DIR_NAME_3="flags"
UFS_DIR_NAME_4="geometry_descriptor"
UFS_DIR_NAME_5="interconnect_descriptor"
UFS_DIR_NAME_6="attributes"
UFS_DIR_NAME_7="power"
UFS_DIR_NAME_8=""
UFS_DIR_NAME_MAX=8

# exapnd_var - evaluates a variable represented by a string
#
# inputs:
#   variable name
#
# outputs:
#   output of variable's evaluation
expand_var() {
  eval "echo \"\${$1}\""
}

# get_ssd_model - Return the model name of an ATA device.
#
# inputs:
#   output of hdparm -i command.
#
# outputs:
#   the model name of the device, sanitized of space and punctuation.
get_ssd_model() {
  echo "$1" | sed -e "s/^.*Model=//g" -e "s/,.*//g" -e "s/ /_/g"
}

# get_ssd_version - Return the firmware version of an ATA device.
#
# inputs:
#   output of hdparm -i command.
#
# outputs:
#   the version of the device firmware, sanitized of space and punctuation.
get_ssd_version() {
  echo "$1" | sed -e "s/^.*FwRev=//g" -e "s/,.*//g" -e "s/ /_/g"
}

# is_ignorelist - helper function for is_ssd_ignorelist.
#
# inputs:
#   the information from the device.
#   the ignorelist element to match against.
is_ignorelist() {
  echo "$1" | grep -Eq "$2"
}

# is_ssd_ignorelist - Return true is the device is ignorelisted.
#
# inputs:
#   model : model of the ATA device.
#   version : ATA device firmware version.
#
# outputs:
#   True if the device belongs into the script ignorelist.
#   When an ATA device is in the ignorelist, only a subset of the ATA SMART
#   output is displayed.
is_ssd_ignorelist() {
  local model="$1"
  local version="$2"
  local model_ignorelist
  local version_ignorelist
  local i

  for i in $(seq 0 "${IGNORELIST_MAX}"); do
    model_ignorelist=$(expand_var "MODEL_IGNORELIST_${i}")
    if is_ignorelist "${model}" "${model_ignorelist}"; then
      version_ignorelist=$(expand_var "VERSION_IGNORELIST_${i}")
      if is_ignorelist "${version}" "${version_ignorelist}"; then
        return 0
      fi
    fi
  done
  return 1
}

# print_ssd_info - Print SATA device information
#
# inputs:
#   device name for instance sdb.
print_ssd_info() {
  # BUG: On some machines, smartctl -x causes SSD error (crbug.com/328587).
  # We need to check model and firmware version of the SSD to avoid this bug.

  # SSD model and firmware version is on the same line in hdparm result.
  local hdparm_result="$(hdparm -i "/dev/$1" | grep "Model=")"
  local model="$(get_ssd_model "${hdparm_result}")"
  local version="$(get_ssd_version "${hdparm_result}")"
  local ssd_cmd
  local i

  if is_ssd_ignorelist "${model}" "${version}"; then
    SSD_CMD_1=${SSD_CMD_1_ALTERNATE}
  else
    SSD_CMD_1=${SSD_CMD_1_NORMAL}
  fi

  for i in $(seq 0 "${SSD_CMD_MAX}"); do
    ssd_cmd=$(expand_var "SSD_CMD_${i}")
    echo "$ ${ssd_cmd} /dev/$1"
    ${ssd_cmd} "/dev/$1"
    echo ""
  done
}

# print_mmc_info - Print eMMC device information
#
# inputs:
#   device name for instance mmcblk0.
print_mmc_info() {
  local mmc_name
  local mmc_path
  local mmc_result
  local i

  for i in $(seq 0 "${MMC_NAME_MAX}"); do
    mmc_name=$(expand_var "MMC_NAME_${i}")
    mmc_path="/sys/block/$1/device/${mmc_name}"
    mmc_result="$(cat "${mmc_path}" 2>/dev/null)"
    printf "%-20s | %s\n" "${mmc_name}" "${mmc_result}"
  done

  mmc extcsd read "/dev/$1"
}

# print_nvme - Print NVMe device information
#
# inputs:
#   device name for instance nvme0n1.
print_nvme_info() {
  local nvme_cmd
  local mvme_dev="/dev/$1"
  local i

  for i in $(seq 0 "${NVME_CMD_MAX}"); do
    nvme_cmd=$(expand_var "NVME_CMD_${i}")
    echo "$ ${nvme_cmd} ${mvme_dev}"
    ${nvme_cmd} "${mvme_dev}"
    echo ""
  done
}

# print_ufs_info - Print UFS device information
#
# inputs:
#   device name for instance sdb.
print_ufs_info() {
  local dev="$1"
  local ufs_dir
  local ufs_name
  local ufs_path
  local ufs_result
  local i
  local file

  ufs_dir="$(readlink -f "/sys/block/${dev}")"

  while true; do
    case "$(basename "${ufs_dir}")" in
      *ufs*)
        break
        ;;
      *)
        ufs_dir="$(dirname "${ufs_dir}")"
        ;;
    esac
  done

  echo "/sys/block/$1"
  for i in $(seq 0 "${UFS_DIR_NAME_MAX}"); do
    ufs_name=$(expand_var "UFS_DIR_NAME_${i}")
    ufs_path="${ufs_dir}/${ufs_name}"
    echo "${ufs_path}"
    find "${ufs_path}" -maxdepth 1 -type f -not -name uevent \
      -exec grep ^ {} + | cut -b"${#ufs_path}-" | \
      cut -f2 -d"/"
    echo ""
  done
}

# get_storage_info - Print device information.
#
# Print device information for all fixed devices in the system.
get_storage_info() {
  local dev

  for dev in $(list_fixed_ata_disks); do
    print_ssd_info "${dev}"
  done

  for dev in $(list_fixed_mmc_disks); do
    print_mmc_info "${dev}"
  done

  for dev in $(list_fixed_nvme_disks); do
    print_nvme_info "${dev}"
  done

  for dev in $(list_fixed_ufs_disks); do
    print_ufs_info "${dev}"
  done
}
