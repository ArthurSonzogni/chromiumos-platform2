## Overview

There are 3 ways to build baguette image:
- Native build
- Docker build
- CQ build

Native build is recommended when doing development. However, native builds can only produce baguette image native to builder's architecture. To cross-build a baguette image, docker buildx is used. CQ build is a thin wrapper around docker build to be used in ChromeOS infrastructure.

## Build Requirements

To start a native build, you need a machine native to the target architecture. If you don't have an arm64 Debian device available, a T2A vm instance on GCP will be handy.

Run `src/deps.sh` to install prerequisites.

For docker build, you need docker buildx to be installed and callable as `docker buildx`.

## Build Instructions

#### Native Build

Assume everything goes well, by running `src/build.sh -c` you will get a `rootfs.tar` which contains the rootfs of baguette image.
Alternatively you can use `src/build.sh -u $USERNAME` for a specific username (otherwise chronos will be used).
Then run `src/generate_disk_image.py rootfs.tar baguette_rootfs.img.zstd` will give you a compressed disk image of baguette.

#### Docker Build

Docker build automatically builds for both x86_64 and arm64 architectures.
Run `src/docker-build.sh`, it will generate `docker_export/baguette_rootfs_amd64.img.zstd` and `docker_export/baguette_rootfs_arm64.img.zstd`. This script can only be ran in `baguette_image` directory.
If `docker_export` directory already exists, it will be deleted before building.

#### CQ Build

CQ build is not implemented yet.
