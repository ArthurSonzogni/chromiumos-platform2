// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MINIOS_MOCK_LOG_STORE_MANAGER_H_
#define MINIOS_MOCK_LOG_STORE_MANAGER_H_

#include <memory>

#include <gmock/gmock.h>

#include "minios/log_store_manager_interface.h"

namespace minios {

class MockLogStoreManager : public LogStoreManagerInterface {
 public:
  MockLogStoreManager() = default;

  MOCK_METHOD(bool,
              Init,
              (std::shared_ptr<DiskUtil> disk_util,
               std::shared_ptr<crossystem::Crossystem> cros_system,
               std::shared_ptr<CgptWrapperInterface> cgpt_wrapper),
              (override));
  MOCK_METHOD(bool,
              SaveLogs,
              (LogDirection direction,
               const std::optional<base::FilePath>& path),
              (override));
  MOCK_METHOD(bool,
              FetchLogs,
              (LogDirection direction,
               const base::FilePath& unencrypted_log_path,
               const std::optional<base::FilePath>& src_path),
              (const, override));
  MOCK_METHOD(bool, ClearLogs, (), (const, override));
};

}  // namespace minios

#endif  // MINIOS_MOCK_LOG_STORE_MANAGER_H_
