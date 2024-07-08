#!/bin/bash
# Copyright 2024 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

BAGUETTE_ARCH=$(dpkg --print-architecture)
SCRIPT_DIR=$(dirname "$0")

mkdir target
sudo cdebootstrap --arch "${BAGUETTE_ARCH}" --include=ca-certificates trixie target https://deb.debian.org/debian/
sudo mount --bind /dev target/dev
sudo mount --bind /dev/pts target/dev/pts
sudo mount --make-rslave --rbind /proc target/proc
sudo mount --make-rslave --rbind /sys target/sys
sudo mount -t tmpfs tmpfs target/run
cp -r "${SCRIPT_DIR}/data" "target/tmp/"

cp "${SCRIPT_DIR}/setup_in_guest.sh" target/tmp/
sudo sed -i 's/CROS_PACKAGES_URL/https:\/\/storage.googleapis.com\/cros-packages-staging\/128\//g' target/tmp/setup_in_guest.sh
sudo sed -i 's/CROS_RELEASE/trixie/g' target/tmp/setup_in_guest.sh

sudo chroot target /tmp/setup_in_guest.sh

sudo rm -rf target/tmp/data
sudo rm target/tmp/setup_in_guest.sh
sudo umount target/run
sudo umount target/dev/pts
sudo umount target/dev
sudo umount -R target/sys
sudo umount -R target/proc

sudo virt-make-fs -t ext4 --size=+200M target "baguette_rootfs_${BAGUETTE_ARCH}.img"
sudo chown "${USER}" "baguette_rootfs_${BAGUETTE_ARCH}.img"
zstd -10 -T0 "baguette_rootfs_${BAGUETTE_ARCH}.img"
