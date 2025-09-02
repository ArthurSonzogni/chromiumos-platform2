#!/bin/bash
# Copyright 2024 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

BAGUETTE_ARCH=$(dpkg --print-architecture)
SCRIPT_DIR=$(dirname "$0")

set -eux

function cleanup {
  echo "Cleaning up build state..."

  # some rm'ing may have occurred, so try everything each time.
  set +e

  sudo rm -rf target/tmp/data
  sudo rm target/tmp/setup_in_guest.sh
  sudo umount target/run
  sudo umount target/dev/pts
  sudo umount target/dev
  sudo umount -R target/sys
  sudo umount -R target/proc
  sudo rm -rf target
}

trap cleanup EXIT

mkdir target
sudo cdebootstrap --arch "${BAGUETTE_ARCH}" --include=ca-certificates trixie target https://deb.debian.org/debian/
sudo mount --bind /dev target/dev
sudo mount --bind /dev/pts target/dev/pts
sudo mount --make-rslave --rbind /proc target/proc
sudo mount --make-rslave --rbind /sys target/sys
sudo mount -t tmpfs tmpfs target/run
cp -r "${SCRIPT_DIR}/data" "target/tmp/"

cp "${SCRIPT_DIR}/setup_in_guest.sh" target/tmp/
sudo sed -i 's/CROS_PACKAGES_URL/https:\/\/storage.googleapis.com\/cros-packages-staging\/136\//g' target/tmp/setup_in_guest.sh
sudo sed -i 's/CROS_RELEASE/trixie/g' target/tmp/setup_in_guest.sh

sudo chroot target /tmp/setup_in_guest.sh

sudo rm -rf target/tmp/data
sudo rm target/tmp/setup_in_guest.sh
sudo umount target/run
sudo umount target/dev/pts
sudo umount target/dev
sudo umount -R target/sys
sudo umount -R target/proc
sudo tar -c -f rootfs.tar -C target .
sudo chown "$(whoami)" rootfs.tar
