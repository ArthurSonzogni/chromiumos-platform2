// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "vm_tools/concierge/sibling_vms.h"

#include <linux/pci_regs.h>
#include <linux/virtio_pci.h>
#include <sys/mman.h>

#include <string>

#include <base/containers/span.h>
#include <base/files/file_enumerator.h>
#include <base/files/file_util.h>
#include <base/logging.h>
#include <base/files/memory_mapped_file.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/string_util.h>

namespace vm_tools {
namespace concierge {

namespace {

// Path where all PCI devices reside.
constexpr char kPciDevicesPath[] = "/sys/devices/";

// PCI devices have paths like these /sys/devices/pci0000:02/0000:02:01.0.
// This pattern is to search for the "pci0000:02" directory within
// /sys/devices/.
constexpr char kTopLevelPciDevicePattern[] = "pci0000:*";
// This pattern is to search for "0000:02:01.0" directory within
// /sys/devices/pci0000:02/.
constexpr char kSecondaryLevelPciDevicePattern[] = "0000:*";

// The Vendor and Device Ids that identify Virtio Vhost User devices.
constexpr int16_t kVvuVendorId = 0x1af4;
constexpr int16_t kVvuDeviceId = 0x107d;

// The byte which represents the Socket index of a VVU device in its
// |VvuProxyDeviceConfig|'s |uuid|.
constexpr int32_t kVvuSocketIndexByte = 15;

// Size of a PCI device's configuration.
constexpr int64_t kPciDeviceConfigurationSize = 256;

// Name of the file within the PCI device directory which contains a device's
// 256 byte configuration header.
constexpr char kPciConfigFileName[] = "config";

// Name of the file within the PCI device directory which contains a device's
// vendor ID.
constexpr char kPciVendorIdFileName[] = "vendor";

// Name of the file within the PCI device directory which contains a device's
// dwvicw ID.
constexpr char kPciDeviceIdFileName[] = "device";

// BAR 0 is present in the file "resource0", BAR 1 is present at "resource1".
constexpr char kPciBarFilePrefix[] = "resource";

// Offset in the configuration header at which the location of the first PCI
// capability is present.
constexpr int64_t kFirstCapabilityOffset = 0x34;

// Maximum number of PCI capabilities in a PCI device. This isn't defined
// anywhere but we define it as a sanity check.
constexpr int32_t kMaxPciCapabilities = 256;

// Encapsulates where a PCI device's configuration resides i.e. which bar and
// at what offset within that bar.
struct PciDeviceConfigLocation {
  int64_t bar;
  int64_t offset_in_bar;
};

// Size of the UUID in |VvuProxyDeviceConfig|.
constexpr int64_t kConfigUuidSize = 16;

// Device configuration of a Virtio Vhost User proxy device.
struct __attribute__((packed)) VvuProxyDeviceConfig {
  uint32_t status;
  uint32_t max_vhost_queues;
  uint8_t uuid[kConfigUuidSize];
};

// Returns the vendor ID for the PCI device at |pci_device|. Returns
// base::nullopt in case of any parsing errors.
base::Optional<int64_t> GetPciDeviceVendorId(const base::FilePath& pci_device) {
  base::FilePath vendor_id_path = pci_device.Append(kPciVendorIdFileName);

  std::string vendor_id;
  if (!base::ReadFileToString(vendor_id_path, &vendor_id)) {
    LOG(ERROR) << "Failed to read vendor id for: " << pci_device;
    return base::nullopt;
  }

  // sysfs adds a newline to this value. Remove it.
  base::TrimString(vendor_id, "\n", &vendor_id);

  int64_t parsed_vendor_id;
  if (!base::HexStringToInt64(vendor_id, &parsed_vendor_id)) {
    LOG(ERROR) << "Failed to parse vendor id for: " << pci_device;
    return base::nullopt;
  }

  return parsed_vendor_id;
}

// Returns the device ID for the PCI device at |pci_device|. Returns
// base::nullopt in case of any parsing errors.
base::Optional<int64_t> GetPciDeviceDeviceId(const base::FilePath& pci_device) {
  base::FilePath device_id_path = pci_device.Append(kPciDeviceIdFileName);

  std::string device_id;
  if (!base::ReadFileToString(device_id_path, &device_id)) {
    LOG(ERROR) << "Failed to read device id for: " << pci_device;
    return base::nullopt;
  }

  // sysfs adds a newline to this value. Remove it.
  base::TrimString(device_id, "\n", &device_id);

  int64_t parsed_device_id;
  if (!base::HexStringToInt64(device_id, &parsed_device_id)) {
    LOG(ERROR) << "Failed to parse device id for: " << pci_device;
    return base::nullopt;
  }

  return parsed_device_id;
}

// Walks all the PCI capabilities of |pci_device| and tries to find the bar and
// offset corresponding to the device's configuration.
//
// Returns base::nullopt if there is a parsing error or it can't find the
// location.
base::Optional<PciDeviceConfigLocation> GetPciDeviceConfigLocation(
    const base::FilePath& pci_device) {
  uint8_t config[kPciDeviceConfigurationSize] = {0};
  base::File pci_device_config(
      pci_device.Append(kPciConfigFileName),
      base::File::Flags::FLAG_OPEN | base::File::Flags::FLAG_READ);

  if (!pci_device_config.ReadAndCheck(0 /* offset */, config)) {
    LOG(ERROR) << "Failed to read PCI config for device: " << pci_device;
    return base::nullopt;
  }

  // Location of the first capability is at offset |kFirstCapabilityOffset|
  // within |config|.
  int64_t capability_offset = config[kFirstCapabilityOffset];

  // Walk the capability list to try and find the PCI device's configuration
  // location.
  int32_t num_tries = 0;
  while (capability_offset > 0) {
    // We don't want to be in an endless list of PCI capabilities. It may be a
    // malicious or malformed device. Bail in this situation.
    if (num_tries >= kMaxPciCapabilities) {
      LOG(ERROR) << "Maxed out capability walk iterations for PCI devices: "
                 << pci_device;
      return base::nullopt;
    }

    virtio_pci_cap virtio_pci_cap = {0};
    // Ensure that no capability tries to access memory beyond configuration
    // header. This could both be a functionality as well as security issue.
    if (capability_offset + sizeof(virtio_pci_cap) >=
        kPciDeviceConfigurationSize) {
      LOG(ERROR) << "Encountered bad capability offset: " << capability_offset
                 << " for PCI device: " << pci_device;
      return base::nullopt;
    }
    memcpy(&virtio_pci_cap, &config[capability_offset], sizeof(virtio_pci_cap));

    // If this is a vendor specific and device configuration related capability,
    // it will tells us about which BAR and at what offset to read the device
    // configuration.
    if (virtio_pci_cap.cap_vndr == PCI_CAP_ID_VNDR &&
        virtio_pci_cap.cfg_type == VIRTIO_PCI_CAP_DEVICE_CFG) {
      PciDeviceConfigLocation result;
      result.bar = virtio_pci_cap.bar;
      result.offset_in_bar = virtio_pci_cap.offset;
      return result;
    }
    capability_offset = virtio_pci_cap.cap_next;
    num_tries++;
  }

  return base::nullopt;
}

// This function returns the device configuration corresponding to |pci_device|.
// Returns base::nullopt if there's an error reading the device configuration.
//
// The caller must ensure that |pci_device| is a VVU device.
base::Optional<VvuProxyDeviceConfig> ReadVvuProxyDeviceConfig(
    const base::FilePath& pci_device) {
  // Figure out which bar and what offset the device configuration is located.
  auto device_config_location = GetPciDeviceConfigLocation(pci_device);
  if (!device_config_location) {
    return base::nullopt;
  }

  // Read the bar at the offset calculated above to get the VVU device's
  // configuration.
  //
  // BAR 0 is present in the file "resource0", BAR 1 is present at "resource1".
  base::FilePath pci_device_config_bar_path(pci_device.Append(
      kPciBarFilePrefix + std::to_string(device_config_location->bar)));
  // The BAR file can only be read via mmap.
  VvuProxyDeviceConfig vvu_proxy_device_config;
  base::MemoryMappedFile::Region device_config_region;
  device_config_region.offset = device_config_location->offset_in_bar;
  device_config_region.size = sizeof(vvu_proxy_device_config);
  base::MemoryMappedFile pci_device_config_bar;
  if (!pci_device_config_bar.Initialize(
          base::File(
              pci_device_config_bar_path,
              base::File::Flags::FLAG_OPEN | base::File::Flags::FLAG_READ),
          device_config_region) ||
      !pci_device_config_bar.IsValid()) {
    PLOG(ERROR) << "Failed to mmap BAR file: " << pci_device_config_bar_path;
    return base::nullopt;
  }

  if (pci_device_config_bar.length() < sizeof(vvu_proxy_device_config)) {
    LOG(ERROR) << "BAR size invalid: " << pci_device_config_bar.length();
    return base::nullopt;
  }

  uint8_t* addr = pci_device_config_bar.data();
  // TODO(b/196186396): For some reason the |memcpy| command below causes this
  // process to crash. For now we just populate the socket index value in the
  // UUID from |addr|. This is all we need right now.
  // memcpy((void*)(&vvu_proxy_device_config), addr,
  // sizeof(vvu_proxy_device_config));
  //
  // The socket index is the last byte present in |addr| and |uuid|.
  vvu_proxy_device_config.uuid[kVvuSocketIndexByte] =
      addr[sizeof(vvu_proxy_device_config) - 1];
  return vvu_proxy_device_config;
}

// This function returns the socket index corresponding to |pci_device|. It does
// this by first getting its device configuration and then returning the socket
// index from the VVU device's UUID.
//
// The caller must ensure that |pci_device| is a VVU device.
base::Optional<int32_t> GetVvuDeviceSocketIndex(
    const base::FilePath& pci_device) {
  auto vvu_proxy_device_config = ReadVvuProxyDeviceConfig(pci_device);
  if (!vvu_proxy_device_config) {
    return base::nullopt;
  }
  // The socket index is placed in the UUID at byte index |kVvuSocketIndexByte|.
  return vvu_proxy_device_config->uuid[kVvuSocketIndexByte];
}

// Returns true iff |pci_device| is a VVU device by comparing it's vendor id and
// device id.
bool IsVvuPciDevice(const base::FilePath& pci_device) {
  base::Optional<int64_t> vendor_id = GetPciDeviceVendorId(pci_device);
  if (!vendor_id) {
    return false;
  }

  int64_t parsed_vendor_id = vendor_id.value();
  if (parsed_vendor_id != kVvuVendorId) {
    return false;
  }

  base::Optional<int64_t> device_id = GetPciDeviceDeviceId(pci_device);
  if (!device_id) {
    return false;
  }

  int64_t parsed_device_id = device_id.value();
  if (parsed_device_id != kVvuDeviceId) {
    return false;
  }

  return true;
}

}  // namespace

std::vector<int32_t> GetVvuDevicesSocketIndices() {
  // PCI devices have paths like these /sys/devices/pci0000:02/0000:02:01.0.
  // The first enumerator is to look for "pci0000:02" under /sys/devices/.
  base::FileEnumerator pci_device_roots = base::FileEnumerator(
      base::FilePath(kPciDevicesPath), false /* recursive */,
      base::FileEnumerator::FileType::DIRECTORIES, kTopLevelPciDevicePattern);
  std::vector<int32_t> socket_indices;

  for (auto pci_device_root = pci_device_roots.Next(); !pci_device_root.empty();
       pci_device_root = pci_device_roots.Next()) {
    // The second enumerator is to look for "0000:02:01.0" under
    // /sys/devices/pci0000:02/.
    base::FileEnumerator pci_devices =
        base::FileEnumerator(pci_device_root, false /* recursive */,
                             base::FileEnumerator::FileType::DIRECTORIES,
                             kSecondaryLevelPciDevicePattern);

    // Iterate over each PCI device, check if it's a VVU device, if it is then
    // find its socket index.
    for (auto pci_device = pci_devices.Next(); !pci_device.empty();
         pci_device = pci_devices.Next()) {
      // Nothing to do if this isn't a VVU device.
      if (!IsVvuPciDevice(pci_device)) {
        continue;
      }

      LOG(INFO) << "Found VVU device: " << pci_device;
      auto socket_index = GetVvuDeviceSocketIndex(pci_device);
      if (socket_index) {
        LOG(INFO) << "Found VVU socket index: " << socket_index.value()
                  << " for PCI device: " << pci_device;
        socket_indices.push_back(socket_index.value());
      } else {
        LOG(ERROR) << "Failed to get socket index for PCI device: "
                   << pci_device;
      }
    }
  }

  return socket_indices;
}

}  // namespace concierge
}  // namespace vm_tools
