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
  acl
  alsa-utils
  bash-completion
  btrfs-progs
  curl
  gpg
  gsettings-desktop-schemas
  locales
  nano
  pulseaudio-utils
  pciutils
  pipewire
  pipewire-pulse
  python3
  sudo
  systemd-timesyncd
  tpm2-tools
  usbutils
  vim
  wireplumber
  wget
)
DATA_ROOT="/tmp/data"

main() {
  export DEBIAN_FRONTEND=noninteractive

  guest_user=""
  while getopts ":cu:" opt; do
    case ${opt} in
      c)
        echo "will create chronos user for dev environments"
        guest_user="chronos"
        ;;
      u)
        # strip whitespace from nested chroot and getopts
        guest_user=$(echo "${OPTARG}" | xargs)
        echo "will create ${guest_user} user for dev environemnts"
        ;;
      \?)
        echo "Invalid option: -${OPTARG}" >&2
        exit 1
        ;;
      :)
        echo "-u option requires a username"
        exit 1
        ;;
    esac
  done

  echo penguin > /etc/hostname
  echo '127.0.0.1 penguin' >> /etc/hosts
  echo '100.115.92.2 arc' >> /etc/hosts

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

  # chromeos-bsp-termina files for maitred
  mkdir -p /mnt/chromeos/fonts
  install -D -m 0755 -t /etc/maitred/50-mount-fonts.textproto \
    "${DATA_ROOT}/etc/maitred/50-mount-fonts.textproto"

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
  # Add /mnt/chromeos alias pointing to /mnt/shared for consistency with Crostini
  ln -s shared /mnt/chromeos

  apt-get update
  apt-get -y install "${CROS_PACKAGES[@]}"

  if [ -n "${guest_user}" ]; then
    # test user for debugging
    useradd -m -s /bin/bash -G \
      audio,cdrom,dialout,floppy,kvm,netdev,sudo,tss,video "${guest_user}"
    chpasswd <<< "${guest_user}:test0000"
    mkdir -p /var/lib/systemd/linger
    touch "/var/lib/systemd/linger/${guest_user}"
  fi

  # # TODO(b/271522474): leave networking to NM
  ln -sf /run/resolv.conf /etc/resolv.conf

  # cleanup
  apt-get clean
  rm -rf /opt/google/cros-containers/*
}

main "$@"
