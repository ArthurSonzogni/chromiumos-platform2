#!/bin/bash
# Copyright 2020 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

set -u -e

# Temporary script to get cros_config values for an alternative identity.
# Used for factory process until b/152291015 is resolved.

CONFIGFS_IMAGE="/usr/share/chromeos-config/configfs.img"
SQUASHFS_BASE="/run/chromeos-config/private"

FRID=""
SKU_ID=""
CUSTOM_LABEL_TAG=""

print_usage () {
  cat <<EOF >&2
Usage: $0 [OPTIONS...] PATH PROPERTY

Optional arguments:
  --configfs-image FILE     Path to configfs image.
  --frid FRID               Override the FRID from firmware.
  --sku-id SKU              Override the SKU id from firmware.
  --custom-label-tag VALUE  Override the whitelabel tag from VPD.
  --help                    Show this help message and exit.

Positional arguments:
  PATH                    The path to get from config.
  PROPERTY                The property to get from config.
EOF
}

if [[ "${#@}" -eq 0 ]]; then
  print_usage
  exit 1
fi

while [[ "${1:0:1}" != "/" ]]; do
  case "$1" in
    --configfs-image )
      CONFIGFS_IMAGE="$2"
      shift
      ;;
    --frid )
      FRID="$2"
      shift
      ;;
    --sku-id )
      SKU_ID="$2"
      shift
      ;;
    --custom-label-tag )
      CUSTOM_LABEL_TAG="$2"
      shift
      ;;
    --help )
      print_usage
      exit 0
      ;;
    * )
      print_usage
      echo >&2
      echo "Unknown argument: $1" >&2
      exit 1
      ;;
  esac
  shift
done

if [[ "${#@}" -ne 2 ]]; then
  print_usage
  exit 1
fi

PATH_NAME="$1"
PROPERTY_NAME="$2"

# Load default values from firmware.
FRID_PATHS=(
    /sys/devices/platform/chromeos_acpi/FRID
    /proc/device-tree/firmware/chromeos/readonly-firmware-version
)

for path in "${FRID_PATHS[@]}"; do
    if [[ -f "${path}" && -z "${FRID}" ]]; then
        read -r FRID <"${path}"
    fi
done
FRID="${FRID//.*/}"

if [[ -f /sys/class/dmi/id/product_sku && -z "${SKU_ID}" ]]; then
  # Trim off "sku" in front of the ID
  SKU_ID="$(cut -b4- </sys/class/dmi/id/product_sku)"
fi

if [[ -f /sys/firmware/vpd/ro/custom_label_tag && \
  -z "${CUSTOM_LABEL_TAG}" ]]; then
  read -r CUSTOM_LABEL_TAG </sys/firmware/vpd/ro/custom_label_tag || true
fi

if [[ -f /sys/firmware/vpd/ro/whitelabel_tag && \
  -z "${CUSTOM_LABEL_TAG}" ]]; then
  read -r CUSTOM_LABEL_TAG </sys/firmware/vpd/ro/whitelabel_tag || true
fi

on_exit_unmount () {
  umount "${SQUASHFS_BASE}"
  rmdir "${SQUASHFS_BASE}"
}

if [[ "${CONFIGFS_IMAGE}" != /usr/share/chromeos-config/configfs.img || \
  ! -d "${SQUASHFS_BASE}" ]]; then
  SQUASHFS_BASE="$(mktemp -d)"
  mount -oro "${CONFIGFS_IMAGE}" "${SQUASHFS_BASE}"
  trap on_exit_unmount EXIT
fi

# file_mismatch filename contents
# returns 0 if file exists and the contents don't match or
# file does not exist and content is not empty, 1 otherwise
file_mismatch () {
  if [[ -f "$1" && "${2,,}" != "$(tr '[:upper:]' '[:lower:]' <"$1")" ]]; then
    return 0
  fi
  if [[ ! -f "$1" && -n "$2" ]]; then
    return 0
  fi
  return 1
}

for base in "${SQUASHFS_BASE}"/v1/chromeos/configs/*; do
  if file_mismatch "${base}/identity/frid" "${FRID}"; then
    continue
  fi

  if file_mismatch "${base}/identity/sku-id" "${SKU_ID}"; then
    continue
  fi

  if file_mismatch "${base}/identity/custom-label-tag" "${CUSTOM_LABEL_TAG}"; then
    continue
  fi

  # Identity matched!
  cat "${base}${PATH_NAME}/${PROPERTY_NAME}"
  exit 0
done

echo "No identity matched!" >&2
exit 1
