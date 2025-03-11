#!/bin/bash
# Copyright 2024 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

set -ex

docker buildx rm insecure-builder || true
docker buildx create --use --name insecure-builder --buildkitd-flags '--allow-insecure-entitlement security.insecure'
rm -rf docker_export
mkdir -p docker_export
docker buildx build -f src/build.Dockerfile --platform linux/amd64,linux/arm64 --allow security.insecure --output type=local,dest=docker_export .
docker buildx build -f src/convert.Dockerfile --build-arg COMPRESSION_LEVEL=19 --output type=local,dest=docker_export .
