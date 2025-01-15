# syntax=docker/dockerfile:1.3-labs

# Copyright 2024 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# This Dockerfile is multi-arch compatible, it generates rootfs.tar for each arch

FROM marketplace.gcr.io/google/debian12 AS build-stage
RUN apt-get update && apt-get install -y git sudo
WORKDIR /workspace
COPY src src
RUN chmod +x src/build.sh src/docker-deps.sh
RUN src/docker-deps.sh
RUN --security=insecure src/build.sh

FROM scratch
COPY --from=build-stage /workspace/rootfs.tar .
