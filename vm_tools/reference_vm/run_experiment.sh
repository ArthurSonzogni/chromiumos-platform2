#!/bin/bash
# Copyright 2026 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# Experimental script to process a local VM image and update Tast test data
# so you can test a refvm image with tast tests locally.

set -eux -o pipefail

# Configuration
TIMESTAMP=$(date +%Y%m%d_%H%M%S)
EXPERIMENT_NAME="${TIMESTAMP}"
OUT_DIR="out/experimental/${EXPERIMENT_NAME}"
mkdir -p "${OUT_DIR}"

# Input image path from argument, default to refvm.img
INPUT_IMAGE="${1:-refvm.img}"

if [ ! -f "${INPUT_IMAGE}" ]; then
    echo "Error: Input image not found at ${INPUT_IMAGE}"
    exit 1
fi

# Working copy in out directory
QCOW2_PATH="${OUT_DIR}/refvm-${EXPERIMENT_NAME}.qcow2"

echo "=== 1. Processing Image (Sparsify, Convert) ==="
# Create a sparsified qcow2 image directly from the input.
# This keeps the original INPUT_IMAGE unchanged.
sudo virt-sparsify "${INPUT_IMAGE}" "${QCOW2_PATH}" --convert qcow2

# Fix permissions so we can read it without sudo
sudo chown "$(whoami)" "${QCOW2_PATH}"

# Get uncompressed hash for Tast
UNCOMPRESSED_SUM=$(sha256sum "${QCOW2_PATH}" | cut -d ' ' -f 1)

# Compress image. Tast fixture expects brotli-compressed image.
brotli -7 "${QCOW2_PATH}"
COMPRESSED_PATH="${QCOW2_PATH}.br"

echo "=== 3. Updating Tast Tests ==="
REPO_DIR=$(repo list -pf chromiumos/platform/tast-tests)
DATA_DIR="${REPO_DIR}/src/go.chromium.org/tast-tests/cros/local/bruschetta/data"

if [ ! -d "${DATA_DIR}" ]; then
    echo "Error: Tast tests data directory not found at ${DATA_DIR}"
    exit 1
fi

echo "Copying compressed image to Tast data directory..."
cp "${COMPRESSED_PATH}" "${DATA_DIR}/refvm.qcow2"

echo "Updating refvm.qcow2.SHA256 with uncompressed hash..."
echo "${UNCOMPRESSED_SUM}" > "${DATA_DIR}/refvm.qcow2.SHA256"

echo "Removing refvm.qcow2.external..."
rm -f "${DATA_DIR}/refvm.qcow2.external"

echo "=== Done! ==="
echo "Experiment: ${EXPERIMENT_NAME}"
echo "Tast test data updated in: ${DATA_DIR}"
echo "You can now run test by: tast run \${DUT} bruschetta.Basic"
