#!/bin/bash
# Copyright 2023 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
#
# setup.sh is run inside a chroot of the refvm image to configure it.
# It's launched by build.py.
#
set -eux -o pipefail

CODIUM_VERSION="1.81.1.23222"
CROS_PACKAGES=(
  cros-garcon
  cros-sommelier
  cros-sommelier-config
  cros-wayland
)
PACKAGES=(
  # base packages
  bash-completion
  ca-certificates
  curl
  dkms
  dosfstools
  efibootmgr
  fai-setup-storage
  gpg
  grub-efi-amd64
  grub-efi-amd64-signed
  gsettings-desktop-schemas
  linux-headers-amd64
  linux-image-amd64
  locales
  lvm2
  pciutils
  rsync
  shim-signed
  tpm2-tools
  usbutils
  vim-tiny
  sudo
  systemd-timesyncd
  zstd
  # for bruschetta.Toolkit.*
  python3-gi gir1.2-gtk-3.0 gir1.2-gtk-4.0 libegl1
  python3-pyqt5 qtwayland5 python3-pyqt6 qt6-wayland
  python3-tk
  # for bruschetta.AppEmacs
  emacs
)
DATA_ROOT="/tmp/data"

main() {
  export DEBIAN_FRONTEND=noninteractive

  echo refvm > /etc/hostname
  sed -i -e '2i127.0.1.1       refvm' /etc/hosts

  # Use minimal initramfs settings.
  mkdir -p /etc/initramfs-tools/conf.d
  echo "MODULES=list" > /etc/initramfs-tools/conf.d/10-refvm.conf
  cat << EOF >> /etc/initramfs-tools/modules
ext4
virtio_blk
virtio-pci
EOF

  apt-get update
  apt-get -y install "${PACKAGES[@]}" --no-install-recommends

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

  install -D -m 0644 -t /usr/local/lib/systemd/journald.conf.d \
    "${DATA_ROOT}/usr/local/lib/systemd/journald.conf.d/50-console.conf"
  install -D -m 0644 -t /usr/local/lib/systemd/system \
    "${DATA_ROOT}/usr/local/lib/systemd/system/install-refvm.service" \
    "${DATA_ROOT}/usr/local/lib/systemd/system/maitred.service" \
    "${DATA_ROOT}/usr/local/lib/systemd/system/opt-google-cros\\x2dcontainers.mount" \
    "${DATA_ROOT}/usr/local/lib/systemd/system/tmp.mount" \
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
  install -D -m 0644 -t /usr/src/virtio-tpm-0 \
    "${DATA_ROOT}/usr/src/virtio-tpm-0/dkms.conf" \
    "${DATA_ROOT}/usr/src/virtio-tpm-0/Makefile" \
    "${DATA_ROOT}/usr/src/virtio-tpm-0/tpm.h" \
    "${DATA_ROOT}/usr/src/virtio-tpm-0/tpm_virtio.c"
  install -D -m 0644 -t /var/lib/dkms "${DATA_ROOT}/var/lib/dkms/mok.pub"
  install -D -m 0600 -t /var/lib/dkms "${DATA_ROOT}/var/lib/dkms/mok.key"

  install -D -m 0440 -t /etc/sudoers.d \
    "${DATA_ROOT}/etc/sudoers.d/10-no-password"

  install -D -m 0755 -t /usr/local/bin \
    "${DATA_ROOT}/usr/local/bin/install-refvm"
  install -D -m 0644 -t /usr/local/share/refvm \
    "${DATA_ROOT}/usr/local/share/refvm/disk_config.tpl"

  # Find the installed, not running, kernel version.
  kernel="$(dpkg-query -Wf '${Package}\n' 'linux-image-*-amd64' | tail -n 1 | \
    sed -E -e 's/linux-image-//')"
  dkms install virtio-wayland/0 -k "${kernel}"
  dkms install virtio-tpm/0 -k "${kernel}"

  # chromeos guest tools repo
  curl https://dl.google.com/linux/linux_signing_key.pub | gpg --dearmor > \
    /usr/share/keyrings/cros.gpg
  # shellcheck disable=SC2154
  echo "deb [signed-by=/usr/share/keyrings/cros.gpg] ${CROS_PACKAGES_URL} ${RELEASE} main" > \
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

  # Provide "vim" binary using vim-tiny with low priority.
  update-alternatives --install /usr/bin/vim vim /usr/bin/vim.tiny 10

  # test user for debugging
  useradd -m -s /bin/bash -G sudo,tss chronos
  chpasswd <<< chronos:test0000
  mkdir -p /var/lib/systemd/linger
  touch /var/lib/systemd/linger/chronos

  # Run the refvm installer on startup, if the appropriate OEM string is set.
  # We do this in .profile so that install messages are shown in the terminal.
  cat << "EOF" >> /home/chronos/.profile
run_installer() {
  if sudo dmidecode -t 11 -q | grep -q refvm:install=true; then
    interactive=true
    if sudo dmidecode -t 11 -q | grep -q refvm:noninteractive=true; then
      interactive=false
    fi

    sudo journalctl --follow --no-tail --unit=install-refvm &
    # No stdin for systemctl to avoid changing terminal options.
    sudo systemctl --quiet start install-refvm.service < /dev/null
    kill %1

    if ! systemctl --quiet is-active install-refvm.service; then
      if [[ "${interactive}" == true ]]; then
        echo "Returning to a shell for debugging."
        return
      fi
    fi

    if [[ "${interactive}" == true ]]; then
      read -r -p "Press ENTER to shut down."
    fi

    sudo systemctl poweroff
    exit
  fi
}

run_installer
EOF

  # Disable garcon auto-updates.
  sed -i -E \
    -e 's/(DisableAutomaticCrosPackageUpdates=)false/\1true/' \
    -e 's/(DisableAutomaticSecurityUpdates=)false/\1true/' \
    /home/chronos/.config/cros-garcon.conf

  curl -L -o /tmp/codium.deb \
    "https://storage.googleapis.com/chromiumos-test-assets-public/crostini_test_files/codium_${CODIUM_VERSION}_amd64.deb"
  apt-get install -y /tmp/codium.deb

  # TODO(b/271522474): leave networking to NM
  ln -sf /run/resolv.conf /etc/resolv.conf

  # cleanup
  apt-get clean
  rm -rf /var/lib/apt/lists
  rm -rf /opt/google/cros-containers/*
}

main "$@"
