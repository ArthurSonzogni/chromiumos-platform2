#!/bin/bash
# Copyright 2024 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

docker buildx create --use --name insecure-builder --buildkitd-flags '--allow-insecure-entitlement security.insecure'
mkdir -p docker_export
docker buildx build -f src/build.Dockerfile --platform linux/amd64,linux/arm64 --allow security.insecure --output type=local,dest=docker_export .
docker buildx build -f src/convert.Dockerfile --output type=local,dest=docker_export .
