#!/bin/bash

# Copyright (c) 2010 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

echo "Modifying image for factory test..."

for SCRIPT in \
    /usr/share/chromeos-installer/mod_for_factory_scripts/[0-9][0-9][0-9]*[!$~]
do
  ${SCRIPT}
done
