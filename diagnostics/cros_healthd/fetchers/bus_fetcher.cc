// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <algorithm>
#include <memory>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include <base/check.h>
#include <base/files/file_enumerator.h>
#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/optional.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/string_piece.h>
#include <base/strings/stringprintf.h>

#include "diagnostics/cros_healthd/fetchers/bus_fetcher.h"
#include "diagnostics/cros_healthd/fetchers/bus_fetcher_constants.h"
#include "diagnostics/cros_healthd/utils/error_utils.h"
#include "diagnostics/cros_healthd/utils/file_utils.h"

namespace diagnostics {
namespace {

namespace mojo_ipc = ::chromeos::cros_healthd::mojom;

template <typename T>
bool HexToUInt(base::StringPiece in, T* out) {
  uint32_t raw;
  if (!base::HexStringToUInt(in, &raw))
    return false;
  *out = static_cast<T>(raw);
  return true;
}

const auto& HexToU8 = HexToUInt<uint8_t>;
const auto& HexToU16 = HexToUInt<uint16_t>;

std::vector<base::FilePath> ListDirectory(const base::FilePath& path) {
  std::vector<base::FilePath> res;
  base::FileEnumerator file_enum(
      path, /*recursive=*/false,
      base::FileEnumerator::FileType::FILES |
          base::FileEnumerator::FileType::DIRECTORIES |
          base::FileEnumerator::FileType::SHOW_SYM_LINKS);
  for (auto path = file_enum.Next(); !path.empty(); path = file_enum.Next()) {
    res.push_back(path);
  }
  return res;
}

base::Optional<std::string> GetDriver(const base::FilePath& path) {
  base::FilePath driver_path;
  if (base::ReadSymbolicLink(path.Append(kFileDriver), &driver_path))
    return driver_path.BaseName().value();
  return base::nullopt;
}

mojo_ipc::PciBusInfoPtr FetchPciInfo(const base::FilePath& path) {
  auto info = mojo_ipc::PciBusInfo::New();
  uint32_t class_raw;
  if (!ReadInteger(path, kFilePciClass, &base::HexStringToUInt, &class_raw) ||
      !ReadInteger(path, kFilePciDevice, &HexToU16, &info->device_id) ||
      !ReadInteger(path, kFilePciVendor, &HexToU16, &info->vendor_id))
    return nullptr;
  info->class_id = GET_PCI_CLASS(class_raw);
  info->subclass_id = GET_PCI_SUBCLASS(class_raw);
  info->prog_if_id = GET_PCI_PROG_IF(class_raw);

  info->driver = GetDriver(path);
  return info;
}

// Some devices cannot be identified by their class id. Try to identify them by
// checking the sysfs structure.
mojo_ipc::BusDeviceClass GetDeviceClassBySysfs(const base::FilePath& path) {
  if (PathExists(path.Append("bluetooth")))
    return mojo_ipc::BusDeviceClass::kBluetoothAdapter;
  const auto net = path.Append("net");
  if (PathExists(net)) {
    for (const auto& nic_path : ListDirectory(net)) {
      const auto name = nic_path.BaseName().value();
      if (name.find("eth") == 0)
        return mojo_ipc::BusDeviceClass::kEthernetController;
      if (name.find("wlan") == 0)
        return mojo_ipc::BusDeviceClass::kWirelessController;
    }
  }
  return mojo_ipc::BusDeviceClass::kOthers;
}

mojo_ipc::BusDeviceClass GetPciDeviceClass(
    const base::FilePath& path, const mojo_ipc::PciBusInfoPtr& info) {
  CHECK(info);
  if (info->class_id == pci_ids::display::kId)
    return mojo_ipc::BusDeviceClass::kDisplayController;
  if (info->class_id == pci_ids::network::kId) {
    if (info->subclass_id == pci_ids::network::ethernet::kId)
      return mojo_ipc::BusDeviceClass::kEthernetController;
    if (info->subclass_id == pci_ids::network::network::kId)
      return mojo_ipc::BusDeviceClass::kWirelessController;
  }
  return GetDeviceClassBySysfs(path);
}

mojo_ipc::BusDevicePtr FetchPciDevice(
    const base::FilePath& path, const std::unique_ptr<PciUtil>& pci_util) {
  auto pci_info = FetchPciInfo(path);
  if (pci_info.is_null())
    return nullptr;

  auto device = mojo_ipc::BusDevice::New();
  device->vendor_name = pci_util->GetVendorName(pci_info->vendor_id);
  device->product_name =
      pci_util->GetDeviceName(pci_info->vendor_id, pci_info->device_id);
  device->device_class = GetPciDeviceClass(path, pci_info);

  device->bus_info = mojo_ipc::BusInfo::NewPciBusInfo(std::move(pci_info));
  return device;
}

mojo_ipc::UsbBusInterfaceInfoPtr FetchUsbBusInterfaceInfo(
    const base::FilePath& path) {
  auto info = mojo_ipc::UsbBusInterfaceInfo::New();
  if (!ReadInteger(path, kFileUsbIFNumber, &HexToU8, &info->interface_number) ||
      !ReadInteger(path, kFileUsbIFClass, &HexToU8, &info->class_id) ||
      !ReadInteger(path, kFileUsbIFSubclass, &HexToU8, &info->subclass_id) ||
      !ReadInteger(path, kFileUsbIFProtocol, &HexToU8, &info->protocol_id))
    return nullptr;
  info->driver = GetDriver(path);
  return info;
}

mojo_ipc::UsbBusInfoPtr FetchUsbBusInfo(const base::FilePath& path) {
  auto info = mojo_ipc::UsbBusInfo::New();
  if (!ReadInteger(path, kFileUsbDevClass, &HexToU8, &info->class_id) ||
      !ReadInteger(path, kFileUsbDevSubclass, &HexToU8, &info->subclass_id) ||
      !ReadInteger(path, kFileUsbDevProtocol, &HexToU8, &info->protocol_id) ||
      !ReadInteger(path, kFileUsbVendor, &HexToU16, &info->vendor_id) ||
      !ReadInteger(path, kFileUsbProduct, &HexToU16, &info->product_id))
    return nullptr;
  for (const auto& if_path : ListDirectory(path)) {
    auto if_info = FetchUsbBusInterfaceInfo(if_path);
    if (if_info) {
      info->interfaces.push_back(std::move(if_info));
    }
  }
  sort(info->interfaces.begin(), info->interfaces.end(),
       [](const mojo_ipc::UsbBusInterfaceInfoPtr& a,
          const mojo_ipc::UsbBusInterfaceInfoPtr& b) {
         return a->interface_number < b->interface_number;
       });
  return info;
}

std::tuple<std::string, std::string> GetUsbNames(
    const base::FilePath& path,
    const mojo_ipc::UsbBusInfoPtr& info,
    const std::unique_ptr<UdevHwdb>& hwdb) {
  CHECK(info);
  auto modalias =
      base::StringPrintf("usb:v%04Xp%04X", info->vendor_id, info->product_id);
  auto propertie = hwdb->GetProperties(modalias);
  // Try to get vendor and product name from hwdb. If fail, try to read them
  // from sysfs.
  auto vendor = propertie[kPropertieVendor];
  if (vendor == "") {
    ReadAndTrimString(path, kFileUsbManufacturerName, &vendor);
  }
  auto product = propertie[kPropertieProduct];
  if (product == "") {
    ReadAndTrimString(path, kFileUsbProductName, &product);
  }
  return std::make_tuple(vendor, product);
}

mojo_ipc::BusDeviceClass GetUsbDeviceClass(
    const base::FilePath& path, const mojo_ipc::UsbBusInfoPtr& info) {
  CHECK(info);
  if (info->class_id == usb_ids::wireless::kId &&
      info->subclass_id == usb_ids::wireless::radio_frequency::kId &&
      info->protocol_id == usb_ids::wireless::radio_frequency::bluetooth::kId) {
    return mojo_ipc::BusDeviceClass::kBluetoothAdapter;
  }
  // Try to get the type by checking the type of each interface.
  for (const auto& if_path : ListDirectory(path)) {
    // |if_path| is an interface if and only if |kFileUsbIFNumber| exist.
    if (!PathExists(if_path.Append(kFileUsbIFNumber)))
      continue;
    auto type = GetDeviceClassBySysfs(if_path);
    if (type != mojo_ipc::BusDeviceClass::kOthers)
      return type;
  }
  return mojo_ipc::BusDeviceClass::kOthers;
}

mojo_ipc::BusDevicePtr FetchUsbDevice(const base::FilePath& path,
                                      const std::unique_ptr<UdevHwdb>& hwdb) {
  auto usb_info = FetchUsbBusInfo(path);
  if (usb_info.is_null())
    return nullptr;
  auto device = mojo_ipc::BusDevice::New();
  std::tie(device->vendor_name, device->product_name) =
      GetUsbNames(path, usb_info, hwdb);
  device->device_class = GetUsbDeviceClass(path, usb_info);

  device->bus_info = mojo_ipc::BusInfo::NewUsbBusInfo(std::move(usb_info));
  return device;
}

}  // namespace

mojo_ipc::BusResultPtr BusFetcher::FetchBusDevices() {
  const auto& root = context_->root_dir();
  std::vector<mojo_ipc::BusDevicePtr> res;

  auto pci_util = context_->udev()->CreatePciUtil();
  for (const auto& path : ListDirectory(root.Append(kPathSysPci))) {
    auto device = FetchPciDevice(path, pci_util);
    if (device) {
      res.push_back(std::move(device));
    }
  }
  auto hwdb = context_->udev()->CreateHwdb();
  for (const auto& path : ListDirectory(root.Append(kPathSysUsb))) {
    auto device = FetchUsbDevice(path, hwdb);
    if (device) {
      res.push_back(std::move(device));
    }
  }
  return mojo_ipc::BusResult::NewBusDevices(std::move(res));
}

}  // namespace diagnostics
