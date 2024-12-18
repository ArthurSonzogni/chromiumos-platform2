// Copyright 2018 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "vm_tools/concierge/tap_device_builder.h"

#include <fcntl.h>
#include <linux/if_tun.h>
#include <linux/sockios.h>
#include <net/if.h>
#include <net/if_arp.h>
#include <netinet/in.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>

#include <utility>

#include <base/logging.h>
#include <brillo/userdb_utils.h>
#include <chromeos/constants/vm_tools.h>

namespace vm_tools::concierge {
namespace {

// Path to the tun device.
constexpr char kTunDev[] = "/dev/net/tun";

// Size of the vnet header.
constexpr int32_t kVnetHeaderSize = 12;

}  // namespace

base::ScopedFD OpenTapDevice(const std::string& ifname_in,
                             bool vnet_hdr,
                             std::string* ifname_out) {
  if (ifname_in.empty()) {
    LOG(ERROR) << "An interface name must be provided";
    return base::ScopedFD();
  }

  // Explicitly not opened with close-on-exec because we want this fd to be
  // inherited by the child process.
  base::ScopedFD dev(open(kTunDev, O_RDWR | O_NONBLOCK));
  if (!dev.is_valid()) {
    PLOG(ERROR) << "Failed to open " << kTunDev;
    return dev;
  }

  // Open the interface.
  struct ifreq ifr;
  memset(&ifr, 0, sizeof(ifr));
  strncpy(ifr.ifr_name, ifname_in.c_str(), sizeof(ifr.ifr_name));
  ifr.ifr_flags = IFF_TAP | IFF_NO_PI;
  if (vnet_hdr) {
    ifr.ifr_flags |= IFF_VNET_HDR;
  }

  // This will overwrite the ifr_name field with the actual name of the
  // interface, if necessary.
  if (ioctl(dev.get(), TUNSETIFF, &ifr) != 0) {
    PLOG(ERROR) << "Failed to open tun interface " << ifname_in;
    return base::ScopedFD();
  }

  // The vnet header size and offloading flags only need to be set if we are
  // actually using the vnet_hdr feature.
  if (vnet_hdr) {
    // Set the vnet header size.
    if (ioctl(dev.get(), TUNSETVNETHDRSZ, &kVnetHeaderSize) != 0) {
      PLOG(ERROR) << "Failed to set vnet header size for vmtap interface "
                  << ifr.ifr_name;
      return base::ScopedFD();
    }

    // Set the offload flags.  These must match the virtio features advertised
    // by the net device in crosvm.
    if (ioctl(dev.get(), TUNSETOFFLOAD,
              TUN_F_CSUM | TUN_F_UFO | TUN_F_TSO4 | TUN_F_TSO6) != 0) {
      PLOG(ERROR) << "Failed to set offload for vmtap interface "
                  << ifr.ifr_name;
      return base::ScopedFD();
    }
  }

  if (ifname_out) {
    ifname_out->assign(ifr.ifr_name);
  }

  return dev;
}

}  // namespace vm_tools::concierge
