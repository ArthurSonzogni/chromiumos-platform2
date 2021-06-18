// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <base/check.h>
#include <base/files/file_enumerator.h>
#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/optional.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/string_piece.h>

#include "diagnostics/cros_healthd/fetchers/bus_fetcher.h"
#include "diagnostics/cros_healthd/fetchers/bus_fetcher_constants.h"
#include "diagnostics/cros_healthd/utils/error_utils.h"
#include "diagnostics/cros_healthd/utils/file_utils.h"

namespace diagnostics {
namespace {

namespace mojo_ipc = ::chromeos::cros_healthd::mojom;

template <typename T>
bool HexToUInt(base::BasicStringPiece<std::string> in, T* out) {
  uint32_t raw;
  if (!base::HexStringToUInt(in, &raw))
    return false;
  *out = static_cast<T>(raw);
  return true;
}

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

mojo_ipc::BusDeviceClass GetPciDeviceClass(
    const mojo_ipc::PciBusInfoPtr& info) {
  CHECK(info);
  // TODO(chungsheng): Implement this.
  return mojo_ipc::BusDeviceClass::kOthers;
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
  device->device_class = GetPciDeviceClass(pci_info);

  device->bus_info = mojo_ipc::BusInfo::NewPciBusInfo(std::move(pci_info));
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
  return mojo_ipc::BusResult::NewBusDevices(std::move(res));
}

}  // namespace diagnostics
