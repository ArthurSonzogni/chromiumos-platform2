#!/bin/bash
# Copyright 2014 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

set -e

OUT=$1
v=$2

deps=$(<"${OUT}"/gen/libshill-net-${v}-deps.txt)
sed \
  -e "s/@BSLOT@/${v}/g" \
  -e "s/@PRIVATE_PC@/${deps}/g" \
  "net/libshill-net.pc.in" > "${OUT}/lib/libshill-net-${v}.pc"
