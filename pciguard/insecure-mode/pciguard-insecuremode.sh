#!/bin/bash
# Copyright 2024 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

err() {
  logger -p ERR "pciguard-insecuremode: $*"
}

auth_devices() {
  udevadm trigger --subsystem-match=thunderbolt || err "Auth devices failed; \
    device cannot create PCI tunnels"
}

usage() {
  cat <<EOF
Usage: $0

$0: is a helper script used to authorize PCI devices
when PCIGuard is in insecure mode. The script is launched by
pciguard-insecuremode.conf when insecure mode is detected.
EOF

  exit 1
}

main() {
  local d

  if [[ $# -ne 0 ]]; then
    usage
  fi

  echo 0 > /sys/bus/pci/drivers_allowlist_lockdown || \
    err "unable to open driver allowlist; drivers cannot be allowed to bind"

  for d in "pcieport" "xhci_hcd" "nvme" "ahci" "igc" "atlantic"; do
    echo "${d}" > /sys/bus/pci/drivers_allowlist || err "driver ${d} is not \
    allowed to bind; devices will not be able to use that driver"
  done

  auth_devices

  exit 0
}

main "$@"
