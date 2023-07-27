// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BIOD_MOCK_CROS_FP_SESSION_MANAGER_H_
#define BIOD_MOCK_CROS_FP_SESSION_MANAGER_H_

#include "biod/cros_fp_session_manager.h"

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "biod/biod_storage.h"
#include "biod/cros_fp_record_manager.h"

namespace biod {

class MockCrosFpSessionManager : public CrosFpSessionManager {
 public:
  MockCrosFpSessionManager() = default;
  ~MockCrosFpSessionManager() override = default;

  MOCK_METHOD(const std::optional<std::string>&,
              GetUser,
              (),
              (const, override));
  MOCK_METHOD(bool, LoadUser, (std::string), (override));
  MOCK_METHOD(void, UnloadUser, (), (override));
  MOCK_METHOD(bool,
              CreateRecord,
              (const BiodStorageInterface::RecordMetadata&,
               std::unique_ptr<VendorTemplate>),
              (override));
  MOCK_METHOD(bool,
              UpdateRecord,
              (const BiodStorageInterface::RecordMetadata&,
               std::unique_ptr<VendorTemplate>),
              (override));
  MOCK_METHOD(bool, HasRecordId, (const std::string&), (override));
  MOCK_METHOD(bool, DeleteRecord, (const std::string&), (override));
  MOCK_METHOD(std::vector<SessionRecord>, GetRecords, (), (override));
  MOCK_METHOD(std::optional<BiodStorageInterface::RecordMetadata>,
              GetRecordMetadata,
              (size_t idx),
              (const, override));
  MOCK_METHOD(size_t, GetNumOfTemplates, (), (override));
};
}  // namespace biod

#endif  // BIOD_MOCK_CROS_FP_SESSION_MANAGER_H_
