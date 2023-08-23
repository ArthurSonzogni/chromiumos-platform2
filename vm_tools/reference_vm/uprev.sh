#!/bin/bash
# Copyright 2023 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# Updates reference VM data dependencies in tast-tests.

set -eu

tmpfile="$(mktemp)"
trap 'rm -rf "${tmpfile}"' EXIT

repodir="$(repo list -pf chromiumos/platform/tast-tests)"
datadir="${repodir}/src/go.chromium.org/tast-tests/cros/local/bruschetta/data"

echo "Finding latest refvm image..."
dir="$(gsutil ls -d 'gs://refvm-images/????-??' | tail -n 1)"
obj="$(gsutil ls "${dir}" | tail -n  1)"

echo "Downloading ${obj} to temporary file..."
gsutil -q cp "${obj}" "${tmpfile}"

echo "Updating refvm.qcow2.external..."
sum="$(sha256sum "${tmpfile}" | cut -d ' ' -f 1)"
size="$(stat -c %s "${tmpfile}")"
cat > "${datadir}/refvm.qcow2.external" << EOF
{
    "sha256sum": "${sum}",
    "size": ${size},
    "url": "${obj}"
}
EOF

echo "Updating refvm.qcow2.SHA256..."
brotli -d -c "${tmpfile}" | sha256sum | cut -d ' ' -f 1 > \
  "${datadir}/refvm.qcow2.SHA256"
