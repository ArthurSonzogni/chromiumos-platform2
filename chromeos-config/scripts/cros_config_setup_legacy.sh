#!/bin/bash
# Copyright 2021 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# Set up /run/chromeos-config during boot (assumes pre-unibuild).
# Note: This is written to target busybox ash and bash, as it needs to
# run in recovery initramfs, where we only have busybox.

# TODO(jrosenth): Delete this script once we're 100% unibuild.

set -e

: "${MOUNTPOINT:=/run/chromeos-config}"
CONFIG_OUT="${MOUNTPOINT}/private"

# Magic environment variable that lets us get access to hidden mosys
# commands accessible only to cros-config now.
export I_AM_CROS_CONFIG=1

# Set a config to a value.
#   $1: path
#   $2: property
#   $3: value
setconfig() {
    local path="$1"
    local property="$2"
    local value="$3"

    if [ -n "${value}" ]; then
        mkdir -p "${CONFIG_OUT}${path}"
        echo -n "${value}" > "${CONFIG_OUT}${path}/${property}"
    fi
}

model="$(mosys platform model)"
brand_code="$(mosys platform brand)"
sku_id="$(mosys platform sku || true)"
customization_id="$(mosys platform customization || true)"
platform_name="$(mosys platform name)"
eval "$(grep DEVICETYPE /etc/lsb-release)"

case "${DEVICETYPE}" in
    CHROMEBOOK )
        has_backlight=true
        psu_type=battery
        ;;
    * )
        has_backlight=false
        psu_type=AC_only
esac

setconfig / brand-code "${brand_code}"
setconfig / name "${model}"
setconfig /firmware image-name "${model}"
setconfig /hardware-properties form-factor "${DEVICETYPE}"
setconfig /hardware-properties has-backlight "${has_backlight}"
setconfig /hardware-properties psu-type "${psu_type}"
setconfig /identity platform-name "${platform_name}"
setconfig /identity sku-id "${sku_id}"
setconfig /ui help-content-id "${customization_id}"

mkdir -p "${MOUNTPOINT}/v1"
mount -n -obind,ro,nodev,noexec,nosuid "${CONFIG_OUT}" "${MOUNTPOINT}/v1"
