# Copyright 2024 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

BOARD       := nocturne
# The fpstudy setup is still designed for bloonchipper
# samples count, only. Use at your own risk.
TYPE        := test
# TYPE        := base
# TYPE        := base-enc
# BRANCH      := release-R114-15437.B
# BRANCH      := release-R115-15474.B
# BRANCH      := release-R116-15509.B
# BRANCH      := stable
BRANCH      := main
# BRANCH_SHORT := R114
# BRANCH_SHORT := R115
# BRANCH_SHORT := R116
# BRANCH_SHORT:= stable
BRANCH_SHORT:= main
DESCRIPTION  := This is a test image designated for Chrome OS Fingerprint internal use.
