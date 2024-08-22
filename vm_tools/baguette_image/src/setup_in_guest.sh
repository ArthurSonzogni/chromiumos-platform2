#!/bin/bash
# Copyright 2024 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
#
# setup_in_guest.sh is run inside a chroot of the baguette image to configure it.
# It's launched by build.py.

set -eux -o pipefail

CROS_PACKAGES=(
  cros-garcon
  cros-port-listener
  cros-sommelier
  cros-sommelier-config
  cros-wayland
)
PACKAGES=(
  # base packages
  bash-completion
  curl
  gpg
  gsettings-desktop-schemas
  locales
  nano
  pciutils
  pipewire
  pipewire-pulse
  sudo
  systemd-timesyncd
  tpm2-tools
  usbutils
  wireplumber
  python3
  wget
)
DATA_ROOT="/tmp/data"

main() {
  export DEBIAN_FRONTEND=noninteractive

  echo baguette > /etc/hostname
  echo '127.0.0.1 baguette' >> /etc/hosts

  echo "en_US.UTF-8 UTF-8" > /etc/locale.gen

  apt-get update
  apt-get -y install "${PACKAGES[@]}" --no-install-recommends

  install -m 0755 -t /usr/local/bin \
    "${DATA_ROOT}/usr/local/bin/update-cros-list"

  install -D -m 0644 -t /usr/local/lib/systemd/journald.conf.d \
    "${DATA_ROOT}/usr/local/lib/systemd/journald.conf.d/50-console.conf"
  install -D -m 0644 -t /usr/local/lib/systemd/system \
    "${DATA_ROOT}/usr/local/lib/systemd/system/first-boot-cros.service" \
    "${DATA_ROOT}/usr/local/lib/systemd/system/maitred.service" \
    "${DATA_ROOT}/usr/local/lib/systemd/system/opt-google-cros\\x2dcontainers.mount" \
    "${DATA_ROOT}/usr/local/lib/systemd/system/update-cros-list.service" \
    "${DATA_ROOT}/usr/local/lib/systemd/system/vshd.service"
  systemctl enable maitred.service update-cros-list.service vshd.service \
    'opt-google-cros\x2dcontainers.mount' first-boot-cros.service

  install -D -m 0440 -t /etc/sudoers.d \
    "${DATA_ROOT}/etc/sudoers.d/10-no-password"

  install -D -m 0644 -t /etc/profile.d \
    "${DATA_ROOT}/etc/profile.d/10-baguette-envs.sh"

  install -D -m 0755 -t /usr/local/bin \
    "${DATA_ROOT}/usr/local/bin/first-boot-cros"

  # chromeos guest tools repo
  curl https://dl.google.com/linux/linux_signing_key.pub | gpg --dearmor > \
    /usr/share/keyrings/cros.gpg
  # shellcheck disable=SC2154
  echo "deb [signed-by=/usr/share/keyrings/cros.gpg] CROS_PACKAGES_URL CROS_RELEASE main" > \
    /etc/apt/sources.list.d/cros.list

  # dummy files for installation
  mkdir -p /opt/google/cros-containers/bin
  touch /opt/google/cros-containers/bin/sommelier
  # Required for boot with R/O rootfs
  mkdir -p /mnt/shared
  # Required for disk ballooning
  mkdir -p /mnt/stateful

  apt-get update
  apt-get -y install "${CROS_PACKAGES[@]}"

  # test user for debugging
  useradd -m -s /bin/bash -G audio,sudo,tss,video,dialout,netdev chronos
  chpasswd <<< chronos:test0000
  mkdir -p /var/lib/systemd/linger
  touch /var/lib/systemd/linger/chronos

  # Disable garcon auto-updates.
  sed -i -E \
    -e 's/(DisableAutomaticCrosPackageUpdates=)false/\1true/' \
    -e 's/(DisableAutomaticSecurityUpdates=)false/\1true/' \
    /home/chronos/.config/cros-garcon.conf

  # # TODO(b/271522474): leave networking to NM
  ln -sf /run/resolv.conf /etc/resolv.conf

  # cleanup
  apt-get clean
  rm -rf /opt/google/cros-containers/*
}

main "$@"
