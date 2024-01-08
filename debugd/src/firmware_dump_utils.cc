// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "debugd/src/firmware_dump_utils.h"

#include <memory>
#include <utility>

#include <base/containers/fixed_flat_map.h>
#include <base/files/file_enumerator.h>
#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/logging.h>
#include <brillo/dbus/dbus_method_response.h>
#include <brillo/errors/error_codes.h>
#include <chromeos/dbus/debugd/dbus-constants.h>

#include "debugd/src/path_utils.h"

namespace {

// A look-up table for string representation of base directory that the device
// driver-specific debugfs will mount on.
// The full path to the debugfs file is model-dependent and may not exist when
// the driver unloads. The full path will be fetched by |FindDebugfsPath|.
constexpr auto kDirectoryToSearchMap =
    base::MakeFixedFlatMap<debugd::FirmwareDumpType, std::string_view>(
        {{debugd::FirmwareDumpType::WIFI, "/sys/kernel/debug/iwlwifi"}});

// A look-up table for debugfs file paths of each operation.
constexpr auto kDumperFileMap =
    base::MakeFixedFlatMap<debugd::FirmwareDumpOperation, std::string_view>(
        {{debugd::FirmwareDumpOperation::GenerateFirmwareDump,
          "iwlmvm/fw_dbg_collect"}});

}  // namespace

namespace debugd {

std::optional<base::FilePath> FindDebugfsPath(
    const FirmwareDumpType& fwdump_type,
    const FirmwareDumpOperation& fwdump_operation) {
  if (!kDirectoryToSearchMap.contains(fwdump_type)) {
    LOG(ERROR)
        << "Failed to find the debugfs base directory for firmware dump type: "
        << static_cast<uint32_t>(fwdump_type);
    return std::nullopt;
  }
  base::FilePath dumper_dir_to_search =
      path_utils::GetFilePath(kDirectoryToSearchMap.find(fwdump_type)->second);
  if (!base::PathExists(dumper_dir_to_search)) {
    LOG(ERROR) << "Failed to find debugfs base directory: "
               << dumper_dir_to_search.value();
    return std::nullopt;
  }
  base::FileEnumerator dir_enum(dumper_dir_to_search, /*recursive=*/false,
                                base::FileEnumerator::DIRECTORIES);
  if (!kDumperFileMap.contains(fwdump_operation)) {
    LOG(ERROR)
        << "Failed to find the debugfs file for firmware dump operation: "
        << static_cast<uint32_t>(fwdump_operation);
    return std::nullopt;
  }
  base::FilePath dumper_file =
      base::FilePath(kDumperFileMap.find(fwdump_operation)->second);
  for (base::FilePath dir_name = dir_enum.Next(); !dir_name.empty();
       dir_name = dir_enum.Next()) {
    base::FilePath dumper_path = dir_name.Append(dumper_file);
    if (base::PathExists(dumper_path)) {
      return dumper_path;
    }
  }
  LOG(ERROR) << "Failed to find dumper file " << dumper_file.value()
             << " under sub-directories of " << dumper_dir_to_search.value();
  return std::nullopt;
}

bool WriteToDebugfs(const FirmwareDumpType& fwdump_type,
                    const FirmwareDumpOperation& fwdump_operation,
                    std::string_view content) {
  const auto dumper_path = FindDebugfsPath(fwdump_type, fwdump_operation);
  if (!dumper_path.has_value()) {
    return false;
  }
  if (!base::WriteFile(dumper_path.value(), content)) {
    LOG(ERROR) << "Failed to trigger firmware dump by writing " << content
               << " into " << dumper_path->value();
    return false;
  }
  return true;
}

void GenerateFirmwareDumpHelper(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<bool>> response,
    const FirmwareDumpType& fwdump_type) {
  switch (fwdump_type) {
    case FirmwareDumpType::WIFI:
      if (!WriteToDebugfs(fwdump_type,
                          FirmwareDumpOperation::GenerateFirmwareDump, "1")) {
        response->ReplyWithError(FROM_HERE, brillo::errors::dbus::kDomain,
                                 DBUS_ERROR_FAILED,
                                 "Failed to write to debugfs");
        return;
      }
      break;
    default:
      response->ReplyWithError(
          FROM_HERE, brillo::errors::dbus::kDomain, DBUS_ERROR_FAILED,
          "Firmware dump operation is not supported for type: " +
              std::to_string(static_cast<uint32_t>(fwdump_type)));
      return;
  }
  // Response indicates success/failure of the debugfs call. So far we delegate
  // to the driver and assume the low-level execution is successful.
  response->Return(true);
}

}  // namespace debugd
