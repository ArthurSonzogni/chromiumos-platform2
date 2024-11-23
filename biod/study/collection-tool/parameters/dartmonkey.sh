#!/bin/sh
# shellcheck disable=SC2034  # Vars consumed in sourcer, so ignore unused error
# Copyright 2023 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# Study parameters for FPC1145.
# Expect between 60 to 72 participants.
#
# During a normal enrollment with no faults (like low coverage) only 10
# samples are needed. This is rare to need only 10 total touches for this
# sensor/matcher. We collect 20 enrollment captures to account for
# potentially faulty enrollment captures. The biometric evaluation tool
# will only use as many captures as it needs to get coverage, which
# should be about 10. This attempts to simulate the normal enrollment
# flow, where a capture would be rejected if it contributed too little
# coverage.
#
# Fingers:         4
# Enrollment:      20
# Template Update: 20
# Verification:    80
#
# Capture 4 different fingers per participant.
FINGER_COUNT=4
# Capture 20 enrollment samples per finger.
ENROLLMENT_COUNT=20
# Capture 20 template update samples + the 80 verification samples per finger.
VERIFICATION_COUNT=100
