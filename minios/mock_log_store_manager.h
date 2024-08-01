// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MINIOS_MOCK_LOG_STORE_MANAGER_H_
#define MINIOS_MOCK_LOG_STORE_MANAGER_H_

#include <memory>
#include <optional>

#include <gmock/gmock.h>

#include "minios/log_store_manager.h"

namespace minios {

class MockLogStoreManager : public LogStoreManagerInterface {
 public:
  MockLogStoreManager() = default;
  ~MockLogStoreManager() override = default;

  MockLogStoreManager(const MockLogStoreManager&) = delete;
  MockLogStoreManager& operator=(const MockLogStoreManager&) = delete;

  MOCK_METHOD(bool,
              Init,
              (std::unique_ptr<DiskUtil> disk_util,
               std::unique_ptr<crossystem::Crossystem> cros_system,
               std::unique_ptr<libstorage::Platform> platform),
              (override));
  MOCK_METHOD(bool,
              SaveLogs,
              (LogDirection direction,
               const std::optional<base::FilePath>& path),
              (override));
  MOCK_METHOD(std::optional<bool>,
              FetchLogs,
              (LogDirection direction,
               const base::FilePath& dest_directory,
               const brillo::SecureBlob& key,
               const std::optional<base::FilePath>& encrypted_archive_path),
              (const, override));
  MOCK_METHOD(bool, ClearLogs, (), (const, override));
  MOCK_METHOD(void,
              SetLogStoreManifest,
              (std::unique_ptr<LogStoreManifestInterface>),
              (override));
};

}  // namespace minios

#endif  // MINIOS_MOCK_LOG_STORE_MANAGER_H_
