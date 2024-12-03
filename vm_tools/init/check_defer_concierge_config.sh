#!/bin/sh

# Copyright 2024 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

set -e

DBUS_REPLY=$(/usr/bin/dbus-send --system --print-reply \
    --type=method_call \
    --dest=org.chromium.ChromeFeaturesService \
    /org/chromium/ChromeFeaturesService \
    org.chromium.ChromeFeaturesServiceInterface.IsFeatureEnabled \
    string:"DeferConciergeStartup")
ENABLED=$(echo "${DBUS_REPLY}" | awk 'NR==2 { print $2 }')

if [ "${ENABLED}" != "true" ]; then
  exit 1
fi

exit 0
