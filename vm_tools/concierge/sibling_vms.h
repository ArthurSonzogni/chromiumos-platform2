// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef VM_TOOLS_CONCIERGE_SIBLING_VMS_H_
#define VM_TOOLS_CONCIERGE_SIBLING_VMS_H_

#include <stdint.h>

#include <vector>

#include <base/files/file_path.h>

namespace vm_tools {
namespace concierge {

// Contains the info related to a VVU device.
struct VvuDeviceInfo {
  // The path of a proxy device corresponding to a VVU device e.g.
  // "/sys/devices/pci0000:00/00:00:0a".
  base::FilePath proxy_device;

  // Socket index corresponding to a VVU proxy device. The VVU devices all use a
  // socket with a path like: <Some-Prefix>/%d (|proxy_socket_index|).
  int32_t proxy_socket_index;
};

// Parses all PCI devices, looks for any VVU devices and returns their
// corresponding info.
std::vector<VvuDeviceInfo> GetVvuDevicesInfo();

}  // namespace concierge
}  // namespace vm_tools

#endif  // VM_TOOLS_CONCIERGE_SIBLING_VMS_H_
