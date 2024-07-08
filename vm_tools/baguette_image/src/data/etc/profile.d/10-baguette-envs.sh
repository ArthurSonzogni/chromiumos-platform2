# Copyright 2024 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# This is not meant to be executed, no shebang needed for profile
# shellcheck disable=SC2148

IDU_RESULT=$(id -u)
IDUN_RESULT=$(id -un)

if [[ -z "${DBUS_SESSION_BUS_ADDRESS}" ]]; then
  export DBUS_SESSION_BUS_ADDRESS="unix:path=/run/user/${IDU_RESULT}/bus"
fi

if [[ -z "${XDG_SESSION_TYPE}" ]]; then
  export XDG_SESSION_TYPE="wayland"
fi

if [[ -z "${XDG_RUNTIME_DIR}" ]]; then
  export XDG_RUNTIME_DIR="/run/user/${IDU_RESULT}"
fi

if [[ -z "${USER}" ]]; then
  export USER="${IDUN_RESULT}"
fi

# We also want to wait w/ timeout here for sommelier to start before next script is ran
