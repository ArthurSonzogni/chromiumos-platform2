#!/bin/bash
# Copyright 2023 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

set -eux -o pipefail

CROS_PACKAGES=(
  cros-garcon
  cros-sommelier
  cros-sommelier-config
  cros-wayland
)
PACKAGES=(
  bash-completion
  curl
  dkms
  dosfstools
  grub-efi-amd64
  grub-efi-amd64-signed
  linux-image-amd64
  locales
  lvm2
  network-manager
  pciutils
  usbutils
  vim
  sudo
  systemd-timesyncd
)
DATA_ROOT="/tmp/data"

main() {
  export DEBIAN_FRONTEND=noninteractive

  echo localhost > /etc/hostname

  apt-get update
  apt-get -y install "${PACKAGES[@]}"

  rm -f /etc/locale.gen
  debconf-set-selections << EOF
locales locales/default_environment_locale select en_US.UTF-8
locales locales/locales_to_be_generated multiselect en_US.UTF-8 UTF-8
EOF
  dpkg-reconfigure locales

  # install the bootloader
  grub-install --uefi-secure-boot --target=x86_64-efi --no-nvram --removable
  grub-install --uefi-secure-boot --target=x86_64-efi --no-nvram
  install -m 0644 -t /etc/default/grub.d \
    "${DATA_ROOT}/etc/default/grub.d/50-reference-vm.cfg"
  update-grub

  install -m 0755 -t /usr/local/bin \
    "${DATA_ROOT}/usr/local/bin/update-cros-list"

  install -D -m 0644 -t /usr/local/lib/systemd/system \
    "${DATA_ROOT}/usr/local/lib/systemd/system/maitred.service" \
    "${DATA_ROOT}/usr/local/lib/systemd/system/opt-google-cros\\x2dcontainers.mount" \
    "${DATA_ROOT}/usr/local/lib/systemd/system/update-cros-list.service" \
    "${DATA_ROOT}/usr/local/lib/systemd/system/vshd.service"
  systemctl enable maitred.service update-cros-list.service vshd.service \
    'opt-google-cros\x2dcontainers.mount'

  install -D -m 0644 -t /usr/src/virtio-wayland-0 \
    "${DATA_ROOT}/usr/src/virtio-wayland-0/dkms.conf" \
    "${DATA_ROOT}/usr/src/virtio-wayland-0/Makefile" \
    "${DATA_ROOT}/usr/src/virtio-wayland-0/virtio_wl.c"
  install -D -m 0644 -t /usr/src/virtio-wayland-0/include/linux \
    "${DATA_ROOT}/usr/src/virtio-wayland-0/include/linux/virtio_wl.h" \
    "${DATA_ROOT}/usr/src/virtio-wayland-0/include/linux/virtwl.h"
  install -D -m 0644 -t /var/lib/dkms "${DATA_ROOT}/var/lib/dkms/mok.pub"
  install -D -m 0600 -t /var/lib/dkms "${DATA_ROOT}/var/lib/dkms/mok.key"

  # Find the installed, not running, kernel version.
  kernel="$(dpkg-query -Wf '${Package}\n' 'linux-image-*-amd64' | tail -n 1 | \
    sed -E -e 's/linux-image-//')"
  dkms install virtio-wayland/0 -k "${kernel}"

  # chromeos guest tools repo
  curl https://dl.google.com/linux/linux_signing_key.pub | gpg --dearmor > \
    /usr/share/keyrings/cros.gpg
  # shellcheck disable=SC2154
  echo "deb [signed-by=/usr/share/keyrings/cros.gpg] ${CROS_PACKAGES_URL} ${RELEASE} main" > \
    /etc/apt/sources.list.d/cros.list

  # dummy files for installation
  mkdir -p /opt/google/cros-containers/bin
  touch /opt/google/cros-containers/bin/sommelier

  apt-get update
  apt-get -y install "${CROS_PACKAGES[@]}"

  # test user for debugging
  useradd -m -s /bin/bash -G sudo chronos
  chpasswd <<< chronos:test0000
  mkdir -p /var/lib/systemd/linger
  touch /var/lib/systemd/linger/chronos

  # TODO(b/271522474): leave networking to NM
  ln -sf /run/resolv.conf /etc/resolv.conf

  # cleanup
  apt-get clean
  rm -rf /var/lib/apt/lists
  rm -rf /opt/google/cros-containers/*
}

main "$@"
