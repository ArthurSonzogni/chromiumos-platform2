# syntax=docker/dockerfile:1.3

# Copyright 2024 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# This Docker file can only be ran with host arch, it will convert rootfs tarball of any arch into btrfs disk images

FROM marketplace.gcr.io/google/debian12 AS build-stage
RUN apt-get update && apt-get install -y git sudo linux-image-amd64
WORKDIR /workspace
COPY src src
RUN chmod +x src/deps.sh src/generate_disk_image.py
RUN src/deps.sh
COPY docker_export docker_export
RUN src/generate_disk_image.py docker_export/linux_amd64/rootfs.tar baguette_rootfs_amd64.img.zstd
RUN src/generate_disk_image.py docker_export/linux_arm64/rootfs.tar baguette_rootfs_arm64.img.zstd

FROM scratch
COPY --from=build-stage /workspace/baguette_rootfs_amd64.img.zstd .
COPY --from=build-stage /workspace/baguette_rootfs_arm64.img.zstd .
