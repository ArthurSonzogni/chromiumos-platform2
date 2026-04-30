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
  cloud-init
  curl
  dkms
  dmidecode
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
  pipewire
  pipewire-pulse
  rsync
  shim-signed
  socat
  sudo
  systemd-timesyncd
  tpm2-tools
  usbutils
  vim-tiny
  wireplumber
  zstd
  # for bruschetta.Toolkit.*
  python3-gi gir1.2-gtk-3.0 gir1.2-gtk-4.0 libegl1
  python3-pyqt5 qtwayland5 python3-pyqt6 qt6-wayland
  python3-tk
  # for bruschetta.AppEmacs
  emacs
  # for vm-tools grpc
  git
  golang
  protobuf-compiler
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
vfat
iso9660
virtio_blk
virtio-pci
EOF

  if [[ "${UPDATE:-0}" == "0" ]]; then
    apt-get update
    apt-get -y install "${PACKAGES[@]}" --no-install-recommends

    # Install ttyd from GitHub as it is not in the standard repositories
    curl -L -o /usr/local/bin/ttyd \
      https://github.com/tsl0922/ttyd/releases/download/1.7.7/ttyd.x86_64
    chmod +x /usr/local/bin/ttyd

    rm -f /etc/locale.gen
    debconf-set-selections << EOF
locales locales/default_environment_locale select en_US.UTF-8
locales locales/locales_to_be_generated multiselect en_US.UTF-8 UTF-8
EOF
    dpkg-reconfigure locales

    # install the bootloader
    grub-install --uefi-secure-boot --target=x86_64-efi --no-nvram --removable
    grub-install --uefi-secure-boot --target=x86_64-efi --no-nvram
  fi

  install -m 0644 -t /etc/default/grub.d \
    "${DATA_ROOT}/etc/default/grub.d/50-reference-vm.cfg"
  update-grub

  install -m 0755 -t /usr/local/bin \
    "${DATA_ROOT}/usr/local/bin/update-cros-list" \
    "${DATA_ROOT}/usr/local/bin/refvm-mode-setup"

  install -D -m 0644 -t /usr/local/lib/systemd/journald.conf.d \
    "${DATA_ROOT}/usr/local/lib/systemd/journald.conf.d/50-console.conf"
  install -D -m 0644 -t /usr/local/lib/systemd/system \
    "${DATA_ROOT}/usr/local/lib/systemd/system/install-refvm.service" \
    "${DATA_ROOT}/usr/local/lib/systemd/system/maitred.service" \
    "${DATA_ROOT}/usr/local/lib/systemd/system/opt-google-cros\\x2dcontainers.mount" \
    "${DATA_ROOT}/usr/local/lib/systemd/system/refvm-mode-setup.service" \
    "${DATA_ROOT}/usr/local/lib/systemd/system/tmp.mount" \
    "${DATA_ROOT}/usr/local/lib/systemd/system/update-cros-list.service" \
    "${DATA_ROOT}/usr/local/lib/systemd/system/vshd.service"
  systemctl enable maitred.service update-cros-list.service vshd.service \
    refvm-mode-setup.service \
    'opt-google-cros\x2dcontainers.mount'
  systemctl daemon-reload

  for svc in cloud-config cloud-final cloud-init-local \
      cloud-init-main cloud-init-network cloud-init; do
    install -D -m 0644 -t "/etc/systemd/system/${svc}.service.d" \
      "${DATA_ROOT}/etc/systemd/system/${svc}.service.d/10-mode.conf"
  done

  for svc in cros-garcon sommelier@ sommelier-x@; do
    install -D -m 0644 -t "/etc/systemd/user/${svc}.service.d" \
      "${DATA_ROOT}/etc/systemd/user/${svc}.service.d/10-mode.conf"
  done

  install -D -m 0644 -t /etc/systemd/system/cros-garcon.service.d \
    "${DATA_ROOT}/etc/systemd/system/cros-garcon.service.d/10-mode.conf"

  install -D -m 0644 -t /usr/src/virtio-tpm-1 \
    "${DATA_ROOT}/usr/src/virtio-tpm-1/dkms.conf" \
    "${DATA_ROOT}/usr/src/virtio-tpm-1/Makefile" \
    "${DATA_ROOT}/usr/src/virtio-tpm-1/tpm.h" \
    "${DATA_ROOT}/usr/src/virtio-tpm-1/tpm_virtio.c"
  install -D -m 0644 -t /usr/src/virtio-wayland-1 \
    "${DATA_ROOT}/usr/src/virtio-wayland-1/dkms.conf" \
    "${DATA_ROOT}/usr/src/virtio-wayland-1/Makefile" \
    "${DATA_ROOT}/usr/src/virtio-wayland-1/virtio_wl.c"
  install -D -m 0644 -t /usr/src/virtio-wayland-1/include/linux \
    "${DATA_ROOT}/usr/src/virtio-wayland-1/include/linux/virtio_wl.h" \
    "${DATA_ROOT}/usr/src/virtio-wayland-1/include/linux/virtwl.h"
  install -D -m 0644 -t /var/lib/dkms "${DATA_ROOT}/var/lib/dkms/mok.pub"
  install -D -m 0600 -t /var/lib/dkms "${DATA_ROOT}/var/lib/dkms/mok.key"

  install -D -m 0440 -t /etc/sudoers.d \
    "${DATA_ROOT}/etc/sudoers.d/10-no-password"

  install -D -m 0644 -t /etc/cloud/cloud.cfg.d \
    "${DATA_ROOT}/etc/cloud/cloud.cfg.d/99-reference-vm.cfg"

  install -D -m 0755 -t /usr/local/bin \
    "${DATA_ROOT}/usr/local/bin/install-refvm"
  install -D -m 0644 -t /usr/local/share/refvm \
    "${DATA_ROOT}/usr/local/share/refvm/disk_config.tpl"

  if [[ "${UPDATE:-0}" == "0" ]]; then
    # Build the status reporter
    export GOPATH=/tmp/go
    export PATH=${PATH}:${GOPATH}/bin
    go install google.golang.org/protobuf/cmd/protoc-gen-go@v1.36.11
    go install google.golang.org/grpc/cmd/protoc-gen-go-grpc@v1.6.0

    mkdir -p /tmp/build/vm_rpc
    cp "${DATA_ROOT}/usr/src/vm_install_reporter/"* /tmp/build/
    cd /tmp/build
    protoc --go_out=vm_rpc --go_opt=paths=source_relative \
           --go-grpc_out=vm_rpc --go-grpc_opt=paths=source_relative \
           vm_rpc.proto

    go mod init vm_install_reporter
    go mod tidy
    go build -o report_install_status report_install_status.go
    install -m 0755 report_install_status /usr/local/bin/

    # Find the installed, not running, kernel version.
    kernel="$(dpkg-query -Wf '${Package}\n' 'linux-image-*-amd64' | \
      tail -n 1 | sed -E -e 's/linux-image-//')"
    dkms install virtio-tpm/1 -k "${kernel}"
    dkms install virtio-wayland/1 -k "${kernel}"

    # chromeos guest tools repo
    curl https://dl.google.com/linux/linux_signing_key.pub | gpg --dearmor > \
      /usr/share/keyrings/cros.gpg
    # shellcheck disable=SC2154
    echo "deb [signed-by=/usr/share/keyrings/cros.gpg]" \
      "${CROS_PACKAGES_URL} ${RELEASE} main" > \
      /etc/apt/sources.list.d/cros.list
  fi

  # dummy files for installation
  mkdir -p /opt/google/cros-containers/bin
  touch /opt/google/cros-containers/bin/sommelier
  # Required for boot with R/O rootfs
  mkdir -p /mnt/shared
  # Required for disk ballooning
  mkdir -p /mnt/stateful

  if [[ "${UPDATE:-0}" == "0" ]]; then
    apt-get update
    apt-get -y install "${CROS_PACKAGES[@]}"

    # Provide "vim" binary using vim-tiny with low priority.
    update-alternatives --install /usr/bin/vim vim /usr/bin/vim.tiny 10

    # test user for debugging
    useradd -m -s /bin/bash -G audio,sudo,tss chronos
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
  fi

  # TODO(b/271522474): leave networking to NM
  ln -sf /run/resolv.conf /etc/resolv.conf

  # cleanup
  apt-get clean
  rm -rf /var/lib/apt/lists
  rm -rf /opt/google/cros-containers/*
  rm -rf /tmp/go /tmp/build
}

main "$@"
