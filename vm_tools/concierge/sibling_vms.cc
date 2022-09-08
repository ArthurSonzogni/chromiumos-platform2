// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "vm_tools/concierge/sibling_vms.h"

#include <linux/pci_regs.h>
#include <linux/vfio.h>
#include <linux/virtio_pci.h>
#include <sys/ioctl.h>
#include <sys/mman.h>

#include <string>
#include <utility>

#include <base/containers/span.h>
#include <base/files/file_enumerator.h>
#include <base/files/file_util.h>
#include <base/logging.h>
#include <base/files/memory_mapped_file.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/string_util.h>
#include <base/threading/thread.h>

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
// vendor ID.
constexpr char kPciVendorIdFileName[] = "vendor";

// Name of the file within the PCI device directory which contains a device's
// dwvicw ID.
constexpr char kPciDeviceIdFileName[] = "device";

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
// std::nullopt in case of any parsing errors.
std::optional<int64_t> GetPciDeviceVendorId(const base::FilePath& pci_device) {
  base::FilePath vendor_id_path = pci_device.Append(kPciVendorIdFileName);

  std::string vendor_id;
  if (!base::ReadFileToString(vendor_id_path, &vendor_id)) {
    LOG(ERROR) << "Failed to read vendor id for: " << pci_device;
    return std::nullopt;
  }

  // sysfs adds a newline to this value. Remove it.
  base::TrimString(vendor_id, "\n", &vendor_id);

  int64_t parsed_vendor_id;
  if (!base::HexStringToInt64(vendor_id, &parsed_vendor_id)) {
    LOG(ERROR) << "Failed to parse vendor id for: " << pci_device;
    return std::nullopt;
  }

  return parsed_vendor_id;
}

// Returns the device ID for the PCI device at |pci_device|. Returns
// std::nullopt in case of any parsing errors.
std::optional<int64_t> GetPciDeviceDeviceId(const base::FilePath& pci_device) {
  base::FilePath device_id_path = pci_device.Append(kPciDeviceIdFileName);

  std::string device_id;
  if (!base::ReadFileToString(device_id_path, &device_id)) {
    LOG(ERROR) << "Failed to read device id for: " << pci_device;
    return std::nullopt;
  }

  // sysfs adds a newline to this value. Remove it.
  base::TrimString(device_id, "\n", &device_id);

  int64_t parsed_device_id;
  if (!base::HexStringToInt64(device_id, &parsed_device_id)) {
    LOG(ERROR) << "Failed to parse device id for: " << pci_device;
    return std::nullopt;
  }

  return parsed_device_id;
}

// Opens the VFIO group file associated with |pci_device|.
base::File OpenVfioGroup(const base::FilePath& pci_device) {
  // The vfio group number is the same as the kernel iommu_group number
  // this file is symlinked to.
  base::FilePath dev_iommu_group = pci_device.Append("iommu_group");
  base::FilePath iommu_group;
  if (!base::ReadSymbolicLink(dev_iommu_group, &iommu_group)) {
    LOG(ERROR) << "Failed to read iommu group " << dev_iommu_group;
    return base::File();
  }

  // We need to wait for udev to update permissions on the vfio group file
  // before we can open it, which happens asynchronously after we rebind the
  // device to vfio-pci. Unfortunately, there is no easy way to wait for
  // this, so just poll. In practice, this should take <100ms.
  for (int i = 0; i < 50; i++) {
    base::File file(base::FilePath("/dev/vfio").Append(iommu_group.BaseName()),
                    base::File::Flags::FLAG_OPEN |
                        base::File::Flags::FLAG_READ |
                        base::File::Flags::FLAG_WRITE);
    if (file.IsValid()) {
      return file;
    }
    base::PlatformThread::Sleep(base::Milliseconds(50));
  }

  PLOG(ERROR) << "Failed to open vfio group";
  return base::File();
}

// Walks all the PCI capabilities of |vfio_device| and tries to find the bar
// and offset corresponding to the device's configuration.
//
// Returns std::nullopt if there is a parsing error or it can't find the
// location.
std::optional<PciDeviceConfigLocation> FindPciDeviceConfigLocation(
    base::File* vfio_device) {
  uint8_t config[kPciDeviceConfigurationSize] = {0};

  struct vfio_region_info reg = {};
  reg.argsz = sizeof(reg);
  reg.index = VFIO_PCI_CONFIG_REGION_INDEX;
  int ret =
      ioctl(vfio_device->GetPlatformFile(), VFIO_DEVICE_GET_REGION_INFO, &reg);
  if (ret != 0) {
    LOG(ERROR) << "Failed to get config region info: " << ret;
    return std::nullopt;
  }

  if (!vfio_device->ReadAndCheck(
          reg.offset, base::make_span(config).subspan(0, reg.size))) {
    PLOG(ERROR) << "Failed to read config";
    return std::nullopt;
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
      LOG(ERROR) << "Maxed out capability walk iterations for PCI devices";
      return std::nullopt;
    }

    virtio_pci_cap virtio_pci_cap = {0};
    // Ensure that no capability tries to access memory beyond configuration
    // header. This could both be a functionality as well as security issue.
    if (capability_offset + sizeof(virtio_pci_cap) >=
        kPciDeviceConfigurationSize) {
      LOG(ERROR) << "Encountered bad capability offset: " << capability_offset;
      return std::nullopt;
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

  return std::nullopt;
}

// This function returns the device configuration corresponding to |pci_device|.
// Returns std::nullopt if there's an error reading the device configuration.
//
// The caller must ensure that |pci_device| is a VVU device.
std::optional<VvuProxyDeviceConfig> ReadVvuProxyDeviceConfig(
    const base::FilePath& pci_device) {
  // Initialize VFIO access to |pci_device|.
  base::File vfio_container(base::FilePath("/dev/vfio/vfio"),
                            base::File::Flags::FLAG_OPEN |
                                base::File::Flags::FLAG_READ |
                                base::File::Flags::FLAG_WRITE);
  if (!vfio_container.IsValid()) {
    PLOG(ERROR) << "Failed to open vfio container";
    return std::nullopt;
  }

  if (ioctl(vfio_container.GetPlatformFile(), VFIO_GET_API_VERSION) !=
      VFIO_API_VERSION) {
    LOG(ERROR) << "VFIO API version mismatch";
    return std::nullopt;
  }

  base::File vfio_group = OpenVfioGroup(pci_device);
  if (!vfio_group.IsValid()) {
    return std::nullopt;
  }

  // Store the fd in a local variable because VFIO_GROUP_SET_CONTAINER
  // needs a pointer to the fd.
  base::PlatformFile container_fd = vfio_container.GetPlatformFile();
  int ret = ioctl(vfio_group.GetPlatformFile(), VFIO_GROUP_SET_CONTAINER,
                  &container_fd);
  if (ret != 0) {
    LOG(ERROR) << "Failed to set container: " << ret;
    return std::nullopt;
  }

  // We're not doing any IO, but we still can't get the device fd
  // without an IOMMU.
  ret =
      ioctl(vfio_container.GetPlatformFile(), VFIO_SET_IOMMU, VFIO_TYPE1_IOMMU);
  if (ret != 0) {
    LOG(ERROR) << "Failed to set VFIO IOMMU: " << ret;
    return std::nullopt;
  }

  ret = ioctl(vfio_group.GetPlatformFile(), VFIO_GROUP_GET_DEVICE_FD,
              pci_device.BaseName().MaybeAsASCII().c_str());
  if (ret < 0) {
    LOG(ERROR) << "Failed to get device fd: " << ret;
    return std::nullopt;
  }
  base::File vfio_device(ret);

  // Figure out which bar and what offset the device configuration is located.
  auto device_config_location = FindPciDeviceConfigLocation(&vfio_device);
  if (!device_config_location) {
    LOG(ERROR) << "Failed to find device config for " << pci_device;
    return std::nullopt;
  }

  // Read the bar at the offset calculated above to get the VVU device's
  // configuration.
  struct vfio_region_info reg = {};
  reg.argsz = sizeof(reg);
  reg.index = VFIO_PCI_BAR0_REGION_INDEX + device_config_location->bar;
  ret = ioctl(vfio_device.GetPlatformFile(), VFIO_DEVICE_GET_REGION_INFO, &reg);
  if (ret != 0) {
    LOG(ERROR) << "Failed to get config region info: " << ret;
    return std::nullopt;
  }

  VvuProxyDeviceConfig vvu_proxy_device_config;
  if (!vfio_device.ReadAndCheck(
          reg.offset + device_config_location->offset_in_bar,
          base::as_writable_bytes(
              base::make_span(&vvu_proxy_device_config, 1)))) {
    PLOG(ERROR) << "Failed to read device config";
    return std::nullopt;
  }
  return vvu_proxy_device_config;
}

// This function returns the socket index corresponding to |pci_device|. It does
// this by first getting its device configuration and then returning the socket
// index from the VVU device's UUID.
//
// The caller must ensure that |pci_device| is a VVU device.
std::optional<int32_t> GetVvuDeviceSocketIndex(
    const base::FilePath& pci_device) {
  auto vvu_proxy_device_config = ReadVvuProxyDeviceConfig(pci_device);
  if (!vvu_proxy_device_config) {
    return std::nullopt;
  }
  // The socket index is placed in the UUID at byte index |kVvuSocketIndexByte|.
  return vvu_proxy_device_config->uuid[kVvuSocketIndexByte];
}

// Returns true iff |pci_device| is a VVU device by comparing it's vendor id and
// device id.
bool IsVvuPciDevice(const base::FilePath& pci_device) {
  std::optional<int64_t> vendor_id = GetPciDeviceVendorId(pci_device);
  if (!vendor_id) {
    return false;
  }

  int64_t parsed_vendor_id = vendor_id.value();
  if (parsed_vendor_id != kVvuVendorId) {
    return false;
  }

  std::optional<int64_t> device_id = GetPciDeviceDeviceId(pci_device);
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

std::vector<VvuDeviceInfo> GetVvuDevicesInfo() {
  // PCI devices have paths like these /sys/devices/pci0000:02/0000:02:01.0.
  // The first enumerator is to look for "pci0000:02" under /sys/devices/.
  base::FileEnumerator pci_device_roots = base::FileEnumerator(
      base::FilePath(kPciDevicesPath), false /* recursive */,
      base::FileEnumerator::FileType::DIRECTORIES, kTopLevelPciDevicePattern);
  std::vector<VvuDeviceInfo> vvu_devices_info;

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
        VvuDeviceInfo device_info;
        device_info.proxy_device = pci_device;
        device_info.proxy_socket_index = socket_index.value();
        vvu_devices_info.push_back(std::move(device_info));
      } else {
        LOG(ERROR) << "Failed to get socket index for PCI device: "
                   << pci_device;
      }
    }
  }

  return vvu_devices_info;
}

}  // namespace concierge
}  // namespace vm_tools
