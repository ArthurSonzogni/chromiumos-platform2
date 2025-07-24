// Copyright 2025 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "installer/platform.h"

#include <optional>
#include <string_view>

#include "base/files/file_util.h"
#include "base/logging.h"
#include "base/strings/string_util.h"
#include "installer/cgpt_manager.h"
#include "installer/inst_util.h"

namespace {
constexpr std::string_view kDmiSysPath = "/sys/class/dmi/id";
constexpr std::string_view kDmiProductNameFile = "product_name";
constexpr std::string_view kDmiSysVendorFile = "sys_vendor";

std::optional<std::string_view> DmiKeyToString(DmiKey key) {
  switch (key) {
    case DmiKey::kProductName:
      return kDmiProductNameFile;
    case DmiKey::kSysVendor:
      return kDmiSysVendorFile;
  }
  LOG(ERROR) << "Invalid enum value for DmiKey";
  return std::nullopt;
}
}  // namespace

Platform::~Platform() = default;

std::string PlatformImpl::DumpKernelConfig(
    const base::FilePath& kernel_dev) const {
  return ::DumpKernelConfig(kernel_dev);
}

std::optional<Guid> PlatformImpl::GetPartitionUniqueId(
    const base::FilePath& base_device, PartitionNum partition_num) const {
  CgptManager cgpt(base_device);
  Guid guid;
  if (cgpt.GetPartitionUniqueId(partition_num, &guid) !=
      CgptErrorCode::kSuccess) {
    return std::nullopt;
  }

  return guid;
}

std::optional<std::string> PlatformImpl::ReadDmi(DmiKey key) const {
  const std::optional<std::string_view> dmi_file = DmiKeyToString(key);
  if (!dmi_file.has_value()) {
    return std::nullopt;
  }

  // *dmi_file is OK, checked above.
  base::FilePath dmi_path = base::FilePath(kDmiSysPath).Append(*dmi_file);
  std::string value;
  if (!base::ReadFileToString(dmi_path, &value)) {
    PLOG(ERROR) << "Failed to read DMI information from " << dmi_path;
    return std::nullopt;
  }

  base::TrimWhitespaceASCII(value, base::TRIM_ALL, &value);
  return value;
}
