#!/bin/bash
#
# Copyright 2022 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# Generate ChromeOS system logs and filter events for FPS UX Study
#
# Print filtered log on the terminal:
#   bash filtered_logs.sh
#
# Save output on a file:
#   bash filtered_logs.sh > filtered.log
#
# Log custom events:
#   logger FpsUxStudy <custom message>
#
# Notes:
# - Edit the list of patterns below to filter different events
# - Save both the **full** and filtered system logs

PATTERNS_FILENAME=log_patterns.txt

cat <<\EOF >"${PATTERNS_FILENAME}"
Power button down
Power button up
Shutting down
all displays off
all displays on
Turning screen
imming screen
StartEnrollSession
EndEnrollSession
DoEnrollImageEvent
Enrolling
Enroll =>
StartAuthSession
EndAuthSession
DoMatchEvent result
Ignoring fp match
Capturing
Matching
Match =>
FpsUxStudy
EOF

LOG_FILENAME=cros_$(date --iso-8601=seconds).log
generate_logs --compress=false --output="${LOG_FILENAME}"

grep --text -f "${PATTERNS_FILENAME}" "${LOG_FILENAME}" | sort | uniq

rm "${PATTERNS_FILENAME}"
