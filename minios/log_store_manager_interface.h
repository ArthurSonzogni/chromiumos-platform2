// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MINIOS_LOG_STORE_MANAGER_INTERFACE_H_
#define MINIOS_LOG_STORE_MANAGER_INTERFACE_H_

#include <memory>
#include <optional>

#include <base/files/file_path.h>
#include <libcrossystem/crossystem.h>

#include "minios/cgpt_wrapper.h"
#include "minios/cgpt_wrapper_interface.h"
#include "minios/disk_util.h"

namespace minios {

// Interface for a log store manager class.
class LogStoreManagerInterface {
 public:
  enum class LogDirection {
    Disk,
    Stateful,
    RemovableDevice,
  };

  virtual ~LogStoreManagerInterface() = default;

  virtual bool Init(
      std::shared_ptr<DiskUtil> disk_util = std::make_shared<DiskUtil>(),
      std::shared_ptr<crossystem::Crossystem> cros_system =
          std::make_shared<crossystem::Crossystem>(),
      std::shared_ptr<CgptWrapperInterface> cgpt_wrapper =
          std::make_shared<CgptWrapper>()) = 0;

  // Save logs to a specified direction. If the direction is not `Disk`, logs
  // will be written to `path`.
  virtual bool SaveLogs(
      LogDirection direction,
      const std::optional<base::FilePath>& path = std::nullopt) = 0;

  // Fetch logs from a specified direction and put them at
  // `unencrypted_archive_path`. True on success, false on failure.
  virtual bool FetchLogs(LogDirection direction,
                         const base::FilePath& unencrypted_archive_path,
                         const std::optional<base::FilePath>&
                             encrypted_archive_path = std::nullopt) const = 0;

  // Clear logs on disk.
  virtual bool ClearLogs() const = 0;
};

}  // namespace minios

#endif  // MINIOS_LOG_STORE_MANAGER_INTERFACE_H_
