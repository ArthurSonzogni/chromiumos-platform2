// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DEBUGD_SRC_FIRMWARE_DUMP_UTILS_H_
#define DEBUGD_SRC_FIRMWARE_DUMP_UTILS_H_

#include <memory>
#include <string>

#include <base/files/file_path.h>
#include <brillo/dbus/dbus_method_response.h>
#include <chromeos/dbus/debugd/dbus-constants.h>

namespace debugd {

// Firmware dump operations supported by this utility.
enum class FirmwareDumpOperation {
  GenerateFirmwareDump,
};

// Find debugfs path based on |dumper_dir_to_search| and |dumper_file|.
std::optional<base::FilePath> FindDebugfsPath(
    const FirmwareDumpType& fwdump_type,
    const FirmwareDumpOperation& fwdump_operation);

// Writes |content| into the debugfs path based on |dumper_dir_to_search| and
// |dumper_file|. Full path can be found by |FindDebugfsPath| based on these
// parts.
// Returns true if the operation is successful.
bool WriteToDebugfs(const FirmwareDumpType& fwdump_type,
                    const FirmwareDumpOperation& fwdump_operation,
                    std::string_view content);

// Trigger firmware dump.
void GenerateFirmwareDumpHelper(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<bool>> response,
    const FirmwareDumpType& fwdump_type);

}  // namespace debugd

#endif  // DEBUGD_SRC_FIRMWARE_DUMP_UTILS_H_
