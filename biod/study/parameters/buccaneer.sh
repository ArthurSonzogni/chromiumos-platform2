#!/bin/sh
# shellcheck disable=SC2034  # Vars consumed in sourcer, so ignore unused error
# Copyright 2024 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# Study parameters for the Elan 80SG.
# Expect between 60 to 72 participants using Participant Groups - Option 3.
#
# During a normal enrollment with no faults (like low coverage) only 13
# samples are needed. We collect 30 enrollment captures to account for
# potentially faulty enrollment captures. The evaluation tool will only use
# as many captures as it needs to get coverage, which should be 13 normally.
# This attempts to simulate the normal enrollment flow, where a capture would
# be rejected if it contributed too little coverage.
#
# Fingers:         6
# Enrollment:      30
# Template Update: 20
# Verification:    60
#
# Capture 6 different fingers per participant.
FINGER_COUNT=6
# Capture 30 enrollment samples per finger.
ENROLLMENT_COUNT=30
# Capture 20 template update samples + the 60 verification samples per finger.
VERIFICATION_COUNT=80
