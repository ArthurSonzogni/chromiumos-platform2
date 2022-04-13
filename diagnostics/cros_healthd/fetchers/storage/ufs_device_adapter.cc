// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/fetchers/storage/ufs_device_adapter.h"

#include <algorithm>
#include <cstdint>
#include <string>

#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/string_util.h>
#include <base/strings/stringprintf.h>
#include <brillo/blkdev_utils/ufs.h>

#include "diagnostics/common/statusor.h"
#include "diagnostics/cros_healthd/utils/file_utils.h"

namespace diagnostics {

namespace {

namespace mojom = ::ash::cros_healthd::mojom;

constexpr char kManfidFile[] = "device_descriptor/manufacturer_id";
constexpr char kModelFile[] = "device/model";
constexpr char kFirmwareVersionFile[] = "device/rev";

constexpr size_t kU64Size = 8;

// Convenience wrapper for error status.
Status ReadFailure(const base::FilePath& path) {
  return Status(StatusCode::kUnavailable,
                base::StringPrintf("Failed to read %s", path.value().c_str()));
}
Status ControllerFailure(const base::FilePath& path) {
  return Status(StatusCode::kUnavailable,
                base::StringPrintf("Failed to get controller node for %s",
                                   path.value().c_str()));
}

}  // namespace

UfsDeviceAdapter::UfsDeviceAdapter(const base::FilePath& dev_sys_path)
    : dev_sys_path_(dev_sys_path) {}

std::string UfsDeviceAdapter::GetDeviceName() const {
  return dev_sys_path_.BaseName().value();
}

StatusOr<mojom::BlockDeviceVendor> UfsDeviceAdapter::GetVendorId() const {
  uint32_t value;
  base::FilePath controller_node =
      brillo::UfsSysfsToControllerNode(dev_sys_path_);
  if (controller_node.empty()) {
    return ControllerFailure(dev_sys_path_);
  }

  if (!ReadInteger(controller_node, kManfidFile, &base::HexStringToUInt,
                   &value)) {
    return ReadFailure(controller_node.Append(kManfidFile));
  }

  mojom::BlockDeviceVendor result;
  result.set_jedec_manfid(value);
  return result;
}

StatusOr<mojom::BlockDeviceProduct> UfsDeviceAdapter::GetProductId() const {
  // No meaningful numerical product id. Use model instead.
  mojom::BlockDeviceProduct result;
  result.set_other(0);
  return result;
}

StatusOr<mojom::BlockDeviceRevision> UfsDeviceAdapter::GetRevision() const {
  // No meaningful numerical revision.
  mojom::BlockDeviceRevision result;
  result.set_other(0);
  return result;
}

StatusOr<std::string> UfsDeviceAdapter::GetModel() const {
  std::string model;
  if (!ReadAndTrimString(dev_sys_path_, kModelFile, &model)) {
    return ReadFailure(dev_sys_path_.Append(kModelFile));
  }
  return model;
}

StatusOr<mojom::BlockDeviceFirmware> UfsDeviceAdapter::GetFirmwareVersion()
    const {
  std::string str_value;
  if (!ReadAndTrimString(dev_sys_path_, kFirmwareVersionFile, &str_value)) {
    return ReadFailure(dev_sys_path_.Append(kFirmwareVersionFile));
  }

  // This is not entirely correct. UFS exports revision as 4 2-byte unicode
  // characters. But Linux's UFS subsystem converts it to a raw ascii string.
  // This is a temporary fixture to provide meaningful info on this aspect.
  // TODO(dlunev): use raw representation, either through ufs-utils, or create
  // a new kernel's node.
  char bytes[kU64Size] = {0};
  memcpy(bytes, str_value.c_str(), std::min(str_value.length(), kU64Size));
  uint64_t value = *reinterpret_cast<uint64_t*>(bytes);

  mojom::BlockDeviceFirmware result;
  result.set_ufs_fwrev(value);
  return result;
}

}  // namespace diagnostics
